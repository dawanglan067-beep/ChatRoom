USE chatroom;

INSERT INTO users (id, email, nickname)
VALUES
  (1, 'alice@example.com', 'Alice'),
  (2, 'bob@example.com', 'Bob'),
  (3, 'carol@example.com', 'Carol')
ON DUPLICATE KEY UPDATE nickname = VALUES(nickname);

INSERT INTO conversations (id, type, name, owner_user_id)
VALUES
  ('c-001-demo-group', 'group', 'Product Team', 1),
  ('c-002-demo-group', 'group', 'Frontend Sync', 2)
ON DUPLICATE KEY UPDATE name = VALUES(name);

INSERT INTO conversation_members (conversation_id, user_id)
VALUES
  ('c-001-demo-group', 1),
  ('c-001-demo-group', 2),
  ('c-001-demo-group', 3),
  ('c-002-demo-group', 1),
  ('c-002-demo-group', 2)
ON DUPLICATE KEY UPDATE user_id = VALUES(user_id);

INSERT INTO messages (conversation_id, sender_id, message_type, content, created_at)
VALUES
  ('c-001-demo-group', 1, 'text', 'Let us finish the chat app skeleton first.', NOW() - INTERVAL 20 MINUTE),
  ('c-001-demo-group', 2, 'text', 'I will split the UI and data layer first.', NOW() - INTERVAL 16 MINUTE),
  ('c-001-demo-group', 3, 'text', 'Please keep auto-scroll working after send.', NOW() - INTERVAL 10 MINUTE),
  ('c-002-demo-group', 2, 'text', 'Need a clean API for the conversation list.', NOW() - INTERVAL 8 MINUTE);

