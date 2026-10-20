# Building for Android and iOS

The project is already configured for both (`Game/Config/DefaultEngine.ini`):

- **Portrait**, full screen, notch-safe UI
- **Android:** package `com.skandaxnexus.inheritance`, arm64, Vulkan with OpenGL ES 3.1 fallback, min SDK 26 (Android 8), target SDK 35 (what Google Play requires), microphone permission
- **iOS:** bundle `com.skandaxnexus.inheritance`, Metal, iPhone and iPad, microphone usage text
- **Mobile deferred renderer** with HDR, TAA and dynamic shadows, plus per-device quality profiles (`DefaultDeviceProfiles.ini`)

Change the package and bundle IDs to your own before publishing.

**Build the art first** (`tools/assets/build-art`, see [ART_PIPELINE.md](ART_PIPELINE.md)). Without it, a packaged build may draw most of the town in the engine's grey grid material, even though Play in the editor looks fine: the flat-colour fallback relies on an engine material that may not be saved for instanced meshes. The packaging scripts below stop when the art is missing; add `--allow-no-art` to package anyway. The editor's **Launch** button doesn't check.

## 1. Point the game at a reachable server

A phone can't reach `127.0.0.1` on your PC. Pick one:

- **Same Wi-Fi:** find your PC's IP (`ipconfig` / `ifconfig`) and set *Project Settings → Game → Inheritance → Backend Url* to `http://192.168.x.x:8080`. Allow Node.js through the firewall.
- **Anywhere:** deploy the server (see ARCHITECTURE.md) and use its `https://` URL. **Required for store builds.**

## 2. Android

On Windows (or a Mac with Android set up):

1. On the phone, enable *Developer options → USB debugging* and connect it by USB.
2. **Quick test from the editor:** Platforms toolbar → **Android** → your device → **Launch**.
3. **Installable build:**
   ```bat
   tools\build\package-android.bat "C:\Program Files\Epic Games\UE_5.6" Development
   ```
   It stops if the art hasn't been built (add `--allow-no-art` to package without it). Then run `Builds\Android\Android_ASTC\Install_Inheritance-arm64.bat` with the phone plugged in.
4. **Google Play:** use the `Shipping` configuration, set `bEnableBundle=True` (App Bundle), create a signing key (*Project Settings → Android → Distribution Signing*), and upload the `.aab` in the Play Console.

## 3. iOS (Mac only)

1. In Xcode, sign in with your Apple ID (*Settings → Accounts*).
2. In Unreal, open *Project Settings → Platforms → iOS*. Tick **Automatic Signing** and enter your **Team ID**, or import a certificate and provisioning profile.
3. **Quick test:** connect the iPhone → Platforms → **iOS** → device → **Launch**.
4. **Installable build:**
   ```bash
   tools/build/package-ios.sh "/Users/Shared/Epic Games/UE_5.6" Development
   ```
   It stops if the art hasn't been built (add `--allow-no-art` to package without it).
5. **TestFlight / App Store:** package with `Shipping`, then upload with Xcode Organizer or Transporter.

Before shipping, remove the `NSAppTransportSecurity` block from `AdditionalPlistData` in `DefaultEngine.ini` and use an https server. It's there only so dev builds can reach a LAN server over plain http.

## Performance targets

| Device class | Profile | Target |
| --- | --- | --- |
| Low-end Android (2020) | `Android_Low` | 30 fps at 720p (`r.MobileContentScaleFactor=1.0`), one shadow cascade, no depth of field or lens flares |
| Mid Android | `Android_Mid` | 30–60 fps at 1.25x (about 900p), two cascades, depth of field and bloom |
| Flagship Android | `Android_High` | 60 fps at 1.5x (1080p), two 2048 cascades, full post-processing |
| iPhone / iPad | `IOS` | 60 fps at 2.0x (Retina), two 2048 cascades, full post-processing |

Every tier keeps dynamic shadows: the shaft of dawn light through the window is the game's key image. All of this is set in `Game/Config/DefaultDeviceProfiles.ini`.

Profile on device with `stat unit` and `stat gpu` (four-finger tap opens the console in Development builds).
