# ChatRoom Phase 3 Backend

This folder contains the first real backend for the Qt chat client.

## What is included

- MySQL schema for users, login codes, sessions, conversations, members, and messages
- Email-code login endpoints
- JWT-based authentication
- Conversation list and message history endpoints
- WebSocket endpoint with token authentication
- Message persistence plus server-side broadcast

## Quick start

1. Create a MySQL database with the SQL in `sql/schema.sql`
2. Optionally import `sql/seed-demo.sql`
3. Copy `.env.example` to `.env`
4. Run `npm install`
5. Run `npm run dev`
6. Run `npm test` (helper-level automated checks)

## Security config

- `JWT_SECRET` is required and must be at least 32 characters.
- `CORS_ORIGINS` is a comma-separated allowlist.  
  Example: `CORS_ORIGINS=http://127.0.0.1:5173,http://localhost:5173`
- If `CORS_ORIGINS` is empty, all origins are allowed (development-friendly, not recommended for production).

## Development mail mode

If `MAIL_MODE=log`, the backend does not send real email.
It prints the verification code in the backend console instead.

## HTTP API

- `GET /api/health`
- `POST /api/auth/request-code`
- `POST /api/auth/verify-code`
- `GET /api/me`
- `GET /api/conversations`
- `GET /api/conversations/:conversationId/messages`
- `POST /api/conversations/:conversationId/read`
- `POST /api/conversations/direct`

### Message history pagination

`GET /api/conversations/:conversationId/messages?limit=50&beforeId=12345`

Response now includes:

- `messages`: current page messages (old -> new)
- `readState.lastReadMessageId`: server-side read cursor for current user
- `pagination.hasMore`: whether older pages still exist
- `pagination.nextBeforeId`: pass this as the next request's `beforeId`

## WebSocket API

Connect to:

- `ws://127.0.0.1:3000/ws?token=YOUR_JWT`

Supported messages:

```json
{ "type": "join_conversation", "conversationId": "c-001-demo-group" }
```

```json
{ "type": "send_message", "conversationId": "c-001-demo-group", "text": "hello" }
```

```json
{ "type": "mark_read", "conversationId": "c-001-demo-group", "messageId": 12345 }
```

## Important architecture change

The SMTP authorization code should stay on the backend, not inside the Qt client.
For a real product, the Qt client should call the backend auth endpoints instead of sending email directly.
