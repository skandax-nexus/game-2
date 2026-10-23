import { randomUUID, randomBytes } from 'node:crypto';
import { mkdirSync, writeFileSync, createReadStream, existsSync, rmSync } from 'node:fs';
import path from 'node:path';
import { transaction, getSeason, setSeasonValue, addSeasonDelta } from './db.js';
import { actForDay, factionsUnlocked, seasonDay, unlockedTwists } from './story.js';
import { applyAction, finalizeEstate, generateStarterEstate, GameError, validateLegacy } from './game.js';
import { nextSunrise } from './sunrise.js';
import { createRng } from './rng.js';
import { parseWav } from './wav.js';

const JSON_LIMIT = 64 * 1024;
const VOICE_LIMIT = 3.2 * 1024 * 1024;
const REPORTS_TO_REMOVE = 3;

function readBody(req, limit) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(new GameError('too_large', 'Request body is too large.', 413));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

async function readJson(req) {
  const buf = await readBody(req, JSON_LIMIT);
  if (!buf.length) return {};
  try {
    return JSON.parse(buf.toString('utf8'));
  } catch {
    throw new GameError('invalid_json', 'Body must be valid JSON.');
  }
}

function send(res, status, body) {
  const json = JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Content-Length': Buffer.byteLength(json) });
  res.end(json);
}

const parse = (s) => JSON.parse(s);

/**
 * Builds the HTTP request handler. Everything time-dependent goes through `now()` so tests can travel in time.
 */
