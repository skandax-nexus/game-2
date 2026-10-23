import http from 'node:http';
import path from 'node:path';
import { mkdirSync } from 'node:fs';
import { openDb } from './db.js';
import { loadStory } from './story.js';
import { createApp } from './app.js';

const env = process.env;
const dataDir = path.resolve(env.DATA_DIR ?? 'data');
mkdirSync(dataDir, { recursive: true });

const handler = createApp({
  db: openDb(path.join(dataDir, 'inheritance.sqlite')),
  story: loadStory(env.STORY_FILE),
  dataDir,
  seasonStart: env.SEASON_START ? new Date(env.SEASON_START) : undefined,
  minLifeHours: env.MIN_LIFE_HOURS ? Number(env.MIN_LIFE_HOURS) : 12,
  allowEarlySunrise: env.ALLOW_EARLY_SUNRISE === '1',
  dayOverride: env.SEASON_DAY ? Number(env.SEASON_DAY) : undefined,
});

const port = Number(env.PORT ?? 8080);
const host = env.HOST ?? '0.0.0.0';
http.createServer(handler).listen(port, host, () => {
  console.log(`Hollow Bay is listening on http://${host}:${port}`);
});
