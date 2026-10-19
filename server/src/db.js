import { DatabaseSync } from 'node:sqlite';

const SCHEMA = `
CREATE TABLE IF NOT EXISTS players (
  id TEXT PRIMARY KEY,
  token TEXT NOT NULL UNIQUE,
  display_name TEXT NOT NULL,
  lat REAL NOT NULL,
  lng REAL NOT NULL,
  faction TEXT,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS estates (
  id TEXT PRIMARY KEY,
  payload TEXT NOT NULL,
  origin_life_id TEXT,
  origin_player_id TEXT,
  status TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS estates_available ON estates(status, created_at);
CREATE TABLE IF NOT EXISTS lives (
  id TEXT PRIMARY KEY,
  player_id TEXT NOT NULL REFERENCES players(id),
  estate_id TEXT NOT NULL REFERENCES estates(id),
  predecessor_life_id TEXT,
  heir_life_id TEXT,
  status TEXT NOT NULL,
  started_at TEXT NOT NULL,
  dies_at TEXT NOT NULL,
  actions_left INTEGER NOT NULL,
  state TEXT NOT NULL,
  legacy TEXT NOT NULL DEFAULT '{}',
  log TEXT NOT NULL DEFAULT '[]',
  voice_note TEXT,
  voice_note_seconds REAL
);
CREATE INDEX IF NOT EXISTS lives_player ON lives(player_id, status);
CREATE INDEX IF NOT EXISTS lives_dying ON lives(status, dies_at);
CREATE TABLE IF NOT EXISTS season (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS reports (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  life_id TEXT NOT NULL,
  reporter_id TEXT NOT NULL,
  reason TEXT NOT NULL,
  created_at TEXT NOT NULL
);
`;

export function openDb(file = ':memory:') {
  const db = new DatabaseSync(file);
  db.exec('PRAGMA journal_mode = WAL; PRAGMA foreign_keys = ON;');
  db.exec(SCHEMA);
  return db;
}

export function transaction(db, fn) {
  db.exec('BEGIN');
  try {
    const result = fn();
    db.exec('COMMIT');
    return result;
  } catch (err) {
    db.exec('ROLLBACK');
    throw err;
  }
}

export function getSeason(db) {
  const rows = db.prepare('SELECT key, value FROM season').all();
  return Object.fromEntries(rows.map((r) => [r.key, JSON.parse(r.value)]));
}

export function setSeasonValue(db, key, value) {
  db.prepare('INSERT INTO season (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value').run(
    key,
    JSON.stringify(value),
  );
}

export function addSeasonDelta(db, delta) {
  const season = getSeason(db);
  for (const [key, n] of Object.entries(delta ?? {})) setSeasonValue(db, key, (season[key] ?? 0) + n);
}
