# Web preview

A playable browser version of Inheritance, for showing the game to people before the Unreal build is on their phone. Open `index.html` in a browser that supports WebGL 2. It's a single file: it loads three.js from jsDelivr and fonts from Google Fonts, and paints everything else in code.

## What it shows

- **Title:** a looping fly-over of Hollow Bay at night.
- **Opening shot:** in through the window, with letterbox and a title card.
- **The Reading of the Will:** your browser reads voice notes aloud where it can.
- **The room:** tap what glows, spend your 10 actions, and decide your legacy.
- **City and lineage:** the City tab shows the acts, twists, factions and characters; the Lineage tab lists every life you've lived.
- **Sunrise:** dawn rises, the Ferryman passes, and you wake in a stranger's life.
- **Sound:** generated sea, wind, a distant bell, and gulls at dawn.

It uses the real season file (`Game/Content/Data/Story/Season1.json`) and a JavaScript port of the server's rules (`server/src/game.js`, `server/src/rng.js`), so outcomes match the game.

## How it differs from the game

| | Phone game | Web preview |
| --- | --- | --- |
| Engine | Unreal Engine 5, photoscanned CC0 art | three.js, textures generated in code |
| A life lasts | until your local sunrise | 7 real minutes, or **Let the sun rise now** |
| Voice note | 30-second microphone recording | typed note (pages can't use the mic) |
| Heirs and predecessors | other players, through the server | simulated strangers |
| Progress | server | this browser only (`localStorage`) |
| Season | one real day per life | a Day slider in City to explore all 56 days |

## Editing

Edit `index.template.html`, then rebuild so the story file is embedded:

```bash
node web-preview/build.mjs           # writes index.html
node web-preview/build.mjs --check   # fails if index.html is stale (for CI)
```

If you change the rules in `server/src/game.js`, make the same change in the template's "Rules" section.

## Hosting

`index.html` is a static file. Serve it from GitHub Pages (Settings → Pages → deploy from a branch, folder `/web-preview`), Netlify, or any web server. Opening it straight from disk also works in most browsers.
