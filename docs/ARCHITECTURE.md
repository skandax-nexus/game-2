# Architecture

```
 Phone (Unreal Engine 5)                         Server (Node.js 22 + SQLite)
┌──────────────────────────────┐   HTTPS/JSON   ┌───────────────────────────────┐
│ Screens (UMG, built in C++)  │ ─────────────▶ │ /v1/players  register         │
│ InhPlayerController (flow)   │                │ /v1/lives    inherit a life   │
│ InhGameSubsystem (state)     │ ◀───────────── │ /v1/lives/:id/actions|legacy  │
│ InhBackendSubsystem (HTTP)   │   WAV upload   │ /v1/lives/:id/voice-note      │
│ InhRoomBuilder (3D room)     │ ─────────────▶ │ /v1/season   acts, twists...  │
│ InhVoiceRecorder (mic)       │                │ sunrise sweep, estate pool    │
└──────────────────────────────┘                └───────────────────────────────┘
                 ▲                                            ▲
                 └──── Game/Content/Data/Story/Season1.json ──┘
```

The **server is the source of truth**: it decides when you die, what you inherit, and what each action does, so nobody can cheat their estate by editing the app. The game client presents it.

## How the design maps to code

| Design | Where it lives |
| --- | --- |
| A life lasts until the next local sunrise | `server/src/sunrise.js` (NOAA sunrise algorithm). Players share only their UTC offset, not their location. Lives last at least 12 h. |
| A random stranger inherits everything | `server/src/app.js` `inherit()`: picks from the pool of finished estates, never your own. |
| The Reading of the Will | `UI/InhWillScreen.cpp` |
| The 30-second voice note | `Audio/InhVoiceRecorder.cpp` (mic → 16 kHz WAV), uploaded and validated by `server/src/wav.js`. Only your heir can download it. Notes reported by 3 heirs are deleted. |
| ~10 actions per life | `server/src/game.js` `applyAction()`: pay/dodge debt, settle/start feud, chase clue, sell, deliver letter, bribe a Notary. |
| Decide your legacy: keep, sell, hide, destroy | `UI/InhLegacyScreen.cpp` and `game.js` `finalizeEstate()`. Hidden items appear as loose floorboards for the heir. |
| Notary Houses tax every inheritance | `finalizeEstate()` takes `notaryTaxPercent` (10%) at sunrise. |
| The Ledger and its pages | Clues can turn up pages (more likely in later acts). Selling a page to House Thrane or destroying it loses it for the whole city (`season.pagesLost`). |
| Three acts over 8 weeks; five twists | `Season1.json` `acts` and `twists`, revealed by day in `/v1/season` and shown in `UI/InhCityScreen.cpp`. |
| Keepers vs Breakers | Unlocked in Act 2. Chosen once per season. Ledger pages passed on by a faction member score for that faction. |
| Personal storylines | `estateTables.personalStories`: the unfinished letter (deliver or burn), the missing child (a clue), the family secret (opens after 5 heirs keep it), the revenge chain (forgive to end it). |
| Family tree | `/v1/me/family-tree` → `UI/InhLineageScreen.cpp` |
| Characters | `Season1.json` `characters`, shown in the City screen. |

## The client

- **`Core/InhGameSubsystem`** holds the player, the current life and the season, and broadcasts `OnLifeChanged`. Every screen redraws from that state.
- **`World/InhPlayerController`** runs the flow: Title → Will → Room (HUD) → Sunrise → next life. It turns touches into camera orbit/zoom and prop taps.
- **`World/InhRoomBuilder`** spawns the room, the harbour, the lighting (sky atmosphere, volumetric fog, moon/dawn sun, flickering lamp, rotating lighthouse beam, post-processing), and one `AInhProp` per thing in the estate. The sky moves from dusk to dawn as your life runs out.
- **`UI/`** holds every screen. They're built in C++ on top of `UInhScreen` so no Blueprint assets are needed. All of them use safe zones (for notches) and 56 px touch targets.

### Swapping in designer-made UI later

Every screen is a `UUserWidget` subclass. A designer can make a Widget Blueprint that inherits from, say, `InhHudScreen`. To keep the C++ layout but restyle it, edit `UI/InhStyle.cpp` (colours, fonts, button shapes) in one place.

## The server

- Zero dependencies: `node:http`, `node:sqlite` and `node:crypto`.
- **Lazy sunrise:** every request first ends any life whose `dies_at` has passed (`sweepDeaths`). No cron job is needed.
- **Deterministic outcomes:** each action's randomness is seeded from the life id and action number, so results can be reproduced and audited.
- Tests: `cd server && npm test` (18 tests covering sunrise maths, rules, and the full inherit → act → voice note → die → inherited-by-stranger loop over HTTP).

### Deploying

```bash
docker build -f server/Dockerfile -t inheritance-server .
docker run -p 8080:8080 -v inheritance-data:/data inheritance-server
```

Put it behind HTTPS (Caddy, nginx, or any managed host with a persistent volume), then set **Backend Url** to the https address. For more players than one SQLite file can handle, move `db.js` to Postgres. The queries are plain SQL.
