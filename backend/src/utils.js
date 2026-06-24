'use strict';

const crypto = require('crypto');
const jwt = require('jsonwebtoken');
const path = require('path');

function validateEmail(email) {
  return /^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$/.test(String(email || '').trim());
}

function normalizeNickname(value) {
  return String(value || '').trim().replace(/\s+/g, ' ').slice(0, 60);
}

function createVerificationCode() {
  return String(Math.floor(100000 + Math.random() * 900000));
}

function hashCode(email, code) {
  return crypto.createHash('sha256').update(`${String(email).trim().toLowerCase()}:${code}`).digest('hex');
}

function createJwtToken(user, tokenJti, jwtSecret, jwtExpiresIn) {
  return jwt.sign(
    { sub: String(user.id), email: user.email, nickname: user.nickname, jti: tokenJti },
    jwtSecret,
    { expiresIn: jwtExpiresIn }
  );
}

function parseAuthorizationHeader(headerValue) {
  const value = String(headerValue || '');
  if (!value.startsWith('Bearer ')) return '';
  return value.slice('Bearer '.length).trim();
}

function serializeUser(row) {
  return {
    id: row.id,
    email: row.email,
    nickname: row.nickname,
    avatarUrl: row.avatar_url || ''
  };
}

function serializeConversation(row) {
  return {
    id: row.id,
    type: row.type,
    name: row.name,
    ownerUserId: row.owner_user_id ? Number(row.owner_user_id) : null,
    ownerEmail: row.owner_email || '',
    ownerNickname: row.owner_nickname || '',
    createdAt: row.created_at,
    memberCount: Number(row.member_count || 0),
    onlineCount: Number(row.online_count || 0),
    unreadCount: Number(row.unread_count || 0),
    lastMessage: row.last_message_id
      ? {
          id: Number(row.last_message_id),
          messageType: row.last_message_type,
          text: row.last_message_content,
          senderId: Number(row.last_sender_id),
          senderEmail: row.last_sender_email || '',
          createdAt: row.last_message_at
        }
      : null
  };
}

function serializeMessage(row) {
  return {
    id: Number(row.id),
    conversationId: row.conversation_id,
    senderId: Number(row.sender_id),
    clientMessageId: String(row.client_message_id || ''),
    senderEmail: row.sender_email,
    senderNickname: row.sender_nickname,
    senderAvatarUrl: row.sender_avatar_url || '',
    messageType: row.message_type,
    content: row.content,
    createdAt: row.created_at
  };
}

function parseDurationToSeconds(value) {
  const source = String(value || '7d').trim();
  const match = source.match(/^(\d+)([smhd])$/i);
  if (!match) return 7 * 24 * 60 * 60;
  const amount = Number(match[1]);
  const unit = match[2].toLowerCase();
  switch (unit) {
    case 's': return amount;
    case 'm': return amount * 60;
    case 'h': return amount * 60 * 60;
    case 'd': default: return amount * 24 * 60 * 60;
  }
}

function sanitizeMessagePageLimit(rawLimit) {
  if (rawLimit === undefined || rawLimit === null || String(rawLimit).trim() === '') return 50;
  const parsed = Number(rawLimit);
  if (!Number.isFinite(parsed)) return 50;
  const integerLimit = Math.trunc(parsed);
  return Math.max(1, Math.min(integerLimit, 100));
}

function sanitizeSearchLimit(rawLimit) {
  if (rawLimit === undefined || rawLimit === null || String(rawLimit).trim() === '') return 20;
  const parsed = Number(rawLimit);
  if (!Number.isFinite(parsed)) return 20;
  const integerLimit = Math.trunc(parsed);
  return Math.max(1, Math.min(integerLimit, 100));
}

function sanitizeMessageId(rawValue, fallback = 0) {
  if (rawValue === undefined || rawValue === null || String(rawValue).trim() === '') return Math.max(0, Math.trunc(Number(fallback) || 0));
  const parsed = Number(rawValue);
  if (!Number.isFinite(parsed)) return Math.max(0, Math.trunc(Number(fallback) || 0));
  return Math.max(0, Math.trunc(parsed));
}

function sanitizeClientMessageId(rawValue) {
  const normalized = String(rawValue || '').trim();
  if (!normalized) return '';
  return normalized.slice(0, 80);
}

function parseDateLikeToEpochMs(value) {
  if (value instanceof Date) return value.getTime();
  if (typeof value === 'number') return Number.isFinite(value) ? value : Number.NaN;
  const parsed = Date.parse(String(value || '').trim());
  return Number.isFinite(parsed) ? parsed : Number.NaN;
}

