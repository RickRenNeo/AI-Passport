/**
 * WeChat CloudBase / Tencent Cloud SCF HTTP Cloud Function reference template
 * implementing the exact zero-retention ephemeral relay API used by AI Passport.
 *
 * Deployment notes:
 * - Bind a CloudBase HTTP Trigger (free tier included) to this function.
 * - Messages live in CloudBase DB/Redis with a 24h TTL index and are immediately
 *   deleted inside `/api/v1/device/ack` and `/api/v1/client/ack` (zero retention).
 */

'use strict';

const crypto = require('crypto');

const TTL_SEC = 86400;
const ephemeralQueue = {
  toDevice: new Map(), // device_id -> Map<remote_id, msg>
  toClient: new Map(), // remote_id -> msg
  acls: new Map(),     // device_id -> acl
};

function purgeExpired() {
  const cutoff = Math.floor(Date.now() / 1000) - TTL_SEC;
  for (const [, bucket] of ephemeralQueue.toDevice) {
    for (const [rid, msg] of bucket) {
      if (msg.created_epoch < cutoff) bucket.delete(rid);
    }
  }
  for (const [rid, msg] of ephemeralQueue.toClient) {
    if (msg.created_epoch < cutoff) ephemeralQueue.toClient.delete(rid);
  }
}

function isSenderAuthorized(acl, role, phone, school, className) {
  if (!acl) return false;
  if (role === 'primary_parent') {
    return Boolean(phone && acl.primary_phone && phone === acl.primary_phone);
  }
  if (role === 'family') {
    if (!phone) return false;
    if (phone === acl.primary_phone) return true;
    return (acl.family || []).some((m) => m.phone === phone);
  }
  if (role === 'teacher') {
    if (acl.student_school && school !== acl.student_school) return false;
    if (acl.student_class && className !== acl.student_class) return false;
    const list = acl.teachers || [];
    if (list.length > 0) {
      return list.some(
        (t) =>
          (!t.school || t.school === school) &&
          (!t.class_name || t.class_name === className) &&
          (!phone || !t.phone || t.phone === phone)
      );
    }
    return Boolean(school && className);
  }
  if (role === 'school_admin') {
    if (acl.student_school && school !== acl.student_school) return false;
    const list = acl.admins || [];
    if (list.length > 0) {
      return list.some(
        (a) =>
          (!a.school || a.school === school) &&
          (!phone || !a.phone || a.phone === phone)
      );
    }
    return Boolean(school);
  }
  return false;
}

function jsonResp(statusCode, body) {
  return {
    statusCode,
    headers: { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' },
    body: JSON.stringify(body),
  };
}

exports.main = async (event) => {
  purgeExpired();
  const method = (event.httpMethod || 'GET').toUpperCase();
  const path = event.path || '/';
  const qs = event.queryStringParameters || {};
  const nowEpoch = Math.floor(Date.now() / 1000);

  if (method === 'POST' && path === '/api/v1/device/poll') {
    const body = JSON.parse(event.body || '{}');
    const devId = body.device_id || 'unknown';
    const acl = ephemeralQueue.acls.get(devId) || {
      primary_phone: body.primary_phone || '',
      student_school: body.school || '',
      student_class: body.class_name || '',
      family: [],
      teachers: [],
      admins: [],
    };
    ephemeralQueue.acls.set(devId, acl);
    const bucket = ephemeralQueue.toDevice.get(devId) || new Map();
    const messages = Array.from(bucket.values()).map(({ audioBase64, ...meta }) => meta);
    return jsonResp(200, { ok: true, server_time: nowEpoch, acl, messages });
  }

  if (method === 'GET' && path.startsWith('/api/v1/device/audio/')) {
    const rid = path.slice('/api/v1/device/audio/'.length);
    const bucket = ephemeralQueue.toDevice.get(qs.device_id || '') || new Map();
    const msg = bucket.get(rid);
    if (!msg) return jsonResp(404, { ok: false, error: 'not_found_or_acked' });
    return {
      statusCode: 200,
      isBase64Encoded: true,
      headers: { 'Content-Type': 'application/octet-stream' },
      body: msg.audioBase64,
    };
  }

  if (method === 'POST' && path === '/api/v1/device/ack') {
    const body = JSON.parse(event.body || '{}');
    const bucket = ephemeralQueue.toDevice.get(body.device_id || '');
    let deleted = 0;
    if (bucket) {
      for (const rid of body.remote_ids || []) {
        if (bucket.delete(rid)) deleted += 1;
      }
    }
    return jsonResp(200, { ok: true, deleted, server_time: nowEpoch });
  }

  if (method === 'POST' && path === '/api/v1/device/upload') {
    const audioBuf = event.isBase64Encoded
      ? Buffer.from(event.body || '', 'base64')
      : Buffer.from(event.body || '', 'binary');
    const remoteId = 'up_' + crypto.randomBytes(5).toString('hex');
    const durationSec = Number(qs.duration_sec || 1);
    const text = `学生已发送 ${durationSec} 秒语音留言`;
    ephemeralQueue.toClient.set(remoteId, {
      remote_id: remoteId,
      device_id: qs.device_id || 'unknown',
      student_name: qs.student_name || '学生',
      duration_sec: durationSec,
      audioBase64: audioBuf.toString('base64'),
      text,
      created_epoch: nowEpoch,
    });
    return jsonResp(200, { ok: true, remote_id: remoteId, text, server_time: nowEpoch });
  }

  if (method === 'POST' && path === '/api/v1/client/send') {
    const body = JSON.parse(event.body || '{}');
    const devId = body.device_id || 'unknown';
    const acl = ephemeralQueue.acls.get(devId);
    if (
      !isSenderAuthorized(
        acl,
        body.sender_role,
        body.sender_phone,
        body.sender_school,
        body.sender_class
      )
    ) {
      return jsonResp(403, { ok: false, error: 'forbidden' });
    }
    const rid = 'm_' + crypto.randomBytes(5).toString('hex');
    if (!ephemeralQueue.toDevice.has(devId)) ephemeralQueue.toDevice.set(devId, new Map());
    ephemeralQueue.toDevice.get(devId).set(rid, {
      remote_id: rid,
      sender_role: body.sender_role,
      sender_name: body.sender_name || '家长',
      sender_phone: body.sender_phone || '',
      sender_school: body.sender_school || '',
      sender_class: body.sender_class || '',
      text: body.text || '',
      duration_sec: Number(body.duration_sec || 3),
      audio_format: 'adpcm_8k',
      audioBase64: body.adpcm_base64 || '',
      created_epoch: nowEpoch,
    });
    return jsonResp(200, { ok: true, remote_id: rid, server_time: nowEpoch });
  }

  if (method === 'POST' && path === '/api/v1/client/ack') {
    const body = JSON.parse(event.body || '{}');
    let deleted = 0;
    for (const rid of body.remote_ids || []) {
      if (ephemeralQueue.toClient.delete(rid)) deleted += 1;
    }
    return jsonResp(200, { ok: true, deleted });
  }

  return jsonResp(404, { ok: false, error: 'unknown_route' });
};
