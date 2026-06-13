'use strict';

const crypto = require('crypto');
const http = require('http');

const host = process.env.CHATROOM_WS_HOST || '127.0.0.1';
const port = Number(process.env.CHATROOM_WS_PORT || 8765);

const clients = new Map();
const rooms = new Map();

function createAcceptValue(websocketKey) {
  return crypto
    .createHash('sha1')
    .update(websocketKey + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11', 'binary')
    .digest('base64');
}

function buildTextFrame(text) {
  const payload = Buffer.from(text, 'utf8');
  const payloadLength = payload.length;

  if (payloadLength > 65535) {
    throw new Error('Payload too large for the demo chat server');
  }

  if (payloadLength < 126) {
    return Buffer.concat([Buffer.from([0x81, payloadLength]), payload]);
  }

  const header = Buffer.alloc(4);
  header[0] = 0x81;
  header[1] = 126;
  header.writeUInt16BE(payloadLength, 2);
  return Buffer.concat([header, payload]);
}

function decodeFrame(buffer) {
  const firstByte = buffer[0];
  const secondByte = buffer[1];
  const opcode = firstByte & 0x0f;
  const masked = (secondByte & 0x80) === 0x80;
  let payloadLength = secondByte & 0x7f;
  let currentOffset = 2;

  if (payloadLength === 126) {
    payloadLength = buffer.readUInt16BE(currentOffset);
    currentOffset += 2;
  } else if (payloadLength === 127) {
    throw new Error('64-bit payloads are not supported in this demo server');
  }

  let maskingKey = null;
  if (masked) {
    maskingKey = buffer.subarray(currentOffset, currentOffset + 4);
    currentOffset += 4;
  }

  const payload = buffer.subarray(currentOffset, currentOffset + payloadLength);
  if (!masked) {
    return { opcode, payload: Buffer.from(payload) };
  }

  const decoded = Buffer.alloc(payloadLength);
  for (let index = 0; index < payloadLength; index += 1) {
    decoded[index] = payload[index] ^ maskingKey[index % 4];
  }

  return { opcode, payload: decoded };
}

function sendJson(socket, payload) {
  socket.write(buildTextFrame(JSON.stringify(payload)));
}

function sanitizeName(value, fallback) {
  if (typeof value !== 'string') {
    return fallback;
  }

  const trimmed = value.trim();
  return trimmed.length > 0 ? trimmed.slice(0, 30) : fallback;
}

function sanitizeText(value) {
  if (typeof value !== 'string') {
    return '';
  }

  return value.trim().slice(0, 500);
}

function roomSet(roomId) {
  if (!rooms.has(roomId)) {
    rooms.set(roomId, new Set());
  }
  return rooms.get(roomId);
}

function currentUsers(roomId) {
  const sockets = roomSet(roomId);
  return Array.from(sockets)
    .map((socket) => clients.get(socket))
    .filter(Boolean)
    .map((client) => client.username)
    .sort((left, right) => left.localeCompare(right));
}

function broadcastToRoom(roomId, payload) {
  const sockets = roomSet(roomId);
  for (const socket of sockets) {
    sendJson(socket, payload);
  }
}

function broadcastUserList(roomId, roomName) {
  broadcastToRoom(roomId, {
    type: 'user_list',
    roomId,
    roomName,
    users: currentUsers(roomId),
  });
}

function leaveRoom(socket, announce) {
  const client = clients.get(socket);
  if (!client || !client.roomId) {
    return;
  }

  const sockets = roomSet(client.roomId);
  sockets.delete(socket);

  const previousRoomId = client.roomId;
  const previousRoomName = client.roomName;
  client.roomId = '';
  client.roomName = '';

  if (announce && client.username) {
    broadcastToRoom(previousRoomId, {
      type: 'system',
      roomId: previousRoomId,
      roomName: previousRoomName,
      text: `${client.username} 离开了房间。`,
    });
  }

  broadcastUserList(previousRoomId, previousRoomName);

  if (sockets.size === 0) {
    rooms.delete(previousRoomId);
  }
}

function joinRoom(socket, roomId, roomName, actionType) {
  const client = clients.get(socket);
  if (!client) {
    return;
  }

  const nextRoomId = sanitizeName(roomId, 'lobby');
  const nextRoomName = sanitizeName(roomName, '大厅');

  if (client.roomId === nextRoomId) {
    sendJson(socket, {
      type: 'room_joined',
      roomId: nextRoomId,
      roomName: nextRoomName,
      action: actionType,
    });
    broadcastUserList(nextRoomId, nextRoomName);
    return;
  }

  leaveRoom(socket, true);

  client.roomId = nextRoomId;
  client.roomName = nextRoomName;
  roomSet(nextRoomId).add(socket);

  sendJson(socket, {
    type: 'room_joined',
    roomId: nextRoomId,
    roomName: nextRoomName,
    action: actionType,
  });

  if (client.username) {
    broadcastToRoom(nextRoomId, {
      type: 'system',
      roomId: nextRoomId,
      roomName: nextRoomName,
      text: `${client.username} 进入了房间。`,
    });
  }

  broadcastUserList(nextRoomId, nextRoomName);
}

function handleLogin(socket, payload) {
  const client = clients.get(socket);
  if (!client) {
    return;
  }

  client.username = sanitizeName(payload.username, `Guest-${client.id}`);
  const roomId = sanitizeName(payload.roomId, 'lobby');
  const roomName = sanitizeName(payload.roomName, '大厅');

  sendJson(socket, {
    type: 'login_ack',
    clientId: client.id,
    username: client.username,
    roomId,
    roomName,
  });

  joinRoom(socket, roomId, roomName, 'login');
}

function handleJoinRoom(socket, payload) {
  const client = clients.get(socket);
  if (!client || !client.username) {
    sendJson(socket, {
      type: 'error',
      text: '请先登录，再加入房间。',
    });
    return;
  }

  joinRoom(socket, payload.roomId, payload.roomName, 'switch');
}

function handleMessage(socket, payload) {
  const client = clients.get(socket);
  if (!client || !client.username) {
    sendJson(socket, {
      type: 'error',
      text: '请先登录，再发送消息。',
    });
    return;
  }

  if (!client.roomId) {
    sendJson(socket, {
      type: 'error',
      text: '请先加入一个房间。',
    });
    return;
  }

  const text = sanitizeText(payload.text);
  if (!text) {
    return;
  }

  broadcastToRoom(client.roomId, {
    type: 'message',
    roomId: client.roomId,
    roomName: client.roomName,
    sender: client.username,
    text,
    serverTimestamp: Date.now(),
  });
}

function handleClientPayload(socket, payload) {
  switch (payload.type) {
    case 'login':
      handleLogin(socket, payload);
      return;
    case 'join_room':
      handleJoinRoom(socket, payload);
      return;
    case 'message':
      handleMessage(socket, payload);
      return;
    default:
      sendJson(socket, {
        type: 'error',
        text: `不支持的操作：${payload.type || 'unknown'}`,
      });
  }
}

const server = http.createServer((request, response) => {
  response.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  response.end('WebSocket 聊天模拟服务已启动。\n');
});

server.on('upgrade', (request, socket) => {
  const websocketKey = request.headers['sec-websocket-key'];
  if (!websocketKey) {
    socket.write('HTTP/1.1 400 Bad Request\r\n\r\n');
    socket.destroy();
    return;
  }

  const acceptValue = createAcceptValue(websocketKey);
  const headers = [
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${acceptValue}`,
    '\r\n',
  ];

  socket.write(headers.join('\r\n'));

  const clientState = {
    id: crypto.randomBytes(4).toString('hex'),
    username: '',
    roomId: '',
    roomName: '',
  };
  clients.set(socket, clientState);

  console.log(`客户端已连接：${request.socket.remoteAddress}`);

  socket.on('data', (chunk) => {
    try {
      const frame = decodeFrame(chunk);
      if (frame.opcode === 0x8) {
        socket.end();
        return;
      }

      if (frame.opcode === 0x9) {
        socket.write(Buffer.from([0x8a, 0x00]));
        return;
      }

      if (frame.opcode !== 0x1) {
        return;
      }

      const messageText = frame.payload.toString('utf8');
      console.log(`收到消息：${messageText}`);

      let payload = null;
      try {
        payload = JSON.parse(messageText);
      } catch (error) {
        sendJson(socket, {
          type: 'error',
          text: '消息体必须是合法的 JSON。',
        });
        return;
      }

      handleClientPayload(socket, payload);
    } catch (error) {
      console.error('解析帧失败：', error.message);
      socket.end();
    }
  });

  socket.on('close', () => {
    const client = clients.get(socket);
    if (client && client.username) {
      console.log(`${client.username} 已断开连接`);
    } else {
      console.log('客户端已断开连接');
    }

    leaveRoom(socket, true);
    clients.delete(socket);
  });

  socket.on('error', (error) => {
    console.error('套接字错误：', error.message);
  });
});

server.listen(port, host, () => {
  console.log(`聊天模拟服务监听地址：ws://${host}:${port}`);
});