function canRecallMessageCreatedAt(createdAtValue, nowMs = Date.now(), windowMs = 2 * 60 * 1000) {
  const createdAtMs = parseDateLikeToEpochMs(createdAtValue);
  if (!Number.isFinite(createdAtMs) || !Number.isFinite(nowMs) || !Number.isFinite(windowMs) || windowMs <= 0) return false;
  if (createdAtMs > nowMs) return false;
  return (nowMs - createdAtMs) <= windowMs;
}

function sanitizeTypingState(rawValue) {
  if (typeof rawValue === 'boolean') return rawValue;
  if (typeof rawValue === 'number') return rawValue !== 0;
  const normalized = String(rawValue || '').trim().toLowerCase();
  return normalized === '1' || normalized === 'true' || normalized === 'yes';
}

function sanitizeUploadFileName(rawName) {
  const source = String(rawName || '').trim();
  const fallback = 'upload.bin';
  if (!source) return fallback;
  const onlyName = path.basename(source);
  const safe = onlyName.replace(/[<>:"/\\|?*\x00-\x1F]/g, '_').replace(/\s+/g, ' ').trim();
  if (!safe) return fallback;
  return safe.slice(0, 120);
}

function normalizeFriendPairIds(userIdA, userIdB) {
  const a = Number(userIdA);
  const b = Number(userIdB);
  if (!Number.isFinite(a) || !Number.isFinite(b)) return null;
  const left = Math.trunc(a);
  const right = Math.trunc(b);
  if (left <= 0 || right <= 0 || left === right) return null;
  return left < right ? { lowUserId: left, highUserId: right } : { lowUserId: right, highUserId: left };
}

function isImageMimeType(mimeType) {
  return String(mimeType || '').trim().toLowerCase().startsWith('image/');
}

function detectFileExtension(fileName) {
  const extension = path.extname(String(fileName || '').trim()).toLowerCase();
  if (!extension || extension.length > 12) return '';
  return extension;
}

function normalizeMimeType(rawMimeType) {
  return String(rawMimeType || '').trim().toLowerCase();
}

function detectMimeTypeBySignature(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length < 4) return '';
  if (buffer.length >= 8 && buffer[0] === 0x89 && buffer[1] === 0x50 && buffer[2] === 0x4E && buffer[3] === 0x47) return 'image/png';
  if (buffer.length >= 3 && buffer[0] === 0xFF && buffer[1] === 0xD8 && buffer[2] === 0xFF) return 'image/jpeg';
  if (buffer.length >= 6) {
    const gifHeader = buffer.toString('ascii', 0, 6);
    if (gifHeader === 'GIF87a' || gifHeader === 'GIF89a') return 'image/gif';
  }
  if (buffer.length >= 12 && buffer.toString('ascii', 0, 4) === 'RIFF' && buffer.toString('ascii', 8, 12) === 'WEBP') return 'image/webp';
  if (buffer.length >= 2 && buffer[0] === 0x42 && buffer[1] === 0x4D) return 'image/bmp';
  if (buffer.length >= 5 && buffer[0] === 0x25 && buffer[1] === 0x50 && buffer[2] === 0x44 && buffer[3] === 0x46 && buffer[4] === 0x2D) return 'application/pdf';
  if (buffer.length >= 4 && buffer[0] === 0x50 && buffer[1] === 0x4B && (buffer[2] === 0x03 || buffer[2] === 0x05 || buffer[2] === 0x07) && (buffer[3] === 0x04 || buffer[3] === 0x06 || buffer[3] === 0x08)) return 'application/zip';
  return '';
}

function isLikelyUtf8Text(buffer) {
  if (!Buffer.isBuffer(buffer) || buffer.length === 0) return false;
  if (buffer.includes(0x00)) return false;
  const sample = buffer.subarray(0, Math.min(buffer.length, 4096));
  let printableCount = 0;
  for (const byte of sample) {
    const isPrintableAscii = byte >= 0x20 && byte <= 0x7E;
    const isCommonWhitespace = byte === 0x09 || byte === 0x0A || byte === 0x0D;
    if (isPrintableAscii || isCommonWhitespace || byte >= 0x80) printableCount += 1;
  }
  return (printableCount / sample.length) >= 0.9;
}

function inferUploadMimeType(buffer, fileName, rawMimeType = '') {
  const signatureMimeType = detectMimeTypeBySignature(buffer);
  if (signatureMimeType) return signatureMimeType;
  const extension = detectFileExtension(fileName);
  const kExtensionToMimeMap = new Map([
    ['.png', 'image/png'], ['.jpg', 'image/jpeg'], ['.jpeg', 'image/jpeg'],
    ['.gif', 'image/gif'], ['.webp', 'image/webp'], ['.bmp', 'image/bmp'],
    ['.pdf', 'application/pdf'], ['.zip', 'application/zip'],
    ['.txt', 'text/plain'], ['.log', 'text/plain'], ['.md', 'text/plain'],
    ['.json', 'text/plain'], ['.csv', 'text/plain']
  ]);
  const extensionMimeType = kExtensionToMimeMap.get(extension) || '';
  if (extensionMimeType === 'text/plain' && isLikelyUtf8Text(buffer)) return extensionMimeType;
  const claimedMimeType = normalizeMimeType(rawMimeType);
  if (claimedMimeType === 'text/plain' && isLikelyUtf8Text(buffer)) return 'text/plain';
  return extensionMimeType || claimedMimeType || 'application/octet-stream';
}

function isAllowedUploadMimeType(mimeType) {
  const kAllowedUploadMimeTypes = new Set([
    'image/png', 'image/jpeg', 'image/gif', 'image/webp', 'image/bmp',
    'application/pdf', 'application/zip', 'text/plain'
  ]);
  return kAllowedUploadMimeTypes.has(normalizeMimeType(mimeType));
}

function chooseStoredFileExtension(fileName, mimeType) {
  const normalizedMimeType = normalizeMimeType(mimeType);
  const kMimeToExtensionMap = new Map([
    ['image/png', '.png'], ['image/jpeg', '.jpg'], ['image/gif', '.gif'],
    ['image/webp', '.webp'], ['image/bmp', '.bmp'],
    ['application/pdf', '.pdf'], ['application/zip', '.zip'],
    ['text/plain', '.txt']
  ]);
  const mapped = kMimeToExtensionMap.get(normalizedMimeType);
  if (mapped) return mapped;
  return detectFileExtension(fileName);
}

function buildPublicUploadUrl(fileName) {
  return `/uploads/${fileName}`;
}

function isAvatarUploadFileName(fileName) {
  return String(fileName || '').startsWith('avatar_');
}

function sanitizeStoredUploadFileName(rawName) {
  const normalized = String(rawName || '').trim();
  if (!normalized || normalized.includes('/') || normalized.includes('\\')) return '';
  if (!/^[A-Za-z0-9._-]+$/.test(normalized)) return '';
  return normalized;
}

function escapeLikePattern(value) {
  return String(value || '').replace(/\\/g, '\\\\').replace(/%/g, '\\%').replace(/_/g, '\\_');
}

function sendServerError(res, message, error, exposeErrorDetail = false) {
  const payload = { message };
  if (exposeErrorDetail && error && String(error.message || '').trim()) {
    payload.detail = String(error.message || '').trim();
  }
  return res.status(500).json(payload);
}

function paginateMessageRows(rowsInDescOrder, limit) {
  const rows = Array.isArray(rowsInDescOrder) ? [...rowsInDescOrder] : [];
  const pageLimit = sanitizeMessagePageLimit(limit);
  const hasMore = rows.length > pageLimit;
  if (hasMore) rows.pop();
  const rowsInAscOrder = rows.reverse();
  const oldestReturnedRow = rowsInAscOrder[0] || null;
  const nextBeforeId = oldestReturnedRow ? Number(oldestReturnedRow.id || 0) : null;
  return { rows: rowsInAscOrder, hasMore, nextBeforeId: nextBeforeId > 0 ? nextBeforeId : null };
}

function parseCorsOrigins(rawOrigins) {
  return String(rawOrigins || '').split(',').map((item) => item.trim()).filter((item) => item.length > 0);
}

module.exports = {
  validateEmail, normalizeNickname, createVerificationCode, hashCode,
  createJwtToken, parseAuthorizationHeader, serializeUser, serializeConversation,
  serializeMessage, parseDurationToSeconds, sanitizeMessagePageLimit, sanitizeSearchLimit,
  sanitizeMessageId, sanitizeClientMessageId, parseDateLikeToEpochMs, canRecallMessageCreatedAt,
  sanitizeTypingState, sanitizeUploadFileName, normalizeFriendPairIds, isImageMimeType,
  detectFileExtension, normalizeMimeType, detectMimeTypeBySignature, isLikelyUtf8Text,
  inferUploadMimeType, isAllowedUploadMimeType, chooseStoredFileExtension, buildPublicUploadUrl,
  isAvatarUploadFileName, sanitizeStoredUploadFileName, escapeLikePattern, sendServerError,
  paginateMessageRows, parseCorsOrigins
};
