'use strict';

const fs = require('fs');
const path = require('path');

const { config, constants, validateStartupConfig } = require('./config');
const utils = require('./utils');
const db = require('./db');

const cors = require('cors');
const crypto = require('crypto');
const express = require('express');
const http = require('http');
const jwt = require('jsonwebtoken');
const mysql = require('mysql2/promise');
const nodemailer = require('nodemailer');
const WebSocket = require('ws');
const { WebSocketServer } = WebSocket;
const { createMigrations } = require('./migrations');

const {
  kMessageRecallWindowMs, kRecalledSystemText, kTypingStateTtlMs,
  kMaxUploadBytes, kUploadGcIntervalMs, kUploadGcGraceMs,
  kClientMessageIdMaxLength, kAllowedUploadMimeTypes,
  kExtensionToMimeMap, kMimeToExtensionMap
} = constants;

const {
  validateEmail, normalizeNickname, createVerificationCode, hashCode,
  createJwtToken, parseAuthorizationHeader, serializeUser, serializeConversation,
  serializeMessage, parseDurationToSeconds, sanitizeMessagePageLimit, sanitizeSearchLimit,
  sanitizeMessageId, sanitizeClientMessageId, parseDateLikeToEpochMs, canRecallMessageCreatedAt,
  sanitizeTypingState, sanitizeUploadFileName, normalizeFriendPairIds, isImageMimeType,
  detectFileExtension, normalizeMimeType, detectMimeTypeBySignature, isLikelyUtf8Text,
  inferUploadMimeType, isAllowedUploadMimeType, chooseStoredFileExtension, buildPublicUploadUrl,
  isAvatarUploadFileName, sanitizeStoredUploadFileName,   escapeLikePattern, sendServerError,
  paginateMessageRows
} = utils;

const uploadsDir = path.resolve(__dirname, '..', 'uploads');

const pool = mysql.createPool({
  host: process.env.MYSQL_HOST || '127.0.0.1',
  port: Number(process.env.MYSQL_PORT || 3306),
  user: process.env.MYSQL_USER || 'root',
  password: process.env.MYSQL_PASSWORD || '',
  database: process.env.MYSQL_DATABASE || 'chatroom',
  waitForConnections: true,
  connectionLimit: 10,
  namedPlaceholders: false
});

const migrations = createMigrations(pool);

async function acquireNamedLock(connection, lockName, timeoutSeconds = 5) {
  const [rows] = await connection.query(
    'SELECT GET_LOCK(?, ?) AS lock_acquired',
    [String(lockName || '').trim(), Math.max(1, Math.trunc(Number(timeoutSeconds) || 5))]
  );
  return Number(rows[0]?.lock_acquired || 0) === 1;
}

async function releaseNamedLock(connection, lockName) {
  await connection.query(
    'SELECT RELEASE_LOCK(?)',
    [String(lockName || '').trim()]
  );
}

async function ensureFriendRequestPairColumns() {
  return migrations.ensureFriendRequestPairColumns();
}

async function ensureUploadFilesTable() {
  return migrations.ensureUploadFilesTable();
}

async function ensureMessagesClientMessageIdSupport() {
  return migrations.ensureMessagesClientMessageIdSupport();
}

async function ensureUsersProfileColumns() {
  return migrations.ensureUsersProfileColumns();
}

function parseCorsOrigins(rawOrigins) {
  return String(rawOrigins || '')
    .split(',')
    .map((item) => item.trim())
    .filter((item) => item.length > 0);
}

const corsAllowlist = parseCorsOrigins(config.corsOrigins);
const corsOptions = corsAllowlist.length === 0
  ? { origin: true }
  : {
      origin(origin, callback) {
        if (!origin || corsAllowlist.includes(origin)) {
          callback(null, true);
          return;
        }
        callback(null, false);
      }
    };

const app = express();
app.use(cors(corsOptions));
app.use(express.json({ limit: '20mb' }));

const server = http.createServer(app);
const wss = new WebSocketServer({ noServer: true });
const socketsByUserId = new Map();
const typingTimerByConversationUserKey = new Map();
let uploadGcTimer = null;

function normalizeAvatarUrl(value) {
  const avatarUrl = String(value || '').trim().slice(0, 500);
  if (!avatarUrl) {
    return '';
  }

  if (avatarUrl.startsWith('/uploads/')) {
    const storedFileName = sanitizeStoredUploadFileName(avatarUrl.slice('/uploads/'.length));
    if (storedFileName && isAvatarUploadFileName(storedFileName)) {
      return buildPublicUploadUrl(storedFileName);
    }
    return null;
  }

  try {
    const parsed = new URL(avatarUrl);
    if (parsed.protocol !== 'http:' && parsed.protocol !== 'https:') {
      return null;
    }
    return parsed.toString();
  } catch {
    return null;
  }
}

async function sendVerificationEmail(email, code) {
  if (config.mailMode === 'log') {
    console.log(`[mail-log] verification code for ${email}: ${code}`);
    return;
  }

  const transport = nodemailer.createTransport({
    host: config.smtpHost,
    port: config.smtpPort,
    secure: config.smtpSecure,
    auth: {
      user: config.smtpUser,
      pass: config.smtpPass
    }
  });

  await transport.sendMail({
    from: `"${config.smtpFromName}" <${config.smtpUser}>`,
    to: email,
    subject: 'ChatRoom verification code',
    text: [
      'Your ChatRoom verification code is:',
      code,
      '',
      'The code is valid for 5 minutes.'
    ].join('\n')
  });
}

async function getUserById(userId) {
  const [rows] = await pool.query(
    'SELECT id, email, nickname, avatar_url FROM users WHERE id = ? LIMIT 1',
    [userId]
  );
  return rows[0] || null;
}

async function getUserByEmail(email, connection = pool) {
  const [rows] = await connection.query(
    'SELECT id, email, nickname, avatar_url FROM users WHERE email = ? LIMIT 1',
    [String(email || '').trim().toLowerCase()]
  );
  return rows[0] || null;
}

async function isEitherSideBlocked(userIdA, userIdB, connection = pool) {
  const left = Number(userIdA);
  const right = Number(userIdB);
  if (!left || !right || left === right) {
    return false;
  }

  const [rows] = await connection.query(
    `
      SELECT id
      FROM user_blacklist
      WHERE (owner_user_id = ? AND blocked_user_id = ?)
         OR (owner_user_id = ? AND blocked_user_id = ?)
      LIMIT 1
    `,
    [left, right, right, left]
  );
  return rows.length > 0;
}

async function areUsersFriends(userIdA, userIdB, connection = pool) {
  const pair = normalizeFriendPairIds(userIdA, userIdB);
  if (!pair) {
    return false;
  }

  const [rows] = await connection.query(
    `
      SELECT id
      FROM friendships
      WHERE user_low_id = ? AND user_high_id = ?
      LIMIT 1
    `,
    [pair.lowUserId, pair.highUserId]
  );
  return rows.length > 0;
}

async function getMembershipConversation(userId, conversationId) {
  const [rows] = await pool.query(
    `
      SELECT c.id, c.type, c.name, c.owner_user_id, c.created_at,
             cm.last_read_message_id,
             owner.email AS owner_email,
             owner.nickname AS owner_nickname
      FROM conversation_members cm
      INNER JOIN conversations c ON c.id = cm.conversation_id
      LEFT JOIN users owner ON owner.id = c.owner_user_id
      WHERE cm.user_id = ? AND cm.conversation_id = ?
      LIMIT 1
    `,
    [userId, conversationId]
  );
  return rows[0] || null;
}

async function loadSingleConversationPeerUserId(conversationId, currentUserId, connection = pool) {
  const [rows] = await connection.query(
    `
      SELECT cm.user_id
      FROM conversations c
      INNER JOIN conversation_members cm ON cm.conversation_id = c.id
      WHERE c.id = ?
        AND c.type = 'single'
        AND cm.user_id <> ?
      LIMIT 1
    `,
    [conversationId, currentUserId]
  );

  const peerUserId = Number(rows[0]?.user_id || 0);
  return peerUserId > 0 ? peerUserId : 0;
}

async function isSingleConversationBlockedForUser(conversation, currentUserId, connection = pool) {
  if (!conversation || String(conversation.type || '').trim().toLowerCase() !== 'single') {
    return false;
  }

  const peerUserId = await loadSingleConversationPeerUserId(conversation.id, currentUserId, connection);
  if (!peerUserId) {
    return false;
  }

  return isEitherSideBlocked(currentUserId, peerUserId, connection);
}

async function canUserAccessUploadFile(userId, storedFileName, connection = pool) {
  const normalizedFileName = sanitizeStoredUploadFileName(storedFileName);
  if (!normalizedFileName) {
    return false;
  }

  const targetUrl = buildPublicUploadUrl(normalizedFileName);
  if (isAvatarUploadFileName(normalizedFileName)) {
    const [avatarRows] = await connection.query(
      'SELECT id FROM users WHERE avatar_url = ? LIMIT 1',
      [targetUrl]
    );
    if (avatarRows.length > 0) {
      return true;
    }
  }

  const [rows] = await connection.query(
    `
      SELECT uf.id
      FROM upload_files uf
      INNER JOIN conversation_members cm ON cm.conversation_id = uf.conversation_id
      WHERE uf.stored_file_name = ?
        AND cm.user_id = ?
      LIMIT 1
    `,
    [normalizedFileName, userId]
  );
  if (rows.length > 0) {
    return true;
  }

  // Backward compatibility for historical media messages created before upload_files existed.
  const likePattern = `%\n${escapeLikePattern(targetUrl)}`;
  const [legacyRows] = await connection.query(
    `
      SELECT m.id
      FROM conversation_members cm
      INNER JOIN messages m ON m.conversation_id = cm.conversation_id
      WHERE cm.user_id = ?
        AND m.content LIKE ? ESCAPE '\\'
      LIMIT 1
    `,
    [userId, likePattern]
  );

  return legacyRows.length > 0;
}

