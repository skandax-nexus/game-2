// Builds web-preview/index.html from index.template.html and the season's story file, so the
// preview always tells the same story as the game and the server.
//   node web-preview/build.mjs          write index.html
//   node web-preview/build.mjs --check  exit 1 if index.html is out of date
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
const template = readFileSync(path.join(here, 'index.template.html'), 'utf8');
const story = JSON.parse(readFileSync(path.join(here, '../Game/Content/Data/Story/Season1.json'), 'utf8'));
const html = template.replace('/*STORY_JSON*/', () => JSON.stringify(story));
const out = path.join(here, 'index.html');

if (process.argv.includes('--check')) {
  const current = existsSync(out) ? readFileSync(out, 'utf8') : '';
  if (current !== html) {
    console.error('web-preview/index.html is out of date. Run: node web-preview/build.mjs');
    process.exit(1);
  }
  console.log('web-preview/index.html is up to date.');
} else {
  writeFileSync(out, html);
  console.log(`Wrote ${path.relative(process.cwd(), out)} (${Math.round(html.length / 1024)} KB)`);
}
