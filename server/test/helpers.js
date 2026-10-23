import http from 'node:http';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { openDb } from '../src/db.js';
import { loadStory } from '../src/story.js';
import { createApp } from '../src/app.js';

export function makeWav(seconds, sampleRate = 16000) {
  const dataBytes = Math.round(seconds * sampleRate) * 2;
  const buf = Buffer.alloc(44 + dataBytes);
  buf.write('RIFF', 0, 'ascii');
  buf.writeUInt32LE(36 + dataBytes, 4);
  buf.write('WAVE', 8, 'ascii');
  buf.write('fmt ', 12, 'ascii');
  buf.writeUInt32LE(16, 16);
  buf.writeUInt16LE(1, 20);
  buf.writeUInt16LE(1, 22);
  buf.writeUInt32LE(sampleRate, 24);
  buf.writeUInt32LE(sampleRate * 2, 28);
  buf.writeUInt16LE(2, 32);
  buf.writeUInt16LE(16, 34);
  buf.write('data', 36, 'ascii');
  buf.writeUInt32LE(dataBytes, 40);
  return buf;
}

export async function startServer(opts = {}) {
  const clock = { t: new Date(opts.start ?? '2026-10-20T08:00:00Z') };
  const handler = createApp({
    db: openDb(),
    story: loadStory(),
    dataDir: mkdtempSync(path.join(tmpdir(), 'inh-')),
    now: () => clock.t,
    seasonStart: new Date('2026-10-20T00:00:00Z'),
    ...opts,
  });
  const server = http.createServer(handler);
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;

  async function call(method, url, { token, json, body, headers = {} } = {}) {
    const res = await fetch(base + url, {
      method,
      headers: {
        ...(token ? { Authorization: `Bearer ${token}` } : {}),
        ...(json ? { 'Content-Type': 'application/json' } : {}),
        ...headers,
      },
      body: json ? JSON.stringify(json) : body,
    });
    const type = res.headers.get('content-type') ?? '';
    const data = type.includes('json') ? await res.json() : Buffer.from(await res.arrayBuffer());
    return { status: res.status, data };
  }

  async function register(name, extra = { lat: 51.5, lng: -0.12 }) {
    const { data } = await call('POST', '/v1/players', { json: { displayName: name, ...extra } });
    return data.token;
  }

  return {
    call,
    register,
    clock,
    advanceHours: (h) => (clock.t = new Date(clock.t.getTime() + h * 3600000)),
    close: () => new Promise((r) => server.close(r)),
  };
}