async function registerUploadFileMetadata(connection, payload) {
  const storedFileName = sanitizeStoredUploadFileName(payload?.storedFileName);
  if (!storedFileName) {
    throw new Error('Invalid stored file name.');
  }
  const conversationId = String(payload?.conversationId || '').trim();
  const originalFileName = sanitizeUploadFileName(payload?.originalFileName);
  const mimeType = String(payload?.mimeType || 'application/octet-stream').trim().toLowerCase();
  const messageId = sanitizeMessageId(payload?.messageId, 0);
  const uploadedByUserId = sanitizeMessageId(payload?.uploadedByUserId, 0);
  const fileSize = Math.max(0, Math.trunc(Number(payload?.fileSize) || 0));
  if (!conversationId || uploadedByUserId <= 0 || fileSize <= 0) {
    throw new Error('Invalid upload metadata payload.');
  }

  await connection.query(
    `
      INSERT INTO upload_files (
        stored_file_name, original_file_name, mime_type, file_size, conversation_id, message_id, uploaded_by_user_id
      ) VALUES (?, ?, ?, ?, ?, ?, ?)
    `,
    [storedFileName, originalFileName, mimeType, fileSize, conversationId, messageId || null, uploadedByUserId]
  );
}

async function deleteUploadPhysicalFileIfUnreferenced(storedFileName) {
  const normalizedFileName = sanitizeStoredUploadFileName(storedFileName);
  if (!normalizedFileName) {
    return;
  }

  const [rows] = await pool.query(
    'SELECT COUNT(1) AS ref_count FROM upload_files WHERE stored_file_name = ?',
    [normalizedFileName]
  );
  const refCount = Number(rows[0]?.ref_count || 0);
  if (refCount > 0) {
    return;
  }

  const absolutePath = path.join(uploadsDir, normalizedFileName);
  try {
    await fs.promises.unlink(absolutePath);
  } catch (error) {
    if (error && error.code === 'ENOENT') {
      return;
    }
    console.error('Failed to delete unreferenced upload file:', normalizedFileName, error);
  }
}

async function detachUploadFilesByMessageId(messageId) {
  const normalizedMessageId = sanitizeMessageId(messageId, 0);
  if (normalizedMessageId <= 0) {
    return;
  }

  const [rows] = await pool.query(
    'SELECT stored_file_name FROM upload_files WHERE message_id = ?',
    [normalizedMessageId]
  );
  if (rows.length <= 0) {
    return;
  }

  await pool.query(
    'DELETE FROM upload_files WHERE message_id = ?',
    [normalizedMessageId]
  );

  for (const row of rows) {
    await deleteUploadPhysicalFileIfUnreferenced(row.stored_file_name);
  }
}

async function cleanupOrphanUploadFiles(reason = 'scheduled') {
  try {
    await fs.promises.mkdir(uploadsDir, { recursive: true });
    const entries = await fs.promises.readdir(uploadsDir, { withFileTypes: true });
    if (!entries || entries.length <= 0) {
      return;
    }

    const [refRows] = await pool.query('SELECT stored_file_name FROM upload_files');
    const referencedNames = new Set(
      refRows
        .map((row) => sanitizeStoredUploadFileName(row.stored_file_name))
        .filter((name) => name.length > 0)
    );

    const nowMs = Date.now();
    for (const entry of entries) {
      if (!entry.isFile()) {
        continue;
      }

      const fileName = sanitizeStoredUploadFileName(entry.name);
      if (!fileName || referencedNames.has(fileName) || isAvatarUploadFileName(fileName)) {
        continue;
      }

      const absolutePath = path.join(uploadsDir, fileName);
      let stats = null;
      try {
        stats = await fs.promises.stat(absolutePath);
      } catch (error) {
        continue;
      }
      const ageMs = nowMs - Number(stats.mtimeMs || nowMs);
      if (ageMs < kUploadGcGraceMs) {
        continue;
      }

      try {
        await fs.promises.unlink(absolutePath);
      } catch (error) {
        if (!error || error.code !== 'ENOENT') {
          console.error('Failed to cleanup orphan upload file:', fileName, error);
        }
      }
    }
  } catch (error) {
    console.error(`Upload GC failed (${reason}):`, error);
  }
}

async function loadOnlineCountsForConversationIds(conversationIds) {
  const normalizedIds = Array.isArray(conversationIds)
    ? conversationIds.map((id) => String(id || '').trim()).filter((id) => id.length > 0)
    : [];
  if (normalizedIds.length === 0) {
    return new Map();
  }

  const placeholders = normalizedIds.map(() => '?').join(', ');
  const [rows] = await pool.query(
    `
      SELECT conversation_id, user_id
      FROM conversation_members
      WHERE conversation_id IN (${placeholders})
    `,
    normalizedIds
  );

  const onlineCounts = new Map();
  for (const row of rows) {
    const conversationId = String(row.conversation_id || '').trim();
    const userId = Number(row.user_id);
    if (!conversationId || !userId || !socketsByUserId.has(userId)) {
      continue;
    }
    onlineCounts.set(conversationId, (onlineCounts.get(conversationId) || 0) + 1);
  }
  return onlineCounts;
}

async function createDefaultConversationForUser(connection, user) {
  const conversationId = crypto.randomUUID();
  await connection.query(
    'INSERT INTO conversations (id, type, name, owner_user_id) VALUES (?, ?, ?, ?)',
    [conversationId, 'group', `${user.nickname}'s Space`, user.id]
  );
  await connection.query(
    'INSERT INTO conversation_members (conversation_id, user_id) VALUES (?, ?)',
    [conversationId, user.id]
  );
  await connection.query(
    'INSERT INTO messages (conversation_id, sender_id, message_type, content) VALUES (?, ?, ?, ?)',
    [conversationId, user.id, 'system', 'Welcome to ChatRoom. This is your first conversation.']
  );
}

async function createSessionForUser(connection, userId) {
  const tokenJti = crypto.randomUUID();
  const expiresAt = new Date(Date.now() + parseDurationToSeconds(config.jwtExpiresIn) * 1000);
  await connection.query(
    'INSERT INTO user_sessions (user_id, token_jti, expires_at) VALUES (?, ?, ?)',
    [userId, tokenJti, expiresAt]
  );
  return { tokenJti, expiresAt };
}

async function authMiddleware(req, res, next) {
  try {
    const token = parseAuthorizationHeader(req.headers.authorization);
    if (!token) {
      return res.status(401).json({ message: 'Missing bearer token.' });
    }

    const payload = jwt.verify(token, config.jwtSecret);
    const [rows] = await pool.query(
      `
        SELECT s.id, s.user_id, s.token_jti, s.expires_at, s.revoked_at,
               u.email, u.nickname, u.avatar_url
        FROM user_sessions s
        INNER JOIN users u ON u.id = s.user_id
        WHERE s.token_jti = ? AND s.user_id = ? AND s.revoked_at IS NULL AND s.expires_at > NOW()
        LIMIT 1
      `,
      [payload.jti, payload.sub]
    );

    const session = rows[0];
    if (!session) {
      return res.status(401).json({ message: 'Session expired or revoked.' });
    }

    await pool.query(
      'UPDATE user_sessions SET last_seen_at = NOW() WHERE id = ?',
      [session.id]
    );

    req.auth = {
      user: {
        id: Number(session.user_id),
        email: session.email,
        nickname: session.nickname,
        avatarUrl: session.avatar_url || ''
      },
      tokenJti: session.token_jti
    };
    next();
  } catch (error) {
    return res.status(401).json({ message: 'Invalid token.' });
  }
}

app.get('/api/health', async (req, res) => {
  const [rows] = await pool.query('SELECT NOW() AS server_time');
  res.json({
    ok: true,
    serverTime: rows[0].server_time,
    mailMode: config.mailMode
  });
});

app.post('/api/auth/request-code', async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  if (!validateEmail(email)) {
    return res.status(400).json({ message: 'Invalid email address.' });
  }

  const [recentRows] = await pool.query(
    `
      SELECT id, sent_at
      FROM login_codes
      WHERE email = ? AND sent_at > (NOW() - INTERVAL 60 SECOND)
      ORDER BY id DESC
      LIMIT 1
    `,
    [email]
  );

  if (recentRows.length > 0) {
    return res.status(429).json({ message: 'Please wait 60 seconds before requesting another code.' });
  }

  const code = createVerificationCode();

  try {
    await sendVerificationEmail(email, code);
  } catch (error) {
    return sendServerError(res, 'Failed to send email verification code.', error);
  }

  await pool.query(
    `
      INSERT INTO login_codes (email, code_hash, expires_at, ip_address)
      VALUES (?, ?, DATE_ADD(NOW(), INTERVAL 5 MINUTE), ?)
    `,
    [email, hashCode(email, code), req.ip]
  );

  const logMode = config.mailMode === 'log';
  return res.json({
    ok: true,
    message: logMode
      ? `开发验证码：${code}（已同时打印到后端控制台）`
      : 'Verification code sent successfully.',
    verificationCode: logMode ? code : undefined
  });
});

