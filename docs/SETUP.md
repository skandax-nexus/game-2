# Setting up your machine

You need **Windows** to build for Android, and a **Mac** to build for iOS (Apple requires Xcode). A Mac can build both.

## What gets installed

| Tool | Why | Size |
| --- | --- | --- |
| Unreal Engine 5.6 (via Epic Games Launcher) | The game engine | ~60 GB with Android/iOS support |
| Visual Studio 2022 + "Game development with C++" (Windows) | Compiles the C++ | ~25 GB |
| Xcode (Mac) | Compiles for iOS, signs the app | ~15 GB |
| Android Studio + SDK + NDK | Builds and installs Android apps | ~10 GB |
| Node.js 22 LTS | Runs the Inheritance server | small |
| Git + Git LFS | Version control for code and large art files | small |

Plan for **about 120 GB free disk**, 16 GB+ RAM, and a GPU that supports DirectX 12 or Metal.

## Windows (Android)

```powershell
# In PowerShell opened with "Run as administrator", from the repository folder:
Set-ExecutionPolicy -Scope Process Bypass
.\tools\setup\setup-windows.ps1            # or: -EngineVersion 5.5
```

The script installs everything with `winget`. It **pauses twice** for steps that need you:

1. **Unreal Engine.** Sign in to the Epic Games Launcher, then go to *Unreal Engine → Library → + → 5.6 → Install*. Under **Options**, tick **Android** (and **iOS** if you have a Mac for signing).
2. **Android Studio.** Open it once, finish the Standard setup, then go to *More Actions → SDK Manager → SDK Tools* and tick **Android SDK Command-line Tools (latest)**.

After that it runs Unreal's own `SetupAndroid.bat`, which installs the exact NDK version your engine expects, then runs the server tests and generates `Game/Inheritance.sln`. **Restart Windows** afterwards so the new environment variables apply.

## macOS (iOS, and Android too)

```bash
chmod +x tools/setup/setup-macos.sh
./tools/setup/setup-macos.sh 5.6
```

It installs Homebrew, Git, Node, Xcode (from the App Store), Android Studio and the Epic Games Launcher, and pauses for the Unreal Engine install (tick **iOS** under Options). For device builds you also need an [Apple Developer account](https://developer.apple.com/programs/).

## First run

1. **Start the server**:
   ```bash
   cd server
   ALLOW_EARLY_SUNRISE=1 npm start
   ```
   You should see `Hollow Bay is listening on http://0.0.0.0:8080`. `ALLOW_EARLY_SUNRISE=1` turns on the **Skip to sunrise** button so you can test a whole life in a few minutes. Leave it off in production.
2. **Open the project.** Double-click `Game/Inheritance.uproject`. If your engine version differs, right-click it, choose *Switch Unreal Engine version*, and pick yours. Answer **Yes** to "rebuild modules".
3. **Press Play** (Alt+P). The mouse acts as a finger: drag to orbit, scroll to zoom, click to tap.

### Testing inheritance with two players

Inheritance passes estates between *different* players. To see it work on one machine:

1. Play a life, record a voice note in **Legacy**, then press **Skip to sunrise**.
2. Stop Play. Delete `Game/Saved/SaveGames/InheritanceProfile.sav` so the next session registers as a new player.
3. Press Play again. The new player inherits your previous estate and can play your voice note.

## Changing settings

*Edit → Project Settings → Game → Inheritance*:

- **Backend Url**: where the server runs. On a phone, use your computer's LAN IP, for example `http://192.168.1.20:8080`.
- **Show Dev Tools**: shows the Skip-to-sunrise button. It's always hidden in Shipping builds.

Server environment variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `PORT` | `8080` | HTTP port |
| `DATA_DIR` | `./data` | SQLite database and voice notes |
| `SEASON_START` | first launch | ISO date the 56-day season began |
| `SEASON_DAY` | — | Pretend it's this day of the season (for example `20` to test Act 2 factions) |
| `MIN_LIFE_HOURS` | `12` | A life always lasts at least this long, even if sunrise is near |
| `ALLOW_EARLY_SUNRISE` | off | `1` enables the dev "Skip to sunrise" endpoint |

## Troubleshooting

- **"Can't reach Hollow Bay"** on the title screen means the server isn't running, or the Backend Url is wrong. On a phone, make sure it's on the same Wi-Fi and that Windows Firewall allows Node.js on private networks.
- **"Could not be compiled. Try rebuilding from source manually."** Open `Game/Inheritance.sln` (Windows) or the Xcode workspace (Mac), build the `InheritanceEditor` target, and read the first error. The code hasn't been through a compiler yet, so a few small fixes may be needed.
- **Android packaging fails on SDK/NDK.** Re-run `Engine\Extras\Android\SetupAndroid.bat`, restart, and check *Project Settings → Platforms → Android SDK*.
