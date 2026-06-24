'use strict';

const path = require('path');

require('dotenv').config({
  path: path.resolve(__dirname, '..', '.env')
});

const config = {
  port: Number(process.env.PORT || 3000),
  jwtSecret: String(process.env.JWT_SECRET || '').trim(),
  corsOrigins: String(process.env.CORS_ORIGINS || '').trim(),
  jwtExpiresIn: process.env.JWT_EXPIRES_IN || '7d',
  mailMode: process.env.MAIL_MODE || 'log',
  smtpHost: process.env.SMTP_HOST || 'smtp.qq.com',
  smtpPort: Number(process.env.SMTP_PORT || 465),
  smtpSecure: String(process.env.SMTP_SECURE || 'true') !== 'false',
  smtpUser: process.env.SMTP_USER || '',
  smtpPass: process.env.SMTP_PASS || '',
  smtpFromName: process.env.SMTP_FROM_NAME || 'ChatRoom',
  resendApiKey: process.env.RESEND_API_KEY || '',
  resendFromEmail: process.env.RESEND_FROM_EMAIL || 'onboarding@resend.dev',
  exposeErrorDetail: String(process.env.EXPOSE_ERROR_DETAIL || '').trim() === '1'
    || String(process.env.NODE_ENV || '').trim().toLowerCase() !== 'production'
};

const constants = {
  kMessageRecallWindowMs: 2 * 60 * 1000,
  kRecalledSystemText: '\u4e00\u6761\u6d88\u606f\u5df2\u64a4\u56de',
  kTypingStateTtlMs: 6000,
  kMaxUploadBytes: 8 * 1024 * 1024,
  kUploadGcIntervalMs: 30 * 60 * 1000,
  kUploadGcGraceMs: 10 * 60 * 1000,
  kClientMessageIdMaxLength: 80,
  kAllowedUploadMimeTypes: new Set([
    'image/png', 'image/jpeg', 'image/gif', 'image/webp', 'image/bmp',
    'application/pdf', 'application/zip', 'text/plain'
  ]),
  kExtensionToMimeMap: new Map([
    ['.png', 'image/png'], ['.jpg', 'image/jpeg'], ['.jpeg', 'image/jpeg'],
    ['.gif', 'image/gif'], ['.webp', 'image/webp'], ['.bmp', 'image/bmp'],
    ['.pdf', 'application/pdf'], ['.zip', 'application/zip'],
    ['.txt', 'text/plain'], ['.log', 'text/plain'], ['.md', 'text/plain'],
    ['.json', 'text/plain'], ['.csv', 'text/plain']
  ]),
  kMimeToExtensionMap: new Map([
    ['image/png', '.png'], ['image/jpeg', '.jpg'], ['image/gif', '.gif'],
    ['image/webp', '.webp'], ['image/bmp', '.bmp'],
    ['application/pdf', '.pdf'], ['application/zip', '.zip'],
    ['text/plain', '.txt']
  ])
};

function validateStartupConfig(configToValidate = config) {
  const jwtSecret = String(configToValidate?.jwtSecret || '').trim();
  if (jwtSecret.length < 32) {
    throw new Error('JWT_SECRET is required and must be at least 32 characters long.');
  }
}

module.exports = { config, constants, validateStartupConfig };
