CREATE DATABASE IF NOT EXISTS chatroom
  DEFAULT CHARACTER SET utf8mb4
  DEFAULT COLLATE utf8mb4_unicode_ci;

USE chatroom;

CREATE TABLE IF NOT EXISTS users (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  email VARCHAR(120) NOT NULL,
  nickname VARCHAR(60) NOT NULL,
  avatar_url VARCHAR(255) NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_users_email (email)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS login_codes (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  email VARCHAR(120) NOT NULL,
  code_hash CHAR(64) NOT NULL,
  sent_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  used_at DATETIME NULL,
  ip_address VARCHAR(64) NULL,
  attempts INT NOT NULL DEFAULT 0,
  PRIMARY KEY (id),
  KEY idx_login_codes_email_sent_at (email, sent_at),
  KEY idx_login_codes_email_expires_at (email, expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS user_sessions (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_id BIGINT UNSIGNED NOT NULL,
  token_jti CHAR(36) NOT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  expires_at DATETIME NOT NULL,
  last_seen_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  revoked_at DATETIME NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uk_user_sessions_token_jti (token_jti),
  KEY idx_user_sessions_user_id (user_id),
  CONSTRAINT fk_user_sessions_user
    FOREIGN KEY (user_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS conversations (
  id CHAR(36) NOT NULL,
  type ENUM('single', 'group') NOT NULL,
  name VARCHAR(80) NOT NULL,
  owner_user_id BIGINT UNSIGNED NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_conversations_owner_user_id (owner_user_id),
  CONSTRAINT fk_conversations_owner
    FOREIGN KEY (owner_user_id) REFERENCES users (id)
    ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS conversation_members (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  conversation_id CHAR(36) NOT NULL,
  user_id BIGINT UNSIGNED NOT NULL,
  joined_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_read_message_id BIGINT UNSIGNED NULL,
  PRIMARY KEY (id),
  UNIQUE KEY uk_conversation_members_pair (conversation_id, user_id),
  KEY idx_conversation_members_user_id (user_id),
  CONSTRAINT fk_conversation_members_conversation
    FOREIGN KEY (conversation_id) REFERENCES conversations (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_conversation_members_user
    FOREIGN KEY (user_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS messages (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  conversation_id CHAR(36) NOT NULL,
  sender_id BIGINT UNSIGNED NOT NULL,
  client_message_id VARCHAR(80) NULL,
  message_type ENUM('text', 'system') NOT NULL DEFAULT 'text',
  content TEXT NOT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_messages_conversation_id_id (conversation_id, id),
  KEY idx_messages_sender_id (sender_id),
  UNIQUE KEY uk_messages_sender_client_message_id (conversation_id, sender_id, client_message_id),
  CONSTRAINT fk_messages_conversation
    FOREIGN KEY (conversation_id) REFERENCES conversations (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_messages_sender
    FOREIGN KEY (sender_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS friendships (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  user_low_id BIGINT UNSIGNED NOT NULL,
  user_high_id BIGINT UNSIGNED NOT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_friendships_pair (user_low_id, user_high_id),
  KEY idx_friendships_user_low (user_low_id),
  KEY idx_friendships_user_high (user_high_id),
  CONSTRAINT fk_friendships_user_low
    FOREIGN KEY (user_low_id) REFERENCES users (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_friendships_user_high
    FOREIGN KEY (user_high_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS friend_requests (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  requester_user_id BIGINT UNSIGNED NOT NULL,
  addressee_user_id BIGINT UNSIGNED NOT NULL,
  user_low_id BIGINT UNSIGNED NOT NULL,
  user_high_id BIGINT UNSIGNED NOT NULL,
  status ENUM('pending', 'accepted', 'rejected', 'canceled') NOT NULL DEFAULT 'pending',
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  handled_at DATETIME NULL,
  PRIMARY KEY (id),
  KEY idx_friend_requests_pair_status (user_low_id, user_high_id, status, id),
  KEY idx_friend_requests_addressee_status (addressee_user_id, status, id),
  KEY idx_friend_requests_requester_status (requester_user_id, status, id),
  CONSTRAINT fk_friend_requests_user_low
    FOREIGN KEY (user_low_id) REFERENCES users (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_friend_requests_user_high
    FOREIGN KEY (user_high_id) REFERENCES users (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_friend_requests_requester
    FOREIGN KEY (requester_user_id) REFERENCES users (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_friend_requests_addressee
    FOREIGN KEY (addressee_user_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS user_blacklist (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  owner_user_id BIGINT UNSIGNED NOT NULL,
  blocked_user_id BIGINT UNSIGNED NOT NULL,
  created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uk_user_blacklist_pair (owner_user_id, blocked_user_id),
  KEY idx_user_blacklist_blocked (blocked_user_id),
  CONSTRAINT fk_user_blacklist_owner
    FOREIGN KEY (owner_user_id) REFERENCES users (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_user_blacklist_blocked
    FOREIGN KEY (blocked_user_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

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
  CONSTRAINT fk_upload_files_conversation
    FOREIGN KEY (conversation_id) REFERENCES conversations (id)
    ON DELETE CASCADE,
  CONSTRAINT fk_upload_files_message
    FOREIGN KEY (message_id) REFERENCES messages (id)
    ON DELETE SET NULL,
  CONSTRAINT fk_upload_files_uploaded_by
    FOREIGN KEY (uploaded_by_user_id) REFERENCES users (id)
    ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
