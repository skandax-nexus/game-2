# Inheritance

> Every life lasts exactly one real day, and a random stranger inherits whatever you leave behind.

A mobile mystery game for **Android and iOS**, built with **Unreal Engine 5**. It's set in Hollow Bay, a foggy port city under a curse where no one lives past one sunrise. You inherit a stranger's money, debts, house, enemies and secrets, along with a 30-second voice note, and you have one day to decide what your own heir gets.

The design document this implements is summarised in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#how-the-design-maps-to-code).

## What's in this repository

| Folder | What it is |
| --- | --- |
| `Game/` | The Unreal Engine 5 project (`Inheritance.uproject`). It's all C++: the gameplay, every UI screen, a lit, foggy 3D diorama of your inherited room, the whole harbour town outside it, the cinematic camera and a synthesised soundscape. It runs without any Blueprint or `.uasset` setup. |
| `Game/Content/Data/Story/Season1.json` | The whole season's story: acts, twists, characters, factions, endings and the tables used to generate estates. The game and the server both read it. |
| `server/` | The online backend (Node.js 22, no dependencies). It handles lives, sunrise timing, passing an estate to a stranger, voice notes, factions and the Ledger. It comes with 18 automated tests. |
| `tools/setup/` | One-command installers for everything you need: `setup-windows.ps1` (for Android) and `setup-macos.sh` (for iOS). |
| `tools/build/` | Scripts that package the game for Android (`.apk`) and iOS (`.ipa`). |
| `tools/assets/` | The art pipeline: one command downloads free CC0 photoscanned art from Poly Haven and imports it into the project (`build-art.sh` / `build-art.ps1`). |
| `docs/` | Setup, architecture, mobile builds, art pipeline and roadmap. |

## Quick start

1. **Install the tools.** On Windows, run `tools\setup\setup-windows.ps1` in an admin PowerShell. On a Mac, run `tools/setup/setup-macos.sh`. See [docs/SETUP.md](docs/SETUP.md).
2. **Start the server.**
   ```bash
   cd server
   ALLOW_EARLY_SUNRISE=1 npm start     # Windows PowerShell: $env:ALLOW_EARLY_SUNRISE=1; npm start
   ```
3. **Build the art (optional, recommended).** With the editor closed, run `tools/assets/build-art.sh` (macOS/Linux) or `.\tools\assets\build-art.ps1` (Windows). It downloads CC0 textures, models and skies and imports them; see [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md). Skip it and the game still runs in the editor, drawn with simple shapes. The phone packaging scripts require it (or `--allow-no-art`).
4. **Open the game.** Double-click `Game/Inheritance.uproject`. Click **Yes** when it asks to build the C++ module, then press **Play**.
5. **Put it on a phone.** Follow [docs/MOBILE.md](docs/MOBILE.md).

## How a life plays

1. **Inherit.** The Reading of the Will shows what you got and plays your predecessor's voice note.
2. **Investigate.** Drag to look around the room, pinch to zoom, and tap the glowing things: demands pinned to the wall, crossed-out portraits of rivals, clues on the corkboard, a loose floorboard, the phone.
3. **Act.** You get 10 actions: pay or dodge a debt, settle or start a feud, follow a clue (and maybe find a page of the Ledger), sell, deliver a letter, or bribe a Notary.
4. **Decide your legacy.** Keep, sell, hide or destroy each thing you own.
5. **Record the voice note.** You get 30 seconds for your heir: honest advice, a warning, or a lie.

At your local sunrise the Ferryman rows you out, the Notaries take their 10% tax, and your estate waits for a stranger to inherit it.

## Cinematic visuals

The game is staged like a film, on phones as well as in the editor:

- **A real camera.** One cine camera (Super 35 filmback, 24 to 85 mm lenses, physically based depth of field) orbits the room on a 40 mm lens and racks focus to whatever you tap. It drifts gently when idle and has a slight handheld feel.
- **Cinematics.** The harbour plays on a slow loop behind the title screen. Each life opens with a flight over the bay, past the lighthouse and in through your window, under a "HOLLOW BAY" title card with the day and the act. After the will, the camera circles the phone with your predecessor's voice note. At sunrise it leans out of the window as the dawn comes up, follows the Ferryman's boat past Mara at the water steps, then fades to white. Apart from the title loop, cinematics are letterboxed and a tap (or Space on desktop) skips them.
- **A whole town.** Outside the window, Hollow Bay is built in code: the cobbled quay street and its houses with lit windows, the quay and water steps, a pier with boats rocking on the swell, the lighthouse sweeping its beam, gulls, the far shore climbing its hills, stars and a moon that fade at dawn. Dust floats in the window light, rain comes and goes, and mist drifts over the water.
- **A film grade.** A filmic tone curve with teal shadows and warm highlights, bloom, lens flares, vignette, a trace of chromatic aberration and fine grain, cooler at night and warmer at dusk and dawn. On desktop there is also Lumen lighting, virtual shadow maps and volumetric fog. Phones get mobile depth of field, bloom and per-device quality profiles.
- **Sound with no audio files.** The sea, wind, creaking rigging, a bell buoy, gulls at dawn, rain and the sunrise bell are all synthesised as the game runs (`Audio/InhAmbience`).
- **Real art, one command.** `tools/assets/build-art.sh` (or `.\tools\assets\build-art.ps1` on Windows) downloads CC0 photoscanned textures, furniture, props and night and dawn skies from Poly Haven, and imports them with mobile-friendly settings. The code picks them up by name, and keeps a simple-shape fallback for anything that isn't installed. Details are in [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md).

## Status: please read

- **The Unreal C++ has not been compiled yet.** It was written in an environment with no Unreal Engine (the engine needs an Epic account and Windows or macOS). It targets UE 5.4–5.6 APIs. Expect a few small compile fixes on the first build. The server, by contrast, is tested and runs.
- **Without the art build, the world is drawn with simple shapes.** The lighting, camera, cinematics, town and grade all work either way, but the photoscanned look needs `tools/assets/build-art` once (see [Cinematic visuals](#cinematic-visuals)). The art pipeline and the cinematic code haven't run inside Unreal yet either, so expect a few fixes there too.
- See [docs/ROADMAP.md](docs/ROADMAP.md) for what comes next (the live finale event, push notifications at sunrise, shareable family-tree images, and more).