export function createApp({
  db,
  story,
  dataDir,
  now = () => new Date(),
  seasonStart,
  minLifeHours = 12,
  allowEarlySunrise = false,
  dayOverride,
}) {
  const voiceDir = path.join(dataDir, 'voice-notes');
  mkdirSync(voiceDir, { recursive: true });

  const stored = getSeason(db).startedAt;
  const start = seasonStart ?? (stored ? new Date(stored) : now());
  if (!stored || seasonStart) setSeasonValue(db, 'startedAt', start.toISOString());

  const currentDay = () => dayOverride ?? seasonDay(start, now());

  const q = {
    playerByToken: db.prepare('SELECT * FROM players WHERE token = ?'),
    player: db.prepare('SELECT * FROM players WHERE id = ?'),
    life: db.prepare('SELECT * FROM lives WHERE id = ?'),
    aliveLife: db.prepare("SELECT * FROM lives WHERE player_id = ? AND status = 'alive'"),
    dueLives: db.prepare("SELECT * FROM lives WHERE status = 'alive' AND dies_at <= ?"),
    livesOf: db.prepare('SELECT * FROM lives WHERE player_id = ? ORDER BY started_at'),
    countLivesOf: db.prepare('SELECT COUNT(*) AS n FROM lives WHERE player_id = ?'),
    estate: db.prepare('SELECT * FROM estates WHERE id = ?'),
    availableEstates: db.prepare(
      "SELECT * FROM estates WHERE status = 'available' AND (origin_player_id IS NULL OR origin_player_id != ?) ORDER BY created_at LIMIT 25",
    ),
    insertEstate: db.prepare(
      'INSERT INTO estates (id, payload, origin_life_id, origin_player_id, status, created_at) VALUES (?, ?, ?, ?, ?, ?)',
    ),
    claimEstate: db.prepare("UPDATE estates SET status = 'claimed' WHERE id = ? AND status = 'available'"),
    insertLife: db.prepare(
      `INSERT INTO lives (id, player_id, estate_id, predecessor_life_id, status, started_at, dies_at, actions_left, state)
       VALUES (?, ?, ?, ?, 'alive', ?, ?, ?, ?)`,
    ),
    setHeir: db.prepare('UPDATE lives SET heir_life_id = ? WHERE id = ?'),
    updateLifeState: db.prepare('UPDATE lives SET state = ?, actions_left = ?, log = ? WHERE id = ?'),
    updateLegacy: db.prepare('UPDATE lives SET legacy = ? WHERE id = ?'),
    killLife: db.prepare("UPDATE lives SET status = 'dead', dies_at = ? WHERE id = ?"),
    setVoiceNote: db.prepare('UPDATE lives SET voice_note = ?, voice_note_seconds = ? WHERE id = ?'),
    heirOf: db.prepare('SELECT * FROM lives WHERE predecessor_life_id = ? AND player_id = ?'),
    insertPlayer: db.prepare(
      'INSERT INTO players (id, token, display_name, lat, lng, faction, created_at) VALUES (?, ?, ?, ?, ?, NULL, ?)',
    ),
    updatePlayer: db.prepare('UPDATE players SET display_name = ?, lat = ?, lng = ? WHERE id = ?'),
    setFaction: db.prepare('UPDATE players SET faction = ? WHERE id = ?'),
    insertReport: db.prepare('INSERT INTO reports (life_id, reporter_id, reason, created_at) VALUES (?, ?, ?, ?)'),
    countReports: db.prepare('SELECT COUNT(DISTINCT reporter_id) AS n FROM reports WHERE life_id = ?'),
    countLives: db.prepare('SELECT status, COUNT(*) AS n FROM lives GROUP BY status'),
  };

  // ---- life cycle ------------------------------------------------------------------------------

  function endLife(row, at) {
    transaction(db, () => {
      const player = q.player.get(row.player_id);
      const season = getSeason(db);
      const { estate, seasonDelta } = finalizeEstate(parse(row.state), parse(row.legacy), {
        story,
        rng: createRng(`${row.id}:sunrise`),
        faction: player.faction,
        lifeId: row.id,
        displayName: player.display_name,
        season,
      });
      q.insertEstate.run(randomUUID(), JSON.stringify(estate), row.id, row.player_id, 'available', at);
      q.killLife.run(at, row.id);
      addSeasonDelta(db, seasonDelta);
    });
  }

  // Lives end lazily: any request first lets everyone whose sunrise has passed die.
  function sweepDeaths() {
    for (const row of q.dueLives.all(now().toISOString())) endLife(row, row.dies_at);
  }

  function serializeLife(row) {
    const day = currentDay();
    const act = actForDay(story, day);
    let predecessor = null;
    if (row.predecessor_life_id) {
      const pred = q.life.get(row.predecessor_life_id);
      const predPlayer = pred && q.player.get(pred.player_id);
      predecessor = {
        lifeId: row.predecessor_life_id,
        displayName: predPlayer?.display_name ?? 'A stranger',
        hasVoiceNote: Boolean(pred?.voice_note),
      };
    }
    const first = row.predecessor_life_id === null && q.livesOf.all(row.player_id)[0]?.id === row.id;
    return {
      id: row.id,
      status: row.status,
      startedAt: row.started_at,
      diesAt: row.dies_at,
      actionsLeft: row.actions_left,
      actionsMax: story.actionsPerLife,
      seasonDay: day,
      act: { id: act.id, name: act.name, summary: act.summary },
      predecessor,
      prologue: first ? story.prologue : null,
      estate: parse(row.state),
      legacy: parse(row.legacy),
      hasVoiceNote: Boolean(row.voice_note),
      voiceNoteMaxSeconds: story.voiceNoteMaxSeconds,
      log: parse(row.log),
    };
  }

  function ownedLife(player, id, { alive = true } = {}) {
    const row = q.life.get(id);
    if (!row || row.player_id !== player.id) throw new GameError('not_found', 'No such life.', 404);
    if (alive && row.status !== 'alive') throw new GameError('life_over', 'This life ended at sunrise.', 410);
    return row;
  }

  function inherit(player) {
    if (q.aliveLife.get(player.id)) throw new GameError('already_alive', 'You are still alive. Live this day first.', 409);
    const lifeId = randomUUID();
    const rng = createRng(lifeId);
    const at = now();
    return transaction(db, () => {
      const candidates = q.availableEstates.all(player.id);
      let estateRow = candidates.length ? rng.pick(candidates) : null;
      if (estateRow) {
        q.claimEstate.run(estateRow.id);
      } else {
        const payload = JSON.stringify(generateStarterEstate(story, rng));
        estateRow = { id: randomUUID(), payload, origin_life_id: null };
        q.insertEstate.run(estateRow.id, payload, null, null, 'claimed', at.toISOString());
      }
      const diesAt = nextSunrise(at, player.lat, player.lng, minLifeHours);
      q.insertLife.run(
        lifeId,
        player.id,
        estateRow.id,
        estateRow.origin_life_id,
        at.toISOString(),
        diesAt.toISOString(),
        story.actionsPerLife,
        estateRow.payload,
      );
      if (estateRow.origin_life_id) q.setHeir.run(lifeId, estateRow.origin_life_id);
      return q.life.get(lifeId);
    });
  }

  // ---- players ---------------------------------------------------------------------------------

  // Players may share only their UTC offset instead of a location; sunrise is then estimated from longitude.
  function locationFrom(body, fallback) {
    let lat = body.lat ?? fallback?.lat;
    let lng = body.lng ?? fallback?.lng;
    if ((lat === undefined || lng === undefined) && Number.isFinite(body.utcOffsetMinutes)) {
      lng = Math.max(-180, Math.min(180, body.utcOffsetMinutes / 4));
      lat = 20;
    }
    if (!Number.isFinite(lat) || !Number.isFinite(lng) || Math.abs(lat) > 90 || Math.abs(lng) > 180) {
      throw new GameError('invalid', 'Send lat/lng in degrees, or utcOffsetMinutes.');
    }
    return { lat, lng };
  }

  function displayNameFrom(value, fallback) {
    const name = String(value ?? '').trim().replace(/\s+/g, ' ').slice(0, 24);
    return name || fallback;
  }

  function serializePlayer(p) {
    return { id: p.id, displayName: p.display_name, faction: p.faction, createdAt: p.created_at };
  }

  function seasonView() {
    const day = currentDay();
    const act = actForDay(story, day);
    const s = getSeason(db);
    const counts = Object.fromEntries(q.countLives.all().map((r) => [r.status, r.n]));
    return {
      season: story.season,
      title: story.title,
      day,
      lengthDays: story.lengthDays,
      act: { id: act.id, name: act.name, summary: act.summary },
      factionsUnlocked: factionsUnlocked(story, day),
      factions: story.factions.map((f) => ({ ...f, score: s[`${f.id}Score`] ?? 0 })),
      twists: unlockedTwists(story, day),
      ledger: { pagesFound: s.pagesFound ?? 0, pagesLost: s.pagesLost ?? 0 },
      citizens: { alive: counts.alive ?? 0, departed: counts.dead ?? 0 },
    };
  }

  // ---- routing ---------------------------------------------------------------------------------

  const routes = [];
  const route = (method, pattern, auth, handler) => routes.push({ method, pattern, auth, handler });

  route('GET', /^\/health$/, false, () => ({ ok: true }));
  route('GET', /^\/v1\/season$/, false, () => seasonView());

  route('POST', /^\/v1\/players$/, false, async ({ req }) => {
    const body = await readJson(req);
    const { lat, lng } = locationFrom(body);
    const id = randomUUID();
    const token = randomBytes(24).toString('base64url');
    const name = displayNameFrom(body.displayName, `Citizen ${1000 + Math.floor(Math.random() * 9000)}`);
    q.insertPlayer.run(id, token, name, lat, lng, now().toISOString());
    return [201, { token, player: serializePlayer(q.player.get(id)) }];
  });

  route('GET', /^\/v1\/me$/, true, ({ player }) => {
    const alive = q.aliveLife.get(player.id);
    return {
      player: serializePlayer(player),
      currentLife: alive ? serializeLife(alive) : null,
      livesLived: q.countLivesOf.get(player.id).n,
    };
  });

  route('PATCH', /^\/v1\/me$/, true, async ({ req, player }) => {
    const body = await readJson(req);
    const { lat, lng } = locationFrom(body, player);
    q.updatePlayer.run(displayNameFrom(body.displayName, player.display_name), lat, lng, player.id);
    return { player: serializePlayer(q.player.get(player.id)) };
  });

  route('POST', /^\/v1\/me\/faction$/, true, async ({ req, player }) => {
    const { faction } = await readJson(req);
    if (!factionsUnlocked(story, currentDay())) throw new GameError('too_early', 'The city has not split yet.', 409);
    if (player.faction) throw new GameError('already_chosen', 'You chose your side for this season.', 409);
    if (!story.factions.some((f) => f.id === faction)) throw new GameError('invalid', 'Choose keepers or breakers.');
    q.setFaction.run(faction, player.id);
    return { player: serializePlayer(q.player.get(player.id)) };
  });

  route('GET', /^\/v1\/me\/family-tree$/, true, ({ player }) => {
    const nameOfLifeOwner = (lifeId) => {
      const l = lifeId && q.life.get(lifeId);
      return l ? q.player.get(l.player_id)?.display_name ?? null : null;
    };
    const lives = q.livesOf.all(player.id).map((l) => ({
      lifeId: l.id,
      status: l.status,
      startedAt: l.started_at,
      diesAt: l.dies_at,
      house: parse(l.state).house?.name ?? '',
      inheritedFrom: nameOfLifeOwner(l.predecessor_life_id),
      passedTo: nameOfLifeOwner(l.heir_life_id),
      hasVoiceNote: Boolean(l.voice_note),
    }));
    return { player: serializePlayer(player), lives };
  });

  route('POST', /^\/v1\/lives$/, true, ({ player }) => [201, { life: serializeLife(inherit(player)) }]);

  route('GET', /^\/v1\/lives\/current$/, true, ({ player }) => {
    const alive = q.aliveLife.get(player.id);
    if (!alive) throw new GameError('no_life', 'You are between lives. Inherit a new one.', 404);
    return { life: serializeLife(alive) };
  });

  route('POST', /^\/v1\/lives\/([\w-]+)\/actions$/, true, async ({ req, player, params }) => {
    const action = await readJson(req);
    const row = ownedLife(player, params[0]);
    if (row.actions_left <= 0) throw new GameError('no_actions', 'You have no actions left today.', 409);
    const state = parse(row.state);
    const used = story.actionsPerLife - row.actions_left;
    const { message, seasonDelta } = applyAction(state, action, {
      story,
      act: actForDay(story, currentDay()),
      rng: createRng(`${row.id}:${used}`),
      season: getSeason(db),
    });
    const log = [...parse(row.log), message];
    transaction(db, () => {
      q.updateLifeState.run(JSON.stringify(state), row.actions_left - 1, JSON.stringify(log), row.id);
      addSeasonDelta(db, seasonDelta);
    });
    return { message, life: serializeLife(q.life.get(row.id)) };
  });

  route('PUT', /^\/v1\/lives\/([\w-]+)\/legacy$/, true, async ({ req, player, params }) => {
    const body = await readJson(req);
    const row = ownedLife(player, params[0]);
    const decisions = validateLegacy(parse(row.state), body.decisions);
    q.updateLegacy.run(JSON.stringify(decisions), row.id);
    return { life: serializeLife(q.life.get(row.id)) };
  });

  route('PUT', /^\/v1\/lives\/([\w-]+)\/voice-note$/, true, async ({ req, player, params }) => {
    const row = ownedLife(player, params[0]);
    const buf = await readBody(req, VOICE_LIMIT);
    let info;
    try {
      info = parseWav(buf);
    } catch (err) {
      throw new GameError('invalid_audio', `Voice note must be a 16-bit PCM WAV: ${err.message}.`);
    }
    if (info.durationSeconds > story.voiceNoteMaxSeconds + 0.5) {
      throw new GameError('too_long', `Voice notes are at most ${story.voiceNoteMaxSeconds} seconds.`);
    }
    const file = `${row.id}.wav`;
    writeFileSync(path.join(voiceDir, file), buf);
    q.setVoiceNote.run(file, info.durationSeconds, row.id);
    return { life: serializeLife(q.life.get(row.id)) };
  });

  route('GET', /^\/v1\/lives\/([\w-]+)\/voice-note$/, true, ({ player, params, res }) => {
    const row = q.life.get(params[0]);
    const allowed = row && (row.player_id === player.id || q.heirOf.get(row.id, player.id));
    if (!allowed) throw new GameError('not_found', 'No such voice note.', 404);
    const file = row.voice_note && path.join(voiceDir, row.voice_note);
    if (!file || !existsSync(file)) throw new GameError('not_found', 'This life left no voice note.', 404);
    res.writeHead(200, { 'Content-Type': 'audio/wav' });
    createReadStream(file).pipe(res);
    return undefined;
  });

  route('POST', /^\/v1\/lives\/([\w-]+)\/voice-note\/report$/, true, async ({ req, player, params }) => {
    const { reason } = await readJson(req);
    const row = q.life.get(params[0]);
    if (!row || !q.heirOf.get(row.id, player.id)) throw new GameError('not_found', 'No such voice note.', 404);
    q.insertReport.run(row.id, player.id, String(reason ?? 'unspecified').slice(0, 200), now().toISOString());
    if (q.countReports.get(row.id).n >= REPORTS_TO_REMOVE && row.voice_note) {
      rmSync(path.join(voiceDir, row.voice_note), { force: true });
      q.setVoiceNote.run(null, null, row.id);
    }
    return [202, { ok: true }];
  });

  route('POST', /^\/v1\/lives\/([\w-]+)\/sunrise$/, true, ({ player, params }) => {
    if (!allowEarlySunrise) throw new GameError('forbidden', 'The sun rises when it rises.', 403);
    const row = ownedLife(player, params[0]);
    endLife(row, now().toISOString());
    return { life: serializeLife(q.life.get(row.id)) };
  });

  return async function handle(req, res) {
    try {
      const url = new URL(req.url, 'http://localhost');
      const matches = routes.filter((r) => r.pattern.test(url.pathname));
      if (!matches.length) throw new GameError('not_found', 'Nothing here.', 404);
      const r = matches.find((m) => m.method === req.method);
      if (!r) throw new GameError('method_not_allowed', 'Method not allowed.', 405);

      sweepDeaths();
      let player = null;
      if (r.auth) {
        const token = (req.headers.authorization ?? '').replace(/^Bearer\s+/i, '');
        player = token && q.playerByToken.get(token);
        if (!player) throw new GameError('unauthorized', 'Missing or invalid token.', 401);
      }
      const params = r.pattern.exec(url.pathname).slice(1);
      const result = await r.handler({ req, res, player, params, url });
      if (result === undefined) return;
      const [status, body] = Array.isArray(result) ? result : [200, result];
      send(res, status, body);
    } catch (err) {
      if (err instanceof GameError) {
        send(res, err.status, { error: { code: err.code, message: err.message } });
      } else {
        console.error(err);
        send(res, 500, { error: { code: 'internal', message: 'Something went wrong in Hollow Bay.' } });
      }
    }
  };
}
