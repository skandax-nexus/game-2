# Inheritance

> Every life lasts exactly one real day, and a random stranger inherits whatever you leave behind.

A mobile mystery game for **Android and iOS**, built with **Unreal Engine 5**. It's set in Hollow Bay, a foggy port city under a curse where no one lives past one sunrise. You inherit a stranger's money, debts, house, enemies and secrets, along with a 30-second voice note, and you have one day to decide what your own heir gets.

The design document this implements is summarised in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md#how-the-design-maps-to-code).

## What's in this repository

| Folder | What it is |
| --- | --- |
| `Game/` | The Unreal Engine 5 project (`Inheritance.uproject`). It's all C++: the gameplay, every UI screen, and a lit, foggy 3D diorama of your inherited room with the harbour and lighthouse outside. It runs without any Blueprint or `.uasset` setup. |
| `Game/Content/Data/Story/Season1.json` | The whole season's story: acts, twists, characters, factions, endings and the tables used to generate estates. The game and the server both read it. |
| `server/` | The online backend (Node.js 22, no dependencies). It handles lives, sunrise timing, passing an estate to a stranger, voice notes, factions and the Ledger. It comes with 18 automated tests. |
| `tools/setup/` | One-command installers for everything you need: `setup-windows.ps1` (for Android) and `setup-macos.sh` (for iOS). |
| `tools/build/` | Scripts that package the game for Android (`.apk`) and iOS (`.ipa`). |
| `docs/` | Setup, architecture, mobile builds, art pipeline and roadmap. |

## Quick start

1. **Install the tools.** On Windows, run `tools\setup\setup-windows.ps1` in an admin PowerShell. On a Mac, run `tools/setup/setup-macos.sh`. See [docs/SETUP.md](docs/SETUP.md).
2. **Start the server.**
   ```bash
   cd server
   ALLOW_EARLY_SUNRISE=1 npm start     # Windows PowerShell: $env:ALLOW_EARLY_SUNRISE=1; npm start
   ```
3. **Open the game.** Double-click `Game/Inheritance.uproject`. Click **Yes** when it asks to build the C++ module, then press **Play**.
4. **Put it on a phone.** Follow [docs/MOBILE.md](docs/MOBILE.md).

## How a life plays

1. **Inherit.** The Reading of the Will shows what you got and plays your predecessor's voice note.
2. **Investigate.** Drag to look around the room, pinch to zoom, and tap the glowing things: demands pinned to the wall, crossed-out portraits of rivals, clues on the corkboard, a loose floorboard, the phone.
3. **Act.** You get 10 actions: pay or dodge a debt, settle or start a feud, follow a clue (and maybe find a page of the Ledger), sell, deliver a letter, or bribe a Notary.
4. **Decide your legacy.** Keep, sell, hide or destroy each thing you own.
5. **Record the voice note.** You get 30 seconds for your heir: honest advice, a warning, or a lie.

At your local sunrise the Ferryman rows you out, the Notaries take their 10% tax, and your estate waits for a stranger to inherit it.

## Status: please read

- **The Unreal C++ has not been compiled yet.** It was written in an environment with no Unreal Engine (the engine needs an Epic account and Windows or macOS). It targets UE 5.4–5.6 APIs. Expect a few small compile fixes on the first build. The server, by contrast, is tested and runs.
- **The visuals are placeholder geometry.** Everything is lit for real, with fog, volumetric light, a moving lighthouse beam, bloom and colour grading, but the room is built from the engine's basic shapes. To get the "high graphical" look, swap in real art: [docs/ART_PIPELINE.md](docs/ART_PIPELINE.md) shows where each mesh is created and how to replace it with free Fab/Quixel assets.
- See [docs/ROADMAP.md](docs/ROADMAP.md) for what comes next (the live finale event, push notifications at sunrise, shareable family-tree images, and more).
