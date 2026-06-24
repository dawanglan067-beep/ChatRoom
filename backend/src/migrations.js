'use strict';

function createMigrations(pool) {
  async function doesTableColumnExist(tableName, columnName) {
    const [rows] = await pool.query(
      `SELECT 1 FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ? AND column_name = ? LIMIT 1`,
      [String(tableName || '').trim(), String(columnName || '').trim()]
    );
    return rows.length > 0;
  }

  async function doesTableIndexExist(tableName, indexName) {
    const [rows] = await pool.query(
      `SELECT 1 FROM information_schema.statistics WHERE table_schema = DATABASE() AND table_name = ? AND index_name = ? LIMIT 1`,
      [String(tableName || '').trim(), String(indexName || '').trim()]
    );
    return rows.length > 0;
  }

  async function doesForeignKeyExist(tableName, foreignKeyName) {
    const [rows] = await pool.query(
      `SELECT 1 FROM information_schema.table_constraints WHERE table_schema = DATABASE() AND table_name = ? AND constraint_type = 'FOREIGN KEY' AND constraint_name = ? LIMIT 1`,
      [String(tableName || '').trim(), String(foreignKeyName || '').trim()]
    );
    return rows.length > 0;
  }

  async function ensureFriendRequestPairColumns() {
    if (!(await doesTableColumnExist('friend_requests', 'user_low_id'))) {
      await pool.query(`ALTER TABLE friend_requests ADD COLUMN user_low_id BIGINT UNSIGNED NULL`);
    }
    if (!(await doesTableColumnExist('friend_requests', 'user_high_id'))) {
      await pool.query(`ALTER TABLE friend_requests ADD COLUMN user_high_id BIGINT UNSIGNED NULL`);
    }
    await pool.query(`UPDATE friend_requests SET user_low_id = LEAST(requester_user_id, addressee_user_id), user_high_id = GREATEST(requester_user_id, addressee_user_id) WHERE user_low_id IS NULL OR user_high_id IS NULL`);
    await pool.query(`ALTER TABLE friend_requests MODIFY COLUMN user_low_id BIGINT UNSIGNED NOT NULL, MODIFY COLUMN user_high_id BIGINT UNSIGNED NOT NULL`);
    if (await doesTableIndexExist('friend_requests', 'uk_friend_requests_pair_status')) {
      await pool.query('ALTER TABLE friend_requests DROP INDEX uk_friend_requests_pair_status');
    }
    if (!(await doesTableIndexExist('friend_requests', 'idx_friend_requests_pair_status'))) {
      await pool.query(`ALTER TABLE friend_requests ADD KEY idx_friend_requests_pair_status (user_low_id, user_high_id, status, id)`);
    }
    if (!(await doesForeignKeyExist('friend_requests', 'fk_friend_requests_user_low'))) {
      await pool.query(`ALTER TABLE friend_requests ADD CONSTRAINT fk_friend_requests_user_low FOREIGN KEY (user_low_id) REFERENCES users (id) ON DELETE CASCADE`);
    }
    if (!(await doesForeignKeyExist('friend_requests', 'fk_friend_requests_user_high'))) {
      await pool.query(`ALTER TABLE friend_requests ADD CONSTRAINT fk_friend_requests_user_high FOREIGN KEY (user_high_id) REFERENCES users (id) ON DELETE CASCADE`);
    }
  }

  async function ensureUploadFilesTable() {
    await pool.query(`
      CREATE TABLE IF NOT EXISTS upload_files (
        id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
        stored_file_name VARCHAR(140) NOT NULL,
        original_file_name VARCHAR(120) NOT NULL,
        mime_type VARCHAR(120) NOT NULL DEFAULT 'application/octet-stream',
        file_size INT UNSIGNED NOT NULL,
        conversation_id CHAR(36) NOT NULL,
        message_id BIGINT UNSIGNED NULL,
        uploaded_by_user_id BIGINT UNSIGNED NOT NULL,
        created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
        PRIMARY KEY (id),
        UNIQUE KEY uk_upload_files_stored_file_name (stored_file_name),
        KEY idx_upload_files_conversation_id (conversation_id),
        KEY idx_upload_files_message_id (message_id),
        KEY idx_upload_files_uploaded_by_user_id (uploaded_by_user_id),
        CONSTRAINT fk_upload_files_conversation FOREIGN KEY (conversation_id) REFERENCES conversations (id) ON DELETE CASCADE,
        CONSTRAINT fk_upload_files_message FOREIGN KEY (message_id) REFERENCES messages (id) ON DELETE SET NULL,
        CONSTRAINT fk_upload_files_uploaded_by FOREIGN KEY (uploaded_by_user_id) REFERENCES users (id) ON DELETE CASCADE
      ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    `);
  }

  async function ensureMessagesClientMessageIdSupport() {
    if (!(await doesTableColumnExist('messages', 'client_message_id'))) {
      await pool.query(`ALTER TABLE messages ADD COLUMN client_message_id VARCHAR(80) NULL AFTER sender_id`);
    }
    if (!(await doesTableIndexExist('messages', 'uk_messages_sender_client_message_id'))) {
      await pool.query(`ALTER TABLE messages ADD UNIQUE KEY uk_messages_sender_client_message_id (conversation_id, sender_id, client_message_id)`);
    }
  }

  async function ensureUsersProfileColumns() {
    if (!(await doesTableColumnExist('users', 'avatar_url'))) {
      await pool.query('ALTER TABLE users ADD COLUMN avatar_url VARCHAR(500) NULL AFTER nickname');
    }
  }

  return {
    ensureFriendRequestPairColumns,
    ensureUploadFilesTable,
    ensureMessagesClientMessageIdSupport,
    ensureUsersProfileColumns
  };
}

module.exports = { createMigrations };
