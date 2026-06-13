'use strict';

const assert = require('node:assert/strict');

const {
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
} = require('../src/server');

function run(name, fn) {
  try {
    fn();
    console.log(`PASS ${name}`);
  } catch (error) {
    console.error(`FAIL ${name}`);
    console.error(error);
    process.exitCode = 1;
  }
}

run('sanitizeMessagePageLimit keeps value within [1, 100]', () => {
  assert.equal(sanitizeMessagePageLimit(undefined), 50);
  assert.equal(sanitizeMessagePageLimit(0), 1);
  assert.equal(sanitizeMessagePageLimit(-10), 1);
  assert.equal(sanitizeMessagePageLimit(30), 30);
  assert.equal(sanitizeMessagePageLimit(300), 100);
  assert.equal(sanitizeMessagePageLimit('abc'), 50);
});

run('sanitizeMessageId keeps non-negative integers', () => {
  assert.equal(sanitizeMessageId(undefined), 0);
  assert.equal(sanitizeMessageId(''), 0);
  assert.equal(sanitizeMessageId(-1), 0);
  assert.equal(sanitizeMessageId(12.8), 12);
  assert.equal(sanitizeMessageId('77'), 77);
  assert.equal(sanitizeMessageId('abc', 9), 9);
});

run('sanitizeClientMessageId trims and caps length', () => {
  assert.equal(sanitizeClientMessageId(undefined), '');
  assert.equal(sanitizeClientMessageId('   '), '');
  assert.equal(sanitizeClientMessageId('  abc-123  '), 'abc-123');
  assert.equal(
    sanitizeClientMessageId('x'.repeat(120)).length,
    80
  );
});

run('sanitizeSearchLimit keeps value within [1, 100]', () => {
  assert.equal(sanitizeSearchLimit(undefined), 20);
  assert.equal(sanitizeSearchLimit(0), 1);
  assert.equal(sanitizeSearchLimit(-3), 1);
  assert.equal(sanitizeSearchLimit(55), 55);
  assert.equal(sanitizeSearchLimit(1000), 100);
  assert.equal(sanitizeSearchLimit('abc'), 20);
});

run('paginateMessageRows returns asc rows and pagination metadata', () => {
  const rowsInDesc = [
    { id: 109 },
    { id: 108 },
    { id: 107 },
    { id: 106 },
    { id: 105 },
    { id: 104 }
  ];

  const page = paginateMessageRows(rowsInDesc, 5);
  assert.equal(page.hasMore, true);
  assert.equal(page.nextBeforeId, 105);
  assert.deepEqual(page.rows.map((row) => row.id), [105, 106, 107, 108, 109]);
});

run('paginateMessageRows handles empty and non-overflow pages', () => {
  const smallPage = paginateMessageRows([{ id: 103 }, { id: 102 }], 5);
  assert.equal(smallPage.hasMore, false);
  assert.equal(smallPage.nextBeforeId, 102);
  assert.deepEqual(smallPage.rows.map((row) => row.id), [102, 103]);

  const emptyPage = paginateMessageRows([], 5);
  assert.equal(emptyPage.hasMore, false);
  assert.equal(emptyPage.nextBeforeId, null);
  assert.deepEqual(emptyPage.rows, []);
});

run('validateStartupConfig rejects missing or short JWT secret', () => {
  assert.throws(() => validateStartupConfig({ jwtSecret: '' }), /JWT_SECRET is required/);
  assert.throws(() => validateStartupConfig({ jwtSecret: 'short_secret' }), /JWT_SECRET is required/);
  assert.doesNotThrow(() => validateStartupConfig({
    jwtSecret: '12345678901234567890123456789012'
  }));
});

run('canRecallMessageCreatedAt enforces two-minute recall window', () => {
  const baseNow = Date.parse('2026-04-29T10:00:00.000Z');
  assert.equal(
    canRecallMessageCreatedAt('2026-04-29T09:58:30.000Z', baseNow),
    true
  );
  assert.equal(
    canRecallMessageCreatedAt('2026-04-29T09:57:59.999Z', baseNow),
    false
  );
  assert.equal(
    canRecallMessageCreatedAt('2026-04-29T10:00:01.000Z', baseNow),
    false
  );
  assert.equal(
    canRecallMessageCreatedAt('invalid-date', baseNow),
    false
  );
});

run('sanitizeTypingState normalizes common truthy/falsey values', () => {
  assert.equal(sanitizeTypingState(true), true);
  assert.equal(sanitizeTypingState(false), false);
  assert.equal(sanitizeTypingState(1), true);
  assert.equal(sanitizeTypingState(0), false);
  assert.equal(sanitizeTypingState('true'), true);
  assert.equal(sanitizeTypingState('YES'), true);
  assert.equal(sanitizeTypingState('1'), true);
  assert.equal(sanitizeTypingState('false'), false);
  assert.equal(sanitizeTypingState('0'), false);
  assert.equal(sanitizeTypingState('random'), false);
});

run('sanitizeUploadFileName strips unsafe characters and path segments', () => {
  assert.equal(sanitizeUploadFileName(''), 'upload.bin');
  assert.equal(sanitizeUploadFileName('../../a?.png'), 'a_.png');
  assert.equal(sanitizeUploadFileName('C:\\\\tmp\\\\hello world.txt'), 'hello world.txt');
});

run('normalizeFriendPairIds sorts and validates friend pair ids', () => {
  assert.deepEqual(normalizeFriendPairIds(9, 3), { lowUserId: 3, highUserId: 9 });
  assert.deepEqual(normalizeFriendPairIds('12', '34'), { lowUserId: 12, highUserId: 34 });
  assert.equal(normalizeFriendPairIds(0, 2), null);
  assert.equal(normalizeFriendPairIds(8, 8), null);
  assert.equal(normalizeFriendPairIds('x', 2), null);
});

run('inferUploadMimeType prefers binary signatures over claimed/extension types', () => {
  const pngBytes = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00]);
  assert.equal(inferUploadMimeType(pngBytes, 'photo.txt', 'text/plain'), 'image/png');

  const pdfBytes = Buffer.from('%PDF-1.7\n', 'ascii');
  assert.equal(inferUploadMimeType(pdfBytes, 'a.jpg', 'image/jpeg'), 'application/pdf');
});

run('inferUploadMimeType falls back to text/plain for likely text payload', () => {
  const textBytes = Buffer.from('hello, chatroom', 'utf8');
  assert.equal(inferUploadMimeType(textBytes, 'readme.txt', ''), 'text/plain');
  assert.equal(inferUploadMimeType(textBytes, 'readme.unknown', 'text/plain'), 'text/plain');
});

run('upload mime whitelist and stored extension mapping stay consistent', () => {
  assert.equal(isAllowedUploadMimeType('image/jpeg'), true);
  assert.equal(isAllowedUploadMimeType('application/octet-stream'), false);
  assert.equal(chooseStoredFileExtension('evil.exe', 'image/jpeg'), '.jpg');
  assert.equal(chooseStoredFileExtension('notes.log', 'text/plain'), '.txt');
});

if (!process.exitCode) {
  console.log('All backend helper tests passed.');
}
