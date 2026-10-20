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
│ InhHarbourBuilder (town)     │                │                               │
│ InhCameraPawn (cine camera)  │                │                               │
│ InhAmbience (synth audio)    │                │                               │
└──────────────────────────────┘                └───────────────────────────────┘
                 ▲                                            ▲
                 └──── Game/Content/Data/Story/Season1.json ──┘
```

The **server is the source of truth**: it decides when you die, what you inherit, and what each action does, so nobody can cheat their estate by editing the app. The game client presents it.

## How the design maps to code

| Design | Where it lives |
| --- | --- |
| A life lasts until the next local sunrise | `server/src/sunrise.js` (NOAA sunrise algorithm). Players share only their UTC offset, not their location. Lives last at least 12 h. |
| At sunrise the Ferryman rows you out | The Sunrise cinematic (`World/InhCinematics.cpp`) with `InhHarbourBuilder::PlayFerryman` and `ShowMara`, played by `InhPlayerController::ShowSunrise`. |
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
- **`World/InhPlayerController`** runs the flow: Title (with the harbour looping behind it) → Opening cinematic → Will → a short camera move round the phone → Room (HUD) → Sunrise cinematic → Sunrise screen → the next life's Opening. It turns touches into camera orbit/zoom and prop taps, and a tap skips a cinematic. It owns the cinema overlay and the ambience, and keeps one clock for the whole world (`SetWorldProgress`: the room's sky, the harbour, the air, the soundscape and the rain, which each life's id decides).
- **`World/InhGameMode`** builds Hollow Bay at runtime: it spawns the room, the harbour and the atmosphere once at the world origin, and the camera at the room.
- **`World/InhLayout`** is the shared geography (the room, its window, the street, the quay, the pier, the lighthouse, the Ferryman's route). Every builder and every camera move is placed from it.
- **`World/InhRoomBuilder`** builds the room as a cutaway diorama (the floor slab and wall tops are drawn dark like an architect's section, over the fish market below) and lights the scene: sky atmosphere, volumetric height fog, a moon/dawn sun with light shafts, a sky light (blending photographed night and dawn skies when the art is installed), the flickering table lamp, and the film grade (filmic curve, teal shadows and warm highlights, bloom, lens flares, vignette, chromatic aberration, grain). It spawns one `AInhProp` per thing in the estate, and passes the clock on to the harbour and the atmosphere. The sky moves from dusk to dawn as your life runs out.
- **`World/InhHarbourBuilder`** builds the town outside: our building's facade down to the cobbled quay street, terraces of houses with lit windows, the quay and water steps, the pier and its moored boats rocking on the swell, crates and barrels, the lighthouse with its rotating beam, the far shore, the sea, stars and moon, and gulls. It also stages the dawn: the Ferryman's boat (`PlayFerryman`) and Mara at the steps (`ShowMara`). Repeated pieces share instanced components, and the town comes from a fixed seed, so it's the same on every launch.
- **`World/InhAtmosphereFX`** is the air: dust motes in the room (thickest in the window's light shaft at dawn), rain streaks outside, and drifting mist over the water when the fog card art is installed.
- **`World/InhCameraPawn`** is the only camera: a `UCineCameraComponent` on a Super 35 filmback with real lenses and depth of field. It orbits the room (drag, pinch, scroll) on a 40 mm lens, racks focus to a tapped prop, and flies authored shots with eased moves and slight handheld motion, blending back into the orbit afterwards (after the Sunrise it holds on the dawn behind the sunrise screen instead).
- **`World/InhCinematics`** holds the shots as plain data (`FInhShot`: camera and aim paths, focal length, aperture, easing, captions), built from `InhLayout`: `TitleAttract`, `Opening`, `WillReveal` and `Sunrise`. To change a camera move, edit it there; the controller adds the letterbox, title cards and skipping.
- **`UI/InhCinemaOverlay`** draws over cinematics: animated 2.39:1 letterbox bars, colour fades, title cards with widening letter spacing, and a "Tap to skip" hint. It never takes a touch.
- **`Audio/InhAmbience`** synthesises the soundscape as the game runs, with no audio assets: sea swell, wind, rigging creaks, a bell buoy, gulls at dawn, rain and the sunrise bell, quieter and windier at night.
- **`World/InhArt`** and **`World/InhShapes`** connect the code to the art pipeline. `InhArt` looks up imported materials, meshes and skies by their manifest key (`MI_<Key>`, `SM_<Key>`, `HDRI_<Key>`). `InhShapes` builds parts from them, or from flat-coloured basic shapes when they're missing. The pipeline itself is `tools/assets/` plus `Game/Content/Python/inh_build_art.py`; see [ART_PIPELINE.md](ART_PIPELINE.md).
- **`UI/`** holds every screen. They're built in C++ on top of `UInhScreen` so no Blueprint assets are needed. All of them use safe zones (for notches) and 56 px touch targets. The title and sunrise screens only shade the top and bottom edges, so the cinematics show through.

### Rendering on phones

The target is a mid-range phone: under 150 draw calls, 150k triangles and 4 shadow-casting lights. Anything repeated is one instanced component (the room's walls and furniture pieces of one look, the town's windows, boats, stars, dust and rain), and every glowing part skips shadows. Device profiles (`Config/DefaultDeviceProfiles.ini`) scale resolution, shadows, depth of field and bloom per phone tier. Desktop and the editor add Lumen, virtual shadow maps, TSR and volumetric fog (`Config/DefaultEngine.ini`).

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