app.post('/api/auth/verify-code', async (req, res) => {
  const email = String(req.body?.email || '').trim().toLowerCase();
  const code = String(req.body?.code || '').trim();

  if (!validateEmail(email)) {
    return res.status(400).json({ message: 'Invalid email address.' });
  }
  if (!/^\d{6}$/.test(code)) {
    return res.status(400).json({ message: 'Verification code must be 6 digits.' });
  }

  const [codeRows] = await pool.query(
    `
      SELECT id, code_hash, attempts
      FROM login_codes
      WHERE email = ? AND used_at IS NULL AND expires_at > NOW()
      ORDER BY id DESC
      LIMIT 1
    `,
    [email]
  );

  const latestCode = codeRows[0];
  if (!latestCode) {
    return res.status(400).json({ message: 'No valid verification code found. Please request a new one.' });
  }

  if (latestCode.attempts >= 5) {
    return res.status(400).json({ message: 'Too many failed attempts. Please request a new verification code.' });
  }

  if (latestCode.code_hash !== hashCode(email, code)) {
    await pool.query(
      'UPDATE login_codes SET attempts = attempts + 1 WHERE id = ?',
      [latestCode.id]
    );
    return res.status(400).json({ message: 'Incorrect verification code.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    await connection.query(
      'UPDATE login_codes SET used_at = NOW() WHERE id = ?',
      [latestCode.id]
    );

    const [userRows] = await connection.query(
      'SELECT id, email, nickname, avatar_url FROM users WHERE email = ? LIMIT 1',
      [email]
    );

    let user = userRows[0] || null;
    let isNewUser = false;

    if (!user) {
      isNewUser = true;
      const nickname = email.split('@')[0].slice(0, 20) || `user_${Date.now()}`;
      const [insertResult] = await connection.query(
        'INSERT INTO users (email, nickname) VALUES (?, ?)',
        [email, nickname]
      );
      user = {
        id: insertResult.insertId,
        email,
        nickname,
        avatar_url: null
      };
      await createDefaultConversationForUser(connection, user);
    }

    const session = await createSessionForUser(connection, user.id);
    await connection.commit();

    const token = createJwtToken(user, session.tokenJti);
    return res.json({
      ok: true,
      token,
      expiresAt: session.expiresAt,
      isNewUser,
      user: serializeUser(user)
    });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to verify code.', error);
  } finally {
    connection.release();
  }
});

app.get('/api/me', authMiddleware, async (req, res) => {
  res.json({ user: req.auth.user });
});

app.post('/api/me/profile', authMiddleware, async (req, res) => {
  const nickname = normalizeNickname(req.body?.nickname);
  const avatarUrl = normalizeAvatarUrl(req.body?.avatarUrl);
  if (nickname.length < 1) {
    return res.status(400).json({ message: '昵称不能为空。' });
  }
  if (avatarUrl === null) {
    return res.status(400).json({ message: '头像链接必须是 http 或 https 地址，或点击“选择图片”上传本地图片。' });
  }

  await pool.query(
    'UPDATE users SET nickname = ?, avatar_url = ? WHERE id = ? LIMIT 1',
    [nickname, avatarUrl || null, req.auth.user.id]
  );

  const user = {
    ...req.auth.user,
    nickname,
    avatarUrl
  };

  const sockets = socketsByUserId.get(req.auth.user.id);
  if (sockets) {
    for (const ws of sockets) {
      if (ws.auth && ws.auth.user) {
        ws.auth.user.nickname = nickname;
        ws.auth.user.avatarUrl = avatarUrl;
      }
      socketSend(ws, {
        type: 'profile_updated',
        user
      });
    }
  }

  res.json({
    ok: true,
    user,
    message: 'Profile updated successfully.'
  });
});

app.post('/api/me/avatar', authMiddleware, async (req, res) => {
  let uploadPayload = null;
  try {
    uploadPayload = await extractUploadPayloadFromRequest(req);
  } catch (error) {
    return res.status(400).json({ message: error.message || '头像上传数据无效。' });
  }

  const fileName = sanitizeUploadFileName(uploadPayload.fileName || 'avatar.jpg');
  const uploadedMimeType = String(uploadPayload.mimeType || '').trim().toLowerCase();
  const buffer = uploadPayload.buffer;
  if (!buffer || buffer.length <= 0) {
    return res.status(400).json({ message: '头像图片为空。' });
  }
  if (buffer.length > kMaxUploadBytes) {
    return res.status(400).json({ message: `头像图片过大，当前仅支持不超过 ${kMaxUploadBytes} 字节。` });
  }

  const mimeType = inferUploadMimeType(buffer, fileName, uploadedMimeType);
  if (!isImageMimeType(mimeType) || !isAllowedUploadMimeType(mimeType)) {
    return res.status(400).json({ message: `不支持的头像图片类型：${mimeType || 'unknown'}。` });
  }

  const extension = chooseStoredFileExtension(fileName, mimeType) || '.jpg';
  const storedFileName = `avatar_${req.auth.user.id}_${Date.now()}_${crypto.randomUUID()}${extension}`;
  const absolutePath = path.join(uploadsDir, storedFileName);
  const avatarUrl = buildPublicUploadUrl(storedFileName);

  try {
    await fs.promises.mkdir(uploadsDir, { recursive: true });
    await fs.promises.writeFile(absolutePath, buffer);
    await pool.query(
      'UPDATE users SET avatar_url = ? WHERE id = ? LIMIT 1',
      [avatarUrl, req.auth.user.id]
    );
  } catch (error) {
    return sendServerError(res, '头像保存失败。', error);
  }

  const user = {
    ...req.auth.user,
    avatarUrl
  };

  const sockets = socketsByUserId.get(req.auth.user.id);
  if (sockets) {
    for (const ws of sockets) {
      if (ws.auth && ws.auth.user) {
        ws.auth.user.avatarUrl = avatarUrl;
      }
      socketSend(ws, {
        type: 'profile_updated',
        user
      });
    }
  }

  return res.json({
    ok: true,
    user,
    avatar: {
      url: avatarUrl,
      mimeType,
      size: buffer.length
    },
    message: '头像已更新。'
  });
});

app.get('/uploads/:fileName', authMiddleware, async (req, res) => {
  const fileName = sanitizeStoredUploadFileName(req.params.fileName);
  if (!fileName) {
    return res.status(404).json({ message: 'File not found.' });
  }

  const canAccess = await canUserAccessUploadFile(req.auth.user.id, fileName);
  if (!canAccess) {
    return res.status(404).json({ message: 'File not found.' });
  }

  const absolutePath = path.join(uploadsDir, fileName);
  try {
    await fs.promises.access(absolutePath, fs.constants.R_OK);
  } catch (error) {
    return res.status(404).json({ message: 'File not found.' });
  }

  return res.sendFile(absolutePath, {
    headers: {
      'Cache-Control': 'private, max-age=300'
    }
  });
});

app.get('/api/friends', authMiddleware, async (req, res) => {
  const [rows] = await pool.query(
    `
      SELECT
        f.id AS friendship_id,
        f.created_at AS became_friends_at,
        u.id,
        u.email,
        u.nickname,
        u.avatar_url
      FROM friendships f
      INNER JOIN users u
        ON u.id = CASE
          WHEN f.user_low_id = ? THEN f.user_high_id
          ELSE f.user_low_id
        END
      WHERE f.user_low_id = ? OR f.user_high_id = ?
      ORDER BY f.created_at DESC, f.id DESC
    `,
    [req.auth.user.id, req.auth.user.id, req.auth.user.id]
  );

  res.json({
    friends: rows.map((row) => ({
      user: serializeUser(row),
      becameFriendsAt: row.became_friends_at
    }))
  });
});

app.get('/api/friend-requests', authMiddleware, async (req, res) => {
  const [rows] = await pool.query(
    `
      SELECT
        fr.id,
        fr.requester_user_id,
        fr.addressee_user_id,
        fr.status,
        fr.created_at,
        fr.handled_at,
        requester.id AS requester_id,
        requester.email AS requester_email,
        requester.nickname AS requester_nickname,
        requester.avatar_url AS requester_avatar_url,
        addressee.id AS addressee_id,
        addressee.email AS addressee_email,
        addressee.nickname AS addressee_nickname,
        addressee.avatar_url AS addressee_avatar_url
      FROM friend_requests fr
      INNER JOIN users requester ON requester.id = fr.requester_user_id
      INNER JOIN users addressee ON addressee.id = fr.addressee_user_id
      WHERE (fr.requester_user_id = ? OR fr.addressee_user_id = ?)
        AND fr.status = 'pending'
      ORDER BY fr.id DESC
      LIMIT 200
    `,
    [req.auth.user.id, req.auth.user.id]
  );

  res.json({
    requests: rows.map((row) => ({
      id: Number(row.id),
      status: row.status,
      createdAt: row.created_at,
      handledAt: row.handled_at,
      direction: Number(row.requester_user_id) === req.auth.user.id ? 'outgoing' : 'incoming',
      requester: serializeUser({
        id: row.requester_id,
        email: row.requester_email,
        nickname: row.requester_nickname,
        avatar_url: row.requester_avatar_url
      }),
      addressee: serializeUser({
        id: row.addressee_id,
        email: row.addressee_email,
        nickname: row.addressee_nickname,
        avatar_url: row.addressee_avatar_url
      })
    }))
  });
});

app.post('/api/friends/request', authMiddleware, async (req, res) => {
  const peerEmail = String(req.body?.peerEmail || '').trim().toLowerCase();
  if (!validateEmail(peerEmail)) {
    return res.status(400).json({ message: 'Invalid peer email address.' });
  }
  if (peerEmail === req.auth.user.email.toLowerCase()) {
    return res.status(400).json({ message: 'Cannot add yourself as a friend.' });
  }

  const connection = await pool.getConnection();
  let lockName = '';
  let lockAcquired = false;
  try {
    await connection.beginTransaction();

    const peer = await getUserByEmail(peerEmail, connection);
    if (!peer) {
      await connection.rollback();
      return res.status(404).json({ message: 'The target user has not registered yet.' });
    }

    if (await isEitherSideBlocked(req.auth.user.id, peer.id, connection)) {
      await connection.rollback();
      return res.status(403).json({ message: 'Cannot send friend request due to blacklist settings.' });
    }

    if (await areUsersFriends(req.auth.user.id, peer.id, connection)) {
      await connection.rollback();
      return res.json({ ok: true, alreadyFriends: true, peer: serializeUser(peer) });
    }

    const pair = normalizeFriendPairIds(req.auth.user.id, peer.id);
    if (!pair) {
      await connection.rollback();
      return res.status(400).json({ message: 'Invalid friend pair.' });
    }

    lockName = `friend_req_pair_${pair.lowUserId}_${pair.highUserId}`;
    lockAcquired = await acquireNamedLock(connection, lockName, 5);
    if (!lockAcquired) {
      await connection.rollback();
      return res.status(503).json({ message: 'Friend request is busy. Please retry shortly.' });
    }

    const [pendingRows] = await connection.query(
      `
        SELECT id, requester_user_id, addressee_user_id
        FROM friend_requests
        WHERE status = 'pending'
          AND user_low_id = ?
          AND user_high_id = ?
        ORDER BY id DESC
        LIMIT 1
        FOR UPDATE
      `,
      [pair.lowUserId, pair.highUserId]
    );

    const pendingRequest = pendingRows[0] || null;
    if (pendingRequest) {
      if (Number(pendingRequest.requester_user_id) === req.auth.user.id) {
        await connection.rollback();
        return res.status(409).json({ message: 'Friend request has already been sent.' });
      }

      await connection.query(
        `
          UPDATE friend_requests
          SET status = 'accepted', handled_at = NOW()
          WHERE id = ?
        `,
        [pendingRequest.id]
      );
      await connection.query(
        `
          INSERT IGNORE INTO friendships (user_low_id, user_high_id)
          VALUES (?, ?)
        `,
        [pair.lowUserId, pair.highUserId]
      );
      await connection.commit();
      return res.json({
        ok: true,
        autoAccepted: true,
        peer: serializeUser(peer)
      });
    }

    const [insertResult] = await connection.query(
      `
        INSERT INTO friend_requests (
          requester_user_id, addressee_user_id, user_low_id, user_high_id, status
        )
        VALUES (?, ?, ?, ?, 'pending')
      `,
      [req.auth.user.id, peer.id, pair.lowUserId, pair.highUserId]
    );

    await connection.commit();
    return res.json({
      ok: true,
      requestId: Number(insertResult.insertId),
      peer: serializeUser(peer)
    });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to send friend request.', error);
  } finally {
    if (lockAcquired && lockName) {
      try {
        await releaseNamedLock(connection, lockName);
      } catch (error) {
        console.error('Failed to release friend request lock:', error);
      }
    }
    connection.release();
  }
});

app.post('/api/friend-requests/:requestId/accept', authMiddleware, async (req, res) => {
  const requestId = sanitizeMessageId(req.params.requestId, 0);
  if (requestId <= 0) {
    return res.status(400).json({ message: 'Invalid request id.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const [requestRows] = await connection.query(
      `
        SELECT id, requester_user_id, addressee_user_id, status
        FROM friend_requests
        WHERE id = ?
        LIMIT 1
        FOR UPDATE
      `,
      [requestId]
    );
    const requestRow = requestRows[0];
    if (!requestRow) {
      await connection.rollback();
      return res.status(404).json({ message: 'Friend request not found.' });
    }
    if (Number(requestRow.addressee_user_id) !== req.auth.user.id) {
      await connection.rollback();
      return res.status(403).json({ message: 'You can only accept requests sent to you.' });
    }
    if (requestRow.status !== 'pending') {
      await connection.rollback();
      return res.status(400).json({ message: 'This friend request has already been handled.' });
    }

    if (await isEitherSideBlocked(requestRow.requester_user_id, requestRow.addressee_user_id, connection)) {
      await connection.rollback();
      return res.status(403).json({ message: 'Cannot accept request due to blacklist settings.' });
    }

    const pair = normalizeFriendPairIds(requestRow.requester_user_id, requestRow.addressee_user_id);
    await connection.query(
      `
        UPDATE friend_requests
        SET status = 'accepted', handled_at = NOW()
        WHERE id = ?
      `,
      [requestId]
    );
    await connection.query(
      `
        UPDATE friend_requests
        SET status = 'canceled', handled_at = NOW()
        WHERE status = 'pending'
          AND id <> ?
          AND user_low_id = ?
          AND user_high_id = ?
      `,
      [requestId, pair.lowUserId, pair.highUserId]
    );
    await connection.query(
      `
        INSERT IGNORE INTO friendships (user_low_id, user_high_id)
        VALUES (?, ?)
      `,
      [pair.lowUserId, pair.highUserId]
    );

    await connection.commit();
    return res.json({ ok: true, requestId });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to accept friend request.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/friend-requests/:requestId/reject', authMiddleware, async (req, res) => {
  const requestId = sanitizeMessageId(req.params.requestId, 0);
  if (requestId <= 0) {
    return res.status(400).json({ message: 'Invalid request id.' });
  }

  const [result] = await pool.query(
    `
      UPDATE friend_requests
      SET status = 'rejected', handled_at = NOW()
      WHERE id = ?
        AND addressee_user_id = ?
        AND status = 'pending'
    `,
    [requestId, req.auth.user.id]
  );

  if (Number(result.affectedRows || 0) <= 0) {
    return res.status(404).json({ message: 'Pending friend request not found.' });
  }

  return res.json({ ok: true, requestId });
});

app.get('/api/blacklist', authMiddleware, async (req, res) => {
  const [rows] = await pool.query(
    `
      SELECT
        b.id AS blacklist_id,
        b.created_at AS blocked_at,
        u.id,
        u.email,
        u.nickname,
        u.avatar_url
      FROM user_blacklist b
      INNER JOIN users u ON u.id = b.blocked_user_id
      WHERE b.owner_user_id = ?
      ORDER BY b.id DESC
    `,
    [req.auth.user.id]
  );

  res.json({
    blockedUsers: rows.map((row) => ({
      user: serializeUser(row),
      blockedAt: row.blocked_at
    }))
  });
});

app.post('/api/blacklist', authMiddleware, async (req, res) => {
  const peerEmail = String(req.body?.peerEmail || '').trim().toLowerCase();
  if (!validateEmail(peerEmail)) {
    return res.status(400).json({ message: 'Invalid peer email address.' });
  }
  if (peerEmail === req.auth.user.email.toLowerCase()) {
    return res.status(400).json({ message: 'Cannot block yourself.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const peer = await getUserByEmail(peerEmail, connection);
    if (!peer) {
      await connection.rollback();
      return res.status(404).json({ message: 'The target user has not registered yet.' });
    }

    await connection.query(
      `
        INSERT IGNORE INTO user_blacklist (owner_user_id, blocked_user_id)
        VALUES (?, ?)
      `,
      [req.auth.user.id, peer.id]
    );

    const pair = normalizeFriendPairIds(req.auth.user.id, peer.id);
    if (pair) {
      await connection.query(
        `
          DELETE FROM friendships
          WHERE user_low_id = ? AND user_high_id = ?
        `,
        [pair.lowUserId, pair.highUserId]
      );
    }

    await connection.query(
      `
        UPDATE friend_requests
        SET status = 'canceled', handled_at = NOW()
        WHERE status = 'pending'
          AND (
            (requester_user_id = ? AND addressee_user_id = ?)
            OR
            (requester_user_id = ? AND addressee_user_id = ?)
          )
      `,
      [req.auth.user.id, peer.id, peer.id, req.auth.user.id]
    );

    await connection.commit();
    return res.json({ ok: true, blockedUser: serializeUser(peer) });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to add blacklist record.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/blacklist/:blockedUserId/remove', authMiddleware, async (req, res) => {
  const blockedUserId = sanitizeMessageId(req.params.blockedUserId, 0);
  if (blockedUserId <= 0) {
    return res.status(400).json({ message: 'Invalid blocked user id.' });
  }
  if (blockedUserId === req.auth.user.id) {
    return res.status(400).json({ message: 'Cannot unblock yourself.' });
  }

  const [result] = await pool.query(
    `
      DELETE FROM user_blacklist
      WHERE owner_user_id = ? AND blocked_user_id = ?
    `,
    [req.auth.user.id, blockedUserId]
  );

  return res.json({
    ok: true,
    removed: Number(result.affectedRows || 0) > 0
  });
});

app.get('/api/search/messages', authMiddleware, async (req, res) => {
  const keyword = String(req.query.q || '').trim();
  if (keyword.length < 1) {
    return res.status(400).json({ message: 'Search keyword is required.' });
  }

  const limit = sanitizeSearchLimit(req.query.limit);
  const likePattern = `%${keyword}%`;
  const [rows] = await pool.query(
    `
      SELECT
        m.id,
        m.conversation_id,
        c.name AS conversation_name,
        m.sender_id,
        u.email AS sender_email,
        u.nickname AS sender_nickname,
        u.avatar_url AS sender_avatar_url,
        m.message_type,
        m.content,
        m.created_at
      FROM conversation_members cm
      INNER JOIN messages m ON m.conversation_id = cm.conversation_id
      INNER JOIN conversations c ON c.id = m.conversation_id
      INNER JOIN users u ON u.id = m.sender_id
      WHERE cm.user_id = ?
        AND m.content LIKE ?
      ORDER BY m.id DESC
      LIMIT ?
    `,
    [req.auth.user.id, likePattern, limit]
  );

  res.json({
    keyword,
    results: rows.map((row) => ({
      message: serializeMessage(row),
      conversationId: row.conversation_id,
      conversationName: row.conversation_name || row.conversation_id
    }))
  });
});

app.get('/api/conversations', authMiddleware, async (req, res) => {
  const [rows] = await pool.query(
    `
      SELECT
        c.id,
        c.type,
        c.name,
        c.owner_user_id,
        owner.email AS owner_email,
        owner.nickname AS owner_nickname,
        c.created_at,
        COUNT(cm_all.user_id) AS member_count,
        (
          SELECT COUNT(1)
          FROM messages mu
          WHERE mu.conversation_id = c.id
            AND mu.id > COALESCE(cm.last_read_message_id, 0)
            AND mu.sender_id <> ?
        ) AS unread_count,
        last_message.id AS last_message_id,
        last_message.message_type AS last_message_type,
        last_message.content AS last_message_content,
        last_message.sender_id AS last_sender_id,
        last_sender.email AS last_sender_email,
        last_message.created_at AS last_message_at
      FROM conversation_members cm
      INNER JOIN conversations c ON c.id = cm.conversation_id
      LEFT JOIN users owner ON owner.id = c.owner_user_id
      LEFT JOIN conversation_members cm_all ON cm_all.conversation_id = c.id
      LEFT JOIN messages last_message
        ON last_message.id = (
          SELECT m2.id
          FROM messages m2
          WHERE m2.conversation_id = c.id
          ORDER BY m2.id DESC
          LIMIT 1
        )
      LEFT JOIN users last_sender ON last_sender.id = last_message.sender_id
      WHERE cm.user_id = ?
      GROUP BY
        c.id, c.type, c.name, c.created_at,
        last_message.id, last_message.message_type, last_message.content, last_message.sender_id,
        last_sender.email, last_message.created_at
      ORDER BY COALESCE(last_message.created_at, c.created_at) DESC
    `,
    [req.auth.user.id, req.auth.user.id]
  );

  const conversationIds = rows.map((row) => String(row.id || '').trim()).filter((id) => id.length > 0);
  const onlineCounts = await loadOnlineCountsForConversationIds(conversationIds);

  res.json({
    conversations: rows.map((row) => serializeConversation({
      ...row,
      online_count: onlineCounts.get(String(row.id || '').trim()) || 0
    }))
  });
});

app.get('/api/conversations/:conversationId/messages', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const limit = sanitizeMessagePageLimit(req.query.limit);
  const beforeId = sanitizeMessageId(req.query.beforeId, 0);

  const conversation = await getMembershipConversation(req.auth.user.id, conversationId);
  if (!conversation) {
    return res.status(404).json({ message: 'Conversation not found.' });
  }

  const params = [conversationId];
  let sql = `
    SELECT
      m.id,
      m.conversation_id,
      m.sender_id,
      m.client_message_id,
      u.email AS sender_email,
      u.nickname AS sender_nickname,
      u.avatar_url AS sender_avatar_url,
      m.message_type,
      m.content,
      m.created_at
    FROM messages m
    INNER JOIN users u ON u.id = m.sender_id
    WHERE m.conversation_id = ?
  `;

  if (beforeId > 0) {
    sql += ' AND m.id < ?';
    params.push(beforeId);
  }

  sql += ' ORDER BY m.id DESC LIMIT ?';
  params.push(limit + 1);

  const [rows] = await pool.query(sql, params);
  const page = paginateMessageRows(rows, limit);
  const peerLastReadMessageId = await loadPeerLastReadMessageId(req.auth.user.id, conversationId);
  res.json({
    conversation: serializeConversation({
      ...conversation,
      member_count: 0,
      last_message_id: null
    }),
    messages: page.rows.map(serializeMessage),
    readState: {
      lastReadMessageId: sanitizeMessageId(conversation.last_read_message_id, 0),
      peerLastReadMessageId
    },
    pagination: {
      hasMore: page.hasMore,
      nextBeforeId: page.nextBeforeId
    }
  });
});

app.post('/api/conversations/:conversationId/media', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const conversation = await getMembershipConversation(req.auth.user.id, conversationId);
  if (!conversation) {
    return res.status(404).json({ message: 'Conversation not found.' });
  }
  if (await isSingleConversationBlockedForUser(conversation, req.auth.user.id)) {
    return res.status(403).json({ message: 'Message sending is blocked by blacklist settings.' });
  }

  let uploadPayload = null;
  try {
    uploadPayload = await extractUploadPayloadFromRequest(req);
  } catch (error) {
    return res.status(400).json({ message: error.message || 'Invalid upload payload.' });
  }

  const fileName = sanitizeUploadFileName(uploadPayload.fileName);
  const uploadedMimeType = String(uploadPayload.mimeType || '').trim().toLowerCase();
  const clientMessageId = String(uploadPayload.clientMessageId || '').trim();
  const buffer = uploadPayload.buffer;
  if (!buffer || buffer.length <= 0) {
    return res.status(400).json({ message: 'Uploaded file is empty.' });
  }
  if (buffer.length > kMaxUploadBytes) {
    return res.status(400).json({ message: `File too large. Maximum size is ${kMaxUploadBytes} bytes.` });
  }
  const mimeType = inferUploadMimeType(buffer, fileName, uploadedMimeType);
  if (!isAllowedUploadMimeType(mimeType)) {
    return res.status(400).json({
      message: `Unsupported file type: ${mimeType || 'unknown'}.`
    });
  }

  const extension = chooseStoredFileExtension(fileName, mimeType);
  const storedFileName = `${Date.now()}_${crypto.randomUUID()}${extension}`;
  const absolutePath = path.join(uploadsDir, storedFileName);
  const publicUrl = buildPublicUploadUrl(storedFileName);
  const mediaPrefix = isImageMimeType(mimeType) ? '[图片]' : '[文件]';
  const messageContent = [
    `${mediaPrefix} ${fileName}`,
    publicUrl,
    `size: ${buffer.length}`,
    `mime: ${mimeType}`
  ].join('\n');

  if (clientMessageId) {
    const existingMessage = await loadMessageByClientMessageId(
      req.auth.user.id,
      conversationId,
      clientMessageId
    );
    if (existingMessage) {
      return res.json({
        ok: true,
        deduplicated: true,
        conversationId,
        message: serializeMessage(existingMessage)
      });
    }
  }

  try {
    await fs.promises.mkdir(uploadsDir, { recursive: true });
    await fs.promises.writeFile(absolutePath, buffer);
  } catch (error) {
    return sendServerError(res, 'Failed to store uploaded file.', error);
  }

  const connection = await pool.getConnection();
  let row = null;
  try {
    await connection.beginTransaction();
    const persistResult = await persistMessage(
      req.auth.user.id,
      conversationId,
      'text',
      messageContent,
      connection,
      clientMessageId
    );
    row = persistResult.row;
    if (!row) {
      await connection.rollback();
      return res.status(500).json({ message: 'Failed to create media message.' });
    }
    if (persistResult.deduplicated) {
      await connection.rollback();
      try {
        await fs.promises.unlink(absolutePath);
      } catch (unlinkError) {
        if (!unlinkError || unlinkError.code !== 'ENOENT') {
          console.error('Failed to cleanup duplicate upload file:', unlinkError);
        }
      }
      return res.json({
        ok: true,
        deduplicated: true,
        conversationId,
        message: serializeMessage(row)
      });
    }

    await registerUploadFileMetadata(connection, {
      storedFileName,
      originalFileName: fileName,
      mimeType: mimeType || 'application/octet-stream',
      fileSize: buffer.length,
      conversationId,
      messageId: row.id,
      uploadedByUserId: req.auth.user.id
    });
    await connection.commit();
  } catch (error) {
    await connection.rollback();
    try {
      await fs.promises.unlink(absolutePath);
    } catch (unlinkError) {
      console.error('Failed to cleanup orphan upload file:', unlinkError);
    }
    return sendServerError(res, 'Failed to create media message.', error);
  } finally {
    connection.release();
  }

  await broadcastMessageToConversation(conversationId, {
    type: 'message_created',
    conversationId,
    clientMessageId,
    message: serializeMessage(row)
  });

  return res.json({
    ok: true,
    conversationId,
    file: {
      name: fileName,
      mimeType,
      size: buffer.length,
      url: publicUrl
    },
    message: serializeMessage(row)
  });
});

app.post('/api/conversations/:conversationId/read', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const conversation = await getMembershipConversation(req.auth.user.id, conversationId);
  if (!conversation) {
    return res.status(404).json({ message: 'Conversation not found.' });
  }

  const lastReadMessageId = await markConversationReadUpTo(
    req.auth.user.id,
    conversationId,
    req.body?.messageId
  );

  await broadcastMessageToConversation(conversationId, {
    type: 'conversation_read',
    conversationId,
    userId: req.auth.user.id,
    userEmail: req.auth.user.email,
    lastReadMessageId,
    readAt: new Date().toISOString()
  });

  res.json({
    ok: true,
    conversationId,
    lastReadMessageId
  });
});

app.get('/api/conversations/:conversationId/members', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const conversation = await getMembershipConversation(req.auth.user.id, conversationId);
  if (!conversation) {
    return res.status(404).json({ message: 'Conversation not found.' });
  }

  const [rows] = await pool.query(
    `
      SELECT
        u.id,
        u.email,
        u.nickname,
        u.avatar_url,
        cm.joined_at,
        user_presence.last_seen_at
      FROM conversation_members cm
      INNER JOIN users u ON u.id = cm.user_id
      LEFT JOIN (
        SELECT user_id, MAX(last_seen_at) AS last_seen_at
        FROM user_sessions
        GROUP BY user_id
      ) user_presence ON user_presence.user_id = u.id
      WHERE cm.conversation_id = ?
      ORDER BY cm.joined_at ASC, u.id ASC
    `,
    [conversationId]
  );

  res.json({
    conversation: serializeConversation({
      ...conversation,
      member_count: rows.length,
      last_message_id: null
    }),
    members: rows.map((row) => ({
      ...serializeUser(row),
      isOnline: socketsByUserId.has(Number(row.id)),
      lastSeenAt: row.last_seen_at || null
    }))
  });
});

app.post('/api/conversations/direct', authMiddleware, async (req, res) => {
  const peerEmail = String(req.body?.peerEmail || '').trim().toLowerCase();
  if (!validateEmail(peerEmail)) {
    return res.status(400).json({ message: 'Invalid peer email address.' });
  }
  if (peerEmail === req.auth.user.email.toLowerCase()) {
    return res.status(400).json({ message: 'Cannot create a direct conversation with yourself.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const peer = await getUserByEmail(peerEmail, connection);
    if (!peer) {
      await connection.rollback();
      return res.status(404).json({ message: 'The target user has not registered yet.' });
    }

    if (await isEitherSideBlocked(req.auth.user.id, peer.id, connection)) {
      await connection.rollback();
      return res.status(403).json({ message: 'Cannot create direct conversation due to blacklist settings.' });
    }

    if (!(await areUsersFriends(req.auth.user.id, peer.id, connection))) {
      await connection.rollback();
      return res.status(403).json({ message: 'Please become friends before creating a direct conversation.' });
    }

    const [existingRows] = await connection.query(
      `
        SELECT c.id, c.type, c.name, c.created_at
        FROM conversations c
        INNER JOIN conversation_members cm1 ON cm1.conversation_id = c.id AND cm1.user_id = ?
        INNER JOIN conversation_members cm2 ON cm2.conversation_id = c.id AND cm2.user_id = ?
        WHERE c.type = 'single'
        LIMIT 1
      `,
      [req.auth.user.id, peer.id]
    );

    let conversation = existingRows[0];
    if (!conversation) {
      const conversationId = crypto.randomUUID();
      const conversationName = `${req.auth.user.nickname} / ${peer.nickname}`;
      await connection.query(
        'INSERT INTO conversations (id, type, name, owner_user_id) VALUES (?, ?, ?, ?)',
        [conversationId, 'single', conversationName, req.auth.user.id]
      );
      await connection.query(
        'INSERT INTO conversation_members (conversation_id, user_id) VALUES (?, ?), (?, ?)',
        [conversationId, req.auth.user.id, conversationId, peer.id]
      );
      conversation = {
        id: conversationId,
        type: 'single',
        name: conversationName,
        created_at: new Date()
      };
    }

    await connection.commit();
    return res.json({ ok: true, conversation });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to create direct conversation.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/conversations/group', authMiddleware, async (req, res) => {
  const groupName = String(req.body?.name || '').trim();
  const memberEmails = Array.isArray(req.body?.memberEmails) ? req.body.memberEmails : [];

  if (groupName.length < 1 || groupName.length > 80) {
    return res.status(400).json({ message: 'Group name must be between 1 and 80 characters.' });
  }

  const normalizedEmails = [...new Set(
    memberEmails
      .map((value) => String(value || '').trim().toLowerCase())
      .filter((email) => validateEmail(email) && email !== req.auth.user.email.toLowerCase())
  )];

  if (normalizedEmails.length === 0) {
    return res.status(400).json({ message: 'Please provide at least one valid member email.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const placeholders = normalizedEmails.map(() => '?').join(', ');
    const [peerRows] = await connection.query(
      `SELECT id, email, nickname, avatar_url FROM users WHERE email IN (${placeholders})`,
      normalizedEmails
    );

    if (peerRows.length !== normalizedEmails.length) {
      const foundEmails = new Set(peerRows.map((row) => String(row.email).toLowerCase()));
      const missing = normalizedEmails.find((email) => !foundEmails.has(email));
      await connection.rollback();
      return res.status(404).json({
        message: `The target user has not registered yet: ${missing}`
      });
    }

    const conversationId = crypto.randomUUID();
    await connection.query(
      'INSERT INTO conversations (id, type, name, owner_user_id) VALUES (?, ?, ?, ?)',
      [conversationId, 'group', groupName, req.auth.user.id]
    );

    const memberIds = [req.auth.user.id, ...peerRows.map((row) => Number(row.id))];
    const values = memberIds.map(() => '(?, ?)').join(', ');
    const params = memberIds.flatMap((userId) => [conversationId, userId]);
    await connection.query(
      `INSERT INTO conversation_members (conversation_id, user_id) VALUES ${values}`,
      params
    );

    await connection.commit();
    return res.json({
      ok: true,
      conversation: {
        id: conversationId,
        type: 'group',
        name: groupName,
        created_at: new Date()
      }
    });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to create group conversation.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/conversations/:conversationId/members', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const memberEmails = Array.isArray(req.body?.memberEmails) ? req.body.memberEmails : [];
  const normalizedEmails = [...new Set(
    memberEmails
      .map((value) => String(value || '').trim().toLowerCase())
      .filter((email) => validateEmail(email) && email !== req.auth.user.email.toLowerCase())
  )];

  if (normalizedEmails.length === 0) {
    return res.status(400).json({ message: 'Please provide at least one valid member email.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const [conversationRows] = await connection.query(
      `
        SELECT c.id, c.type, c.name, c.owner_user_id, c.created_at
        FROM conversations c
        INNER JOIN conversation_members cm ON cm.conversation_id = c.id AND cm.user_id = ?
        WHERE c.id = ?
        LIMIT 1
      `,
      [req.auth.user.id, conversationId]
    );

    const conversation = conversationRows[0];
    if (!conversation) {
      await connection.rollback();
      return res.status(404).json({ message: 'Conversation not found.' });
    }
    if (conversation.type !== 'group') {
      await connection.rollback();
      return res.status(400).json({ message: 'Only group conversations can invite new members.' });
    }
    if (Number(conversation.owner_user_id || 0) !== req.auth.user.id) {
      await connection.rollback();
      return res.status(403).json({ message: 'Only the group owner can invite new members.' });
    }

    const placeholders = normalizedEmails.map(() => '?').join(', ');
    const [userRows] = await connection.query(
      `SELECT id, email, nickname, avatar_url FROM users WHERE email IN (${placeholders})`,
      normalizedEmails
    );

    if (userRows.length !== normalizedEmails.length) {
      const foundEmails = new Set(userRows.map((row) => String(row.email).toLowerCase()));
      const missing = normalizedEmails.find((email) => !foundEmails.has(email));
      await connection.rollback();
      return res.status(404).json({ message: `The target user has not registered yet: ${missing}` });
    }

    const memberIds = userRows.map((row) => Number(row.id));
    const existingPlaceholders = memberIds.map(() => '?').join(', ');
    const [existingRows] = await connection.query(
      `SELECT user_id FROM conversation_members WHERE conversation_id = ? AND user_id IN (${existingPlaceholders})`,
      [conversationId, ...memberIds]
    );

    const existingIds = new Set(existingRows.map((row) => Number(row.user_id)));
    const idsToAdd = memberIds.filter((userId) => !existingIds.has(userId));

    if (idsToAdd.length > 0) {
      const values = idsToAdd.map(() => '(?, ?)').join(', ');
      const params = idsToAdd.flatMap((userId) => [conversationId, userId]);
      await connection.query(
        `INSERT INTO conversation_members (conversation_id, user_id) VALUES ${values}`,
        params
      );
    }

    await connection.commit();
    return res.json({
      ok: true,
      addedCount: idsToAdd.length
    });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to invite members.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/conversations/:conversationId/remove-member', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const memberEmail = String(req.body?.memberEmail || '').trim().toLowerCase();

  if (!validateEmail(memberEmail)) {
    return res.status(400).json({ message: 'Invalid member email address.' });
  }
  if (memberEmail === req.auth.user.email.toLowerCase()) {
    return res.status(400).json({ message: 'Use leave group to remove yourself.' });
  }

  const connection = await pool.getConnection();
  try {
    await connection.beginTransaction();

    const [conversationRows] = await connection.query(
      `
        SELECT c.id, c.type, c.name, c.owner_user_id, c.created_at
        FROM conversations c
        INNER JOIN conversation_members cm ON cm.conversation_id = c.id AND cm.user_id = ?
        WHERE c.id = ?
        LIMIT 1
      `,
      [req.auth.user.id, conversationId]
    );

    const conversation = conversationRows[0];
    if (!conversation) {
      await connection.rollback();
      return res.status(404).json({ message: 'Conversation not found.' });
    }
    if (conversation.type !== 'group') {
      await connection.rollback();
      return res.status(400).json({ message: 'Only group conversations can remove members.' });
    }
    if (Number(conversation.owner_user_id || 0) !== req.auth.user.id) {
      await connection.rollback();
      return res.status(403).json({ message: 'Only the group owner can remove members.' });
    }

    const [userRows] = await connection.query(
      'SELECT id, email, nickname FROM users WHERE email = ? LIMIT 1',
      [memberEmail]
    );
    const targetUser = userRows[0];
    if (!targetUser) {
      await connection.rollback();
      return res.status(404).json({ message: 'The target user has not registered yet.' });
    }

    const [memberRows] = await connection.query(
      'SELECT id FROM conversation_members WHERE conversation_id = ? AND user_id = ? LIMIT 1',
      [conversationId, Number(targetUser.id)]
    );
    if (memberRows.length === 0) {
      await connection.rollback();
      return res.status(404).json({ message: 'The target user is not in this group.' });
    }

    await connection.query(
      'DELETE FROM conversation_members WHERE conversation_id = ? AND user_id = ? LIMIT 1',
      [conversationId, Number(targetUser.id)]
    );

    await connection.commit();
    return res.json({
      ok: true,
      removedUser: {
        id: Number(targetUser.id),
        email: targetUser.email,
        nickname: targetUser.nickname
      }
    });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to remove member.', error);
  } finally {
    connection.release();
  }
});

app.post('/api/conversations/:conversationId/leave', authMiddleware, async (req, res) => {
  const conversationId = String(req.params.conversationId || '').trim();
  const connection = await pool.getConnection();

  try {
    await connection.beginTransaction();

    const [conversationRows] = await connection.query(
      `
        SELECT c.id, c.type, c.name, c.owner_user_id, c.created_at
        FROM conversations c
        INNER JOIN conversation_members cm ON cm.conversation_id = c.id AND cm.user_id = ?
        WHERE c.id = ?
        LIMIT 1
      `,
      [req.auth.user.id, conversationId]
    );

    const conversation = conversationRows[0];
    if (!conversation) {
      await connection.rollback();
      return res.status(404).json({ message: 'Conversation not found.' });
    }
    if (conversation.type !== 'group') {
      await connection.rollback();
      return res.status(400).json({ message: 'Only group conversations can be left.' });
    }

    const [memberRows] = await connection.query(
      'SELECT user_id, joined_at FROM conversation_members WHERE conversation_id = ? ORDER BY joined_at ASC, id ASC',
      [conversationId]
    );

    if (memberRows.length <= 1) {
      await connection.query('DELETE FROM conversations WHERE id = ?', [conversationId]);
      await connection.commit();
      return res.json({ ok: true, removedConversation: true });
    }

    await connection.query(
      'DELETE FROM conversation_members WHERE conversation_id = ? AND user_id = ? LIMIT 1',
      [conversationId, req.auth.user.id]
    );

    if (Number(conversation.owner_user_id || 0) === req.auth.user.id) {
      const nextOwner = memberRows.find((row) => Number(row.user_id) !== req.auth.user.id);
      if (nextOwner) {
        await connection.query(
          'UPDATE conversations SET owner_user_id = ? WHERE id = ?',
          [Number(nextOwner.user_id), conversationId]
        );
      }
    }

    await connection.commit();
    return res.json({ ok: true, removedConversation: false });
  } catch (error) {
    await connection.rollback();
    return sendServerError(res, 'Failed to leave group conversation.', error);
  } finally {
    connection.release();
  }
});

app.use((error, req, res, next) => {
  console.error(error);
  res.status(500).json({ message: 'Unexpected server error.' });
});

function typingTimerKey(conversationId, userId) {
  return `${String(conversationId || '').trim()}#${Number(userId) || 0}`;
}

function registerSocketForUser(userId, ws) {
  const normalizedUserId = Number(userId);
  const hadExistingSockets = socketsByUserId.has(normalizedUserId);
  if (!hadExistingSockets) {
    socketsByUserId.set(normalizedUserId, new Set());
  }
  socketsByUserId.get(normalizedUserId).add(ws);
  return !hadExistingSockets;
}

function unregisterSocketForUser(userId, ws) {
  const normalizedUserId = Number(userId);
  if (!socketsByUserId.has(normalizedUserId)) {
    return false;
  }
  const sockets = socketsByUserId.get(normalizedUserId);
  sockets.delete(ws);
  if (sockets.size === 0) {
    socketsByUserId.delete(normalizedUserId);
    return true;
  }
  return false;
}

function clearTypingTimer(conversationId, userId) {
  const key = typingTimerKey(conversationId, userId);
  const timer = typingTimerByConversationUserKey.get(key);
  if (timer) {
    clearTimeout(timer);
    typingTimerByConversationUserKey.delete(key);
  }
}

function scheduleTypingTimeout(conversationId, user) {
  const normalizedConversationId = String(conversationId || '').trim();
  if (!normalizedConversationId || !user || !Number(user.id)) {
    return;
  }

  clearTypingTimer(normalizedConversationId, user.id);
  const key = typingTimerKey(normalizedConversationId, user.id);
  const timer = setTimeout(async () => {
    typingTimerByConversationUserKey.delete(key);
    try {
      await broadcastMessageToConversation(normalizedConversationId, {
        type: 'typing_state',
        conversationId: normalizedConversationId,
        userId: user.id,
        userEmail: user.email,
        userNickname: user.nickname,
        isTyping: false,
        updatedAt: new Date().toISOString(),
        reason: 'timeout'
      });
    } catch (error) {
      console.error('Failed to broadcast typing timeout:', error);
    }
  }, kTypingStateTtlMs);

  typingTimerByConversationUserKey.set(key, timer);
}

async function loadSocketAuthFromRequest(request) {
  const requestUrl = new URL(request.url, `http://${request.headers.host}`);
  if (requestUrl.pathname !== '/ws') {
    throw new Error('Unsupported websocket path.');
  }

  const token = String(requestUrl.searchParams.get('token') || '').trim();
  if (!token) {
    throw new Error('Missing websocket token.');
  }

  const payload = jwt.verify(token, config.jwtSecret);
  const [rows] = await pool.query(
    `
      SELECT s.id, s.user_id, s.token_jti, s.expires_at, s.revoked_at,
             u.email, u.nickname, u.avatar_url
      FROM user_sessions s
      INNER JOIN users u ON u.id = s.user_id
      WHERE s.token_jti = ? AND s.user_id = ? AND s.revoked_at IS NULL AND s.expires_at > NOW()
      LIMIT 1
    `,
    [payload.jti, payload.sub]
  );

  const session = rows[0];
  if (!session) {
    throw new Error('Session expired or revoked.');
  }

  return {
    sessionId: session.id,
    user: {
      id: Number(session.user_id),
      email: session.email,
      nickname: session.nickname,
      avatarUrl: session.avatar_url || ''
    }
  };
}

function socketSend(ws, payload) {
  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

async function loadMessageByClientMessageId(userId, conversationId, clientMessageId, connection = pool) {
  const normalizedConversationId = String(conversationId || '').trim();
  const normalizedClientMessageId = sanitizeClientMessageId(clientMessageId);
  if (!normalizedConversationId || !normalizedClientMessageId) {
    return null;
  }

  const [rows] = await connection.query(
    `
      SELECT
        m.id,
        m.conversation_id,
        m.sender_id,
        m.client_message_id,
        u.email AS sender_email,
        u.nickname AS sender_nickname,
        u.avatar_url AS sender_avatar_url,
        m.message_type,
        m.content,
        m.created_at
      FROM messages m
      INNER JOIN users u ON u.id = m.sender_id
      WHERE m.conversation_id = ?
        AND m.sender_id = ?
        AND m.client_message_id = ?
      LIMIT 1
    `,
    [normalizedConversationId, userId, normalizedClientMessageId]
  );

  return rows[0] || null;
}

async function persistMessage(userId, conversationId, messageType, content, connection = pool,
                              clientMessageId = '') {
  const normalizedType = String(messageType || '').trim().toLowerCase();
  const allowedTypes = new Set(['text', 'system']);
  const typeToStore = allowedTypes.has(normalizedType) ? normalizedType : 'text';
  const normalizedContent = String(content || '');
  const normalizedClientMessageId = sanitizeClientMessageId(clientMessageId);

  if (normalizedClientMessageId) {
    const existingMessage = await loadMessageByClientMessageId(
      userId,
      conversationId,
      normalizedClientMessageId,
      connection
    );
    if (existingMessage) {
      return { row: existingMessage, deduplicated: true };
    }
  }

  let insertResult = null;
  try {
    [insertResult] = await connection.query(
      'INSERT INTO messages (conversation_id, sender_id, client_message_id, message_type, content) VALUES (?, ?, ?, ?, ?)',
      [conversationId, userId, normalizedClientMessageId || null, typeToStore, normalizedContent]
    );
  } catch (error) {
    if (error && error.code === 'ER_DUP_ENTRY' && normalizedClientMessageId) {
      const existingMessage = await loadMessageByClientMessageId(
        userId,
        conversationId,
        normalizedClientMessageId,
        connection
      );
      if (existingMessage) {
        return { row: existingMessage, deduplicated: true };
      }
    }
    throw error;
  }

  const [rows] = await connection.query(
    `
      SELECT
        m.id,
        m.conversation_id,
        m.sender_id,
        m.client_message_id,
        u.email AS sender_email,
        u.nickname AS sender_nickname,
        u.avatar_url AS sender_avatar_url,
        m.message_type,
        m.content,
        m.created_at
      FROM messages m
      INNER JOIN users u ON u.id = m.sender_id
      WHERE m.id = ?
      LIMIT 1
    `,
    [insertResult.insertId]
  );

  return {
    row: rows[0] || null,
    deduplicated: false
  };
}

async function persistTextMessage(userId, conversationId, text, connection = pool, clientMessageId = '') {
  return persistMessage(userId, conversationId, 'text', text, connection, clientMessageId);
}

async function loadMessageById(messageId) {
  const [rows] = await pool.query(
    `
      SELECT
        m.id,
        m.conversation_id,
        m.sender_id,
        m.client_message_id,
        u.email AS sender_email,
        u.nickname AS sender_nickname,
        u.avatar_url AS sender_avatar_url,
        m.message_type,
        m.content,
        m.created_at
      FROM messages m
      INNER JOIN users u ON u.id = m.sender_id
      WHERE m.id = ?
      LIMIT 1
    `,
    [messageId]
  );

  return rows[0] || null;
}

async function recallMessageBySender(userId, conversationId, messageId) {
  const normalizedMessageId = sanitizeMessageId(messageId, 0);
  if (normalizedMessageId <= 0) {
    return { code: 'INVALID_MESSAGE_ID' };
  }

  const originalMessage = await loadMessageById(normalizedMessageId);
  if (!originalMessage || String(originalMessage.conversation_id) !== conversationId) {
    return { code: 'MESSAGE_NOT_FOUND' };
  }

  if (Number(originalMessage.sender_id) !== Number(userId)) {
    return { code: 'NOT_MESSAGE_OWNER' };
  }

  if (String(originalMessage.message_type) !== 'text') {
    return { code: 'MESSAGE_NOT_RECALLABLE_TYPE' };
  }

  if (!canRecallMessageCreatedAt(originalMessage.created_at)) {
    return { code: 'RECALL_WINDOW_EXPIRED' };
  }

  await pool.query(
    'UPDATE messages SET message_type = ?, content = ? WHERE id = ? LIMIT 1',
    ['system', kRecalledSystemText, normalizedMessageId]
  );

  await detachUploadFilesByMessageId(normalizedMessageId);

  const updatedMessage = await loadMessageById(normalizedMessageId);
  if (!updatedMessage) {
    return { code: 'MESSAGE_NOT_FOUND' };
  }

  return { code: 'OK', message: updatedMessage };
}

async function resolveConversationReadTargetMessageId(conversationId, requestedMessageId) {
  const requestedId = sanitizeMessageId(requestedMessageId, 0);
  const [rows] = await pool.query(
    'SELECT id FROM messages WHERE conversation_id = ? ORDER BY id DESC LIMIT 1',
    [conversationId]
  );

  const latestMessageId = rows[0] ? sanitizeMessageId(rows[0].id, 0) : 0;
  if (latestMessageId <= 0) {
    return 0;
  }
  if (requestedId <= 0) {
    return latestMessageId;
  }
  return Math.min(requestedId, latestMessageId);
}

async function markConversationReadUpTo(userId, conversationId, requestedMessageId) {
  const targetMessageId = await resolveConversationReadTargetMessageId(conversationId, requestedMessageId);
  await pool.query(
    `
      UPDATE conversation_members
      SET last_read_message_id = GREATEST(COALESCE(last_read_message_id, 0), ?)
      WHERE conversation_id = ? AND user_id = ?
      LIMIT 1
    `,
    [targetMessageId, conversationId, userId]
  );
  return targetMessageId;
}

async function loadPeerLastReadMessageId(userId, conversationId) {
  const [rows] = await pool.query(
    `
      SELECT MAX(COALESCE(last_read_message_id, 0)) AS peer_last_read_message_id
      FROM conversation_members
      WHERE conversation_id = ? AND user_id <> ?
    `,
    [conversationId, userId]
  );
  return sanitizeMessageId(rows[0]?.peer_last_read_message_id, 0);
}

async function broadcastMessageToConversation(conversationId, payload) {
  const [memberRows] = await pool.query(
    'SELECT user_id FROM conversation_members WHERE conversation_id = ?',
    [conversationId]
  );

  for (const row of memberRows) {
    const sockets = socketsByUserId.get(Number(row.user_id));
    if (!sockets) {
      continue;
    }
    for (const ws of sockets) {
      socketSend(ws, payload);
    }
  }
}

async function loadConversationIdsByUserId(userId) {
  const [rows] = await pool.query(
    'SELECT conversation_id FROM conversation_members WHERE user_id = ?',
    [userId]
  );
  return rows.map((row) => String(row.conversation_id || '').trim()).filter((id) => id.length > 0);
}

async function broadcastPresenceStateToUserConversations(user, isOnline) {
  if (!user || !Number(user.id)) {
    return;
  }

  const conversationIds = await loadConversationIdsByUserId(user.id);
  const payload = {
    type: 'presence_state',
    userId: user.id,
    userEmail: user.email,
    userNickname: user.nickname,
    isOnline: Boolean(isOnline),
    lastSeenAt: new Date().toISOString()
  };

  for (const conversationId of conversationIds) {
    await broadcastMessageToConversation(conversationId, {
      ...payload,
      conversationId
    });
  }
}

async function clearTypingStatesForUser(user) {
  if (!user || !Number(user.id)) {
    return;
  }

  const userId = Number(user.id);
  const targets = [];
  for (const key of typingTimerByConversationUserKey.keys()) {
    const parts = key.split('#');
    if (parts.length !== 2) {
      continue;
    }
    const timerUserId = Number(parts[1]);
    if (timerUserId !== userId) {
      continue;
    }
    targets.push(parts[0]);
  }

  for (const conversationId of targets) {
    clearTypingTimer(conversationId, userId);
    await broadcastMessageToConversation(conversationId, {
      type: 'typing_state',
      conversationId,
      userId: user.id,
      userEmail: user.email,
      userNickname: user.nickname,
      isTyping: false,
      updatedAt: new Date().toISOString(),
      reason: 'offline'
    });
  }
}

server.on('upgrade', async (request, socket, head) => {
  try {
    const auth = await loadSocketAuthFromRequest(request);
    wss.handleUpgrade(request, socket, head, (ws) => {
      ws.auth = auth;
      wss.emit('connection', ws, request);
    });
  } catch (error) {
    socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
    socket.destroy();
  }
});

wss.on('connection', async (ws) => {
  const auth = ws.auth;
  const becameOnline = registerSocketForUser(auth.user.id, ws);
  await pool.query(
    'UPDATE user_sessions SET last_seen_at = NOW() WHERE id = ?',
    [auth.sessionId]
  );
  if (becameOnline) {
    await broadcastPresenceStateToUserConversations(auth.user, true);
  }

  socketSend(ws, {
    type: 'connected',
    user: auth.user
  });

  ws.on('message', async (buffer) => {
    let payload = null;
    try {
      payload = JSON.parse(buffer.toString('utf8'));
    } catch (error) {
      socketSend(ws, { type: 'error', message: 'WebSocket payload must be valid JSON.' });
      return;
    }

    try {
      await pool.query(
        'UPDATE user_sessions SET last_seen_at = NOW() WHERE id = ?',
        [auth.sessionId]
      );

      if (payload.type === 'join_conversation') {
        const conversationId = String(payload.conversationId || '').trim();
        const conversation = await getMembershipConversation(auth.user.id, conversationId);
        if (!conversation) {
          socketSend(ws, { type: 'error', message: 'Conversation not found.' });
          return;
        }
        socketSend(ws, {
          type: 'conversation_joined',
          conversationId,
          name: conversation.name
        });
        return;
      }

      if (payload.type === 'mark_read') {
        const conversationId = String(payload.conversationId || '').trim();
        const conversation = await getMembershipConversation(auth.user.id, conversationId);
        if (!conversation) {
          socketSend(ws, { type: 'error', message: 'Conversation not found.' });
          return;
        }

        const lastReadMessageId = await markConversationReadUpTo(
          auth.user.id,
          conversationId,
          payload.messageId
        );

        await broadcastMessageToConversation(conversationId, {
          type: 'conversation_read',
          conversationId,
          userId: auth.user.id,
          userEmail: auth.user.email,
          lastReadMessageId,
          readAt: new Date().toISOString()
        });
        return;
      }

      if (payload.type === 'typing_state') {
        const conversationId = String(payload.conversationId || '').trim();
        const isTyping = sanitizeTypingState(payload.isTyping);
        const conversation = await getMembershipConversation(auth.user.id, conversationId);
        if (!conversation) {
          socketSend(ws, { type: 'error', message: 'Conversation not found.' });
          return;
        }
        if (await isSingleConversationBlockedForUser(conversation, auth.user.id)) {
          socketSend(ws, {
            type: 'error',
            message: 'Typing state is blocked by blacklist settings.',
            conversationId
          });
          return;
        }

        if (isTyping) {
          scheduleTypingTimeout(conversationId, auth.user);
        } else {
          clearTypingTimer(conversationId, auth.user.id);
        }

        await broadcastMessageToConversation(conversationId, {
          type: 'typing_state',
          conversationId,
          userId: auth.user.id,
          userEmail: auth.user.email,
          userNickname: auth.user.nickname,
          isTyping,
          updatedAt: new Date().toISOString()
        });
        return;
      }

      if (payload.type === 'send_message') {
        const conversationId = String(payload.conversationId || '').trim();
        const text = String(payload.text || '').trim().slice(0, 1000);
        const clientMessageId = String(payload.clientMessageId || '').trim();
        if (!text) {
          socketSend(ws, {
            type: 'error',
            message: 'Message text is empty.',
            clientMessageId,
            conversationId
          });
          return;
        }

        const conversation = await getMembershipConversation(auth.user.id, conversationId);
        if (!conversation) {
          socketSend(ws, {
            type: 'error',
            message: 'Conversation not found.',
            clientMessageId,
            conversationId
          });
          return;
        }
        if (await isSingleConversationBlockedForUser(conversation, auth.user.id)) {
          socketSend(ws, {
            type: 'error',
            message: 'Message sending is blocked by blacklist settings.',
            clientMessageId,
            conversationId
          });
          return;
        }

        const persistResult = await persistTextMessage(
          auth.user.id,
          conversationId,
          text,
          pool,
          clientMessageId
        );
        const row = persistResult.row;
        if (!row) {
          socketSend(ws, {
            type: 'error',
            message: 'Failed to save message.',
            clientMessageId,
            conversationId
          });
          return;
        }

        if (persistResult.deduplicated) {
          socketSend(ws, {
            type: 'message_created',
            conversationId,
            clientMessageId,
            deduplicated: true,
            message: serializeMessage(row)
          });
          return;
        }

        clearTypingTimer(conversationId, auth.user.id);
        await broadcastMessageToConversation(conversationId, {
          type: 'typing_state',
          conversationId,
          userId: auth.user.id,
          userEmail: auth.user.email,
          userNickname: auth.user.nickname,
          isTyping: false,
          updatedAt: new Date().toISOString(),
          reason: 'message_sent'
        });

        await broadcastMessageToConversation(conversationId, {
          type: 'message_created',
          conversationId,
          clientMessageId,
          message: serializeMessage(row)
        });
        return;
      }

      if (payload.type === 'recall_message') {
        const conversationId = String(payload.conversationId || '').trim();
        const messageId = sanitizeMessageId(payload.messageId, 0);
        if (messageId <= 0) {
          socketSend(ws, {
            type: 'error',
            message: 'Invalid message id.',
            conversationId
          });
          return;
        }

        const conversation = await getMembershipConversation(auth.user.id, conversationId);
        if (!conversation) {
          socketSend(ws, {
            type: 'error',
            message: 'Conversation not found.',
            conversationId
          });
          return;
        }

        const recallResult = await recallMessageBySender(auth.user.id, conversationId, messageId);
        if (recallResult.code !== 'OK') {
          const messageByCode = {
            MESSAGE_NOT_FOUND: 'Message not found.',
            NOT_MESSAGE_OWNER: 'You can only recall your own messages.',
            MESSAGE_NOT_RECALLABLE_TYPE: 'Only text messages can be recalled.',
            RECALL_WINDOW_EXPIRED: 'Recall window expired. Messages can be recalled within 2 minutes.',
            INVALID_MESSAGE_ID: 'Invalid message id.'
          };
          socketSend(ws, {
            type: 'error',
            message: messageByCode[recallResult.code] || 'Recall message failed.',
            conversationId,
            messageId
          });
          return;
        }

        await broadcastMessageToConversation(conversationId, {
          type: 'message_recalled',
          conversationId,
          messageId,
          recalledByUserId: auth.user.id,
          recalledByEmail: auth.user.email,
          recalledByNickname: auth.user.nickname,
          recalledAt: new Date().toISOString(),
          message: serializeMessage(recallResult.message)
        });
        return;
      }

      socketSend(ws, { type: 'error', message: 'Unsupported websocket action.' });
    } catch (error) {
      console.error(error);
      if (payload && (payload.type === 'send_message' || payload.type === 'recall_message')) {
        socketSend(ws, {
          type: 'error',
          message: 'WebSocket request failed.',
          clientMessageId: String(payload.clientMessageId || '').trim(),
          conversationId: String(payload.conversationId || '').trim(),
          messageId: sanitizeMessageId(payload.messageId, 0)
        });
      } else {
        socketSend(ws, { type: 'error', message: 'WebSocket request failed.' });
      }
    }
  });

  ws.on('close', async () => {
    try {
      const becameOffline = unregisterSocketForUser(auth.user.id, ws);
      if (becameOffline) {
        await pool.query(
          'UPDATE user_sessions SET last_seen_at = NOW() WHERE id = ?',
          [auth.sessionId]
        );
        await clearTypingStatesForUser(auth.user);
        await broadcastPresenceStateToUserConversations(auth.user, false);
      }
    } catch (error) {
      console.error('WebSocket close cleanup failed:', error);
    }
  });

  ws.on('error', async () => {
    try {
      const becameOffline = unregisterSocketForUser(auth.user.id, ws);
      if (becameOffline) {
        await pool.query(
          'UPDATE user_sessions SET last_seen_at = NOW() WHERE id = ?',
          [auth.sessionId]
        );
        await clearTypingStatesForUser(auth.user);
        await broadcastPresenceStateToUserConversations(auth.user, false);
      }
    } catch (error) {
      console.error('WebSocket error cleanup failed:', error);
    }
  });
});

async function start() {
  validateStartupConfig();
  await fs.promises.mkdir(uploadsDir, { recursive: true });
  await ensureUsersProfileColumns();
  await ensureFriendRequestPairColumns();
  await ensureUploadFilesTable();
  await ensureMessagesClientMessageIdSupport();
  await cleanupOrphanUploadFiles('startup');
  if (!uploadGcTimer) {
    uploadGcTimer = setInterval(() => {
      cleanupOrphanUploadFiles('interval').catch((error) => {
        console.error('Upload GC interval failed:', error);
      });
    }, kUploadGcIntervalMs);
    if (typeof uploadGcTimer.unref === 'function') {
      uploadGcTimer.unref();
    }
  }
  await pool.query('SELECT 1');
  server.listen(config.port, () => {
    console.log(`ChatRoom backend listening on http://127.0.0.1:${config.port}`);
    console.log(`WebSocket endpoint: ws://127.0.0.1:${config.port}/ws?token=YOUR_JWT`);
    console.log(`Mail mode: ${config.mailMode}`);
  });
}

if (require.main === module) {
  start().catch((error) => {
    console.error('Failed to start backend:', error);
    process.exit(1);
  });
}

module.exports = {
  sanitizeMessageId,
  sanitizeClientMessageId,
  sanitizeMessagePageLimit,
  sanitizeSearchLimit,
  paginateMessageRows,
  validateStartupConfig,
  canRecallMessageCreatedAt,
  sanitizeTypingState,
  sanitizeUploadFileName,
  normalizeFriendPairIds,
  inferUploadMimeType,
  isAllowedUploadMimeType,
  chooseStoredFileExtension
};
