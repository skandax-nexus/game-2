import test from 'node:test';
import assert from 'node:assert/strict';
import { startServer, makeWav } from './helpers.js';

test('a full life: inherit, act, decide legacy, record a voice note, die, and be inherited by a stranger', async () => {
  const s = await startServer();
  try {
    const ana = await s.register('Ana');
    const ben = await s.register('Ben', { utcOffsetMinutes: 330 });

    let r = await s.call('POST', '/v1/lives', { token: ana });
    assert.equal(r.status, 201);
    const life = r.data.life;
    assert.equal(life.actionsLeft, 10);
    assert.ok(life.prologue, 'first life starts with the prologue');
    assert.equal(life.predecessor, null);
    const hours = (new Date(life.diesAt) - s.clock.t) / 3600000;
    assert.ok(hours >= 12 && hours <= 36);

    r = await s.call('POST', '/v1/lives', { token: ana });
    assert.equal(r.status, 409);

    const clue = life.estate.clues[0];
    r = await s.call('POST', `/v1/lives/${life.id}/actions`, { token: ana, json: { type: 'chase_clue', clueId: clue.id } });
    assert.equal(r.status, 200);
    assert.equal(r.data.life.actionsLeft, 9);
    assert.equal(r.data.life.log.length, 1);

    const item = r.data.life.estate.items[0];
    r = await s.call('PUT', `/v1/lives/${life.id}/legacy`, { token: ana, json: { decisions: { [item.id]: 'hide' } } });
    assert.equal(r.status, 200);

    r = await s.call('PUT', `/v1/lives/${life.id}/voice-note`, { token: ana, body: makeWav(31) });
    assert.equal(r.data.error?.code, 'too_long');
    r = await s.call('PUT', `/v1/lives/${life.id}/voice-note`, { token: ana, body: Buffer.from('hello') });
    assert.equal(r.data.error?.code, 'invalid_audio');
    r = await s.call('PUT', `/v1/lives/${life.id}/voice-note`, { token: ana, body: makeWav(12) });
    assert.equal(r.status, 200);
    assert.equal(r.data.life.hasVoiceNote, true);

    // Ben can't hear Ana's note until he inherits from her.
    r = await s.call('GET', `/v1/lives/${life.id}/voice-note`, { token: ben });
    assert.equal(r.status, 404);

    // Sunrise passes. Ana's life ends on the next request.
    s.advanceHours(40);
    r = await s.call('GET', '/v1/me', { token: ana });
    assert.equal(r.data.currentLife, null);

    r = await s.call('POST', '/v1/lives', { token: ben });
    const benLife = r.data.life;
    assert.equal(benLife.predecessor.displayName, 'Ana');
    assert.equal(benLife.predecessor.hasVoiceNote, true);
    assert.equal(benLife.prologue, null);
    assert.ok(benLife.estate.items.find((i) => i.id === item.id).hidden);

    r = await s.call('GET', `/v1/lives/${life.id}/voice-note`, { token: ben });
    assert.equal(r.status, 200);
    assert.equal(r.data.subarray(0, 4).toString(), 'RIFF');

    // Ana never inherits her own estate; she gets a fresh one.
    r = await s.call('POST', '/v1/lives', { token: ana });
    assert.equal(r.data.life.predecessor, null);

    r = await s.call('GET', '/v1/me/family-tree', { token: ana });
    assert.equal(r.data.lives.length, 2);
    assert.equal(r.data.lives[0].passedTo, 'Ben');
  } finally {
    await s.close();
  }
});

test('actions run out after ten', async () => {
  const s = await startServer();
  try {
    const t = await s.register('Cy');
    const { data } = await s.call('POST', '/v1/lives', { token: t });
    const id = data.life.id;
    for (let i = 0; i < 10; i++) {
      const r = await s.call('POST', `/v1/lives/${id}/actions`, { token: t, json: { type: 'start_feud', rival: `rival ${i}` } });
      assert.equal(r.status, 200);
    }
    const r = await s.call('POST', `/v1/lives/${id}/actions`, { token: t, json: { type: 'start_feud', rival: 'one more' } });
    assert.equal(r.data.error.code, 'no_actions');
  } finally {
    await s.close();
  }
});

test('factions open in Act 2 and are chosen once', async () => {
  const early = await startServer({ dayOverride: 3 });
  try {
    const t = await early.register('Dee');
    const r = await early.call('POST', '/v1/me/faction', { token: t, json: { faction: 'keepers' } });
    assert.equal(r.data.error.code, 'too_early');
  } finally {
    await early.close();
  }

  const s = await startServer({ dayOverride: 20 });
  try {
    const t = await s.register('Eli');
    let r = await s.call('POST', '/v1/me/faction', { token: t, json: { faction: 'breakers' } });
    assert.equal(r.data.player.faction, 'breakers');
    r = await s.call('POST', '/v1/me/faction', { token: t, json: { faction: 'keepers' } });
    assert.equal(r.data.error.code, 'already_chosen');
    r = await s.call('GET', '/v1/season');
    assert.equal(r.data.act.name, 'The Debt');
    assert.equal(r.data.twists.length, 1);
  } finally {
    await s.close();
  }
});

test('early sunrise is only for development servers', async () => {
  const s = await startServer();
  try {
    const t = await s.register('Fen');
    const { data } = await s.call('POST', '/v1/lives', { token: t });
    const r = await s.call('POST', `/v1/lives/${data.life.id}/sunrise`, { token: t });
    assert.equal(r.status, 403);
  } finally {
    await s.close();
  }
  const dev = await startServer({ allowEarlySunrise: true });
  try {
    const t = await dev.register('Gil');
    const { data } = await dev.call('POST', '/v1/lives', { token: t });
    const r = await dev.call('POST', `/v1/lives/${data.life.id}/sunrise`, { token: t });
    assert.equal(r.data.life.status, 'dead');
  } finally {
    await dev.close();
  }
});

test('requests need a valid token', async () => {
  const s = await startServer();
  try {
    assert.equal((await s.call('GET', '/v1/me')).status, 401);
    assert.equal((await s.call('GET', '/v1/me', { token: 'nope' })).status, 401);
    assert.equal((await s.call('GET', '/health')).data.ok, true);
  } finally {
    await s.close();
  }
});
