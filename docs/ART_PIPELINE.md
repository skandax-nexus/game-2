# Art pipeline

Inheritance builds its world in C++, so it always runs, even with no art at all: every surface falls back to an engine basic shape with a flat colour. One command swaps in real, photoscanned art (wood floors, plaster, cobbles, brick, furniture, crates, boats, night and dawn skies), all **CC0** from [Poly Haven](https://polyhaven.com). No game code changes and no manual Content Browser work are involved.

```
ArtManifest.json ──► fetch_cc0_assets.py ──► Game/RawAssets/PolyHaven/  (downloads + manifest.lock.json)
   (art keys)          (Poly Haven API)                │
                                                       ▼
                       inh_build_art.py (inside Unreal) ──► /Game/Inheritance/  (MI_<Key>, SM_<Key>, HDRI_<Key>)
                                                                     │
                       InhArt::Material / Mesh / Cubemap  ◄──────────┘  (C++, cached, nullptr until built)
```

## Build the art: one command

You need Unreal Engine 5.4 to 5.6 and Python 3.9 or newer. Close the editor first; the import runs headless.

```powershell
# Windows (PowerShell, from the repository folder)
.\tools\assets\build-art.ps1                         # or: -EnginePath "D:\Epic\UE_5.5"
```

```bash
# macOS / Linux
tools/assets/build-art.sh                           # or: --engine "/Users/Shared/Epic Games/UE_5.5"
```

The script does three things:

1. **Downloads** each art key's asset from Poly Haven with `tools/assets/fetch_cc0_assets.py`: textures (colour, OpenGL normal, and packed AO/roughness/metal "ARM", or roughness and AO separately), glTF models with their textures, and `.hdr` skies. Files go to `Game/RawAssets/PolyHaven/<kind>/<id>/`, a few hundred MB at 2k. Runs are resumable: files already on disk are skipped and interrupted downloads carry on, so you can re-run it any time.
2. **Builds the editor module** (`InheritanceEditor`) if it has never been built.
3. **Imports** everything with `Game/Content/Python/inh_build_art.py` in `UnrealEditor-Cmd -run=pythonscript`: textures with the right compression, meshes with LODs and collision, HDRI cubemaps, and the master materials and their instances, all under `/Game/Inheritance`. The first run compiles textures and shaders, so it takes a while.

Then open `Game/Inheritance.uproject` and press Play.

Build the art before you package for a phone, too: `tools/build/package-android.bat` and `tools/build/package-ios.sh` stop when it is missing (see [Packaged builds](#packaged-builds)).

| Option (`.sh` / `.ps1`) | What it does |
| --- | --- |
| `--engine <dir>` / `-EnginePath <dir>` | Engine folder that contains `Engine/`. Defaults to `$UE_ROOT`, else the launcher's install folder for 5.6 (`UE_VERSION=5.5` / `-EngineVersion 5.5` changes the version). `UE_EDITOR_CMD` points at the editor executable directly. |
| `--resolution 1k` / `-Resolution 1k` | Download size: `1k`, `2k` (default) or `4k`. If an asset doesn't offer that size, the nearest one is used. |
| `--only FloorWood,Table` / `-Only FloorWood,Table` | Rebuild only these keys. The master materials are always rebuilt. |
| `--skip-fetch` / `-SkipFetch` | Import what is already downloaded (no network). |
| `--skip-import` / `-SkipImport` | Only download. |

You can also run the steps by hand:

```bash
python3 tools/assets/fetch_cc0_assets.py --dry-run          # show which asset each key gets, and the URLs
python3 tools/assets/fetch_cc0_assets.py --only Night Dawn   # just the skies
python3 tools/assets/fetch_cc0_assets.py --self-test         # offline test of the selection logic
```

and run the importer from inside the editor with *Tools → Execute Python Script…* → `Game/Content/Python/inh_build_art.py`. `python3 Game/Content/Python/inh_build_art.py --offline-check` tests its non-Unreal parts.

If Poly Haven can't be reached (offline, or a proxy blocks `api.polyhaven.com`), the import still runs and builds every material with a flat, hand-picked colour, so the art build never leaves the game broken.

## What gets built

| Asset | Where | Notes |
| --- | --- | --- |
| `MI_<Key>` | `/Game/Inheritance/Materials` | One per material key, always written. If a download is missing, it uses the key's `fallback` colour and roughness. |
| `M_InhSurface`, `M_InhEmissive`, `M_InhWater`, `M_InhGlass`, `M_InhFogCard` | `/Game/Inheritance/Materials/Masters` | Authored node by node by the importer. |
| `SM_<Key>` | `/Game/Inheritance/Meshes` | Imported models: Nanite off, 4 LODs, at most 40k triangles at LOD 0, a simple box collision so taps hit them, and their own `M_InhSurface` instances using the model's textures. |
| `SM_WaterGrid` | `/Game/Inheritance/Meshes` | A generated 64×64-quad plane so the water's waves have vertices to move. |
| `HDRI_<Key>` | `/Game/Inheritance/Sky` | `TextureCube` skies (`Night`, `Dawn`) for sky lights and reflections. |
| Textures | `/Game/Inheritance/Textures`, `/Game/Inheritance/Meshes/Textures` | Colour maps are sRGB; normal maps use `TC_Normalmap` with the green channel flipped from OpenGL; ARM, roughness and AO maps are linear `TC_Masks`. |

`/Game/Inheritance` is always cooked (`DirectoriesToAlwaysCook` in `DefaultGame.ini`), because the game loads these assets by path. So that nothing stale ships, each run deletes what earlier runs left in `Textures`, `Meshes/Textures` and `Meshes/Materials` that nothing references any more (2k textures after a `--resolution 1k` rebuild, a swapped model's extra material instances, a roughness map once an ARM map replaces it), and logs each one. An `--only` run removes only those keys' own leftovers, and a run in which a key failed removes nothing. `MI_<Key>`, `SM_<Key>`, `HDRI_<Key>` and the masters are never deleted. Keep your own assets out of those three folders.

### How the game uses it

The game never references art directly. It asks for logical keys and keeps a primitive fallback:

```cpp
InhArt::Material(TEXT("FloorWood"));   // MI_FloorWood, or nullptr before the art build
InhArt::Mesh(TEXT("Table"));           // SM_Table, or nullptr
InhArt::Cubemap(TEXT("Night"));        // HDRI_Night, or nullptr
InhArt::IsArtInstalled();              // true once MI_FloorWood exists

// InhShapes does the fallback for you:
InhShapes::AddSurface(this, Root, InhShapes::Cube(), Loc, Size, TEXT("FloorWood"), FallbackColor);  // art material or flat colour
if (!InhShapes::AddArtMesh(this, Root, TEXT("Table"), FloorPoint, FVector(170, 100, 80)))           // scaled to fit, sits on the floor
{
    /* build the table from cubes, as before */
}
InhShapes::AddEmissive(...);  InhShapes::AddInstanced(...);  InhShapes::AddInstancedEmissive(...);
```

Lookups are cached (misses too) and never log warnings, so calling them every time something is built costs nothing. In the editor a miss is retried after a few seconds, so running the art build while the editor is open is picked up on the next Play.

## Licences

Every downloaded asset is **CC0 1.0 (public domain)**: free for commercial use, no attribution required, and you may modify and redistribute it. See [polyhaven.com/license](https://polyhaven.com/license). `Game/RawAssets/PolyHaven/manifest.lock.json` records, for every key, the Poly Haven asset id, its page, its authors and `"license": "CC0"`, so you can credit the artists in the game's credits (appreciated, not required) and audit exactly what ships.

The downloads themselves are not committed: `Game/RawAssets/PolyHaven/.gitignore` keeps everything except `manifest.lock.json` out of git, because anyone can fetch them again with the same ids. The imported `.uasset` files under `Game/Content/Inheritance` can be committed (use Git LFS) or rebuilt on each machine.

The textures the importer generates itself (the ripple normal map, fog noise, and default white and flat-normal textures) are made by code in this repository.

## Swapping art by key

Everything is addressed by the keys in [`Game/Content/Data/Art/ArtManifest.json`](../Game/Content/Data/Art/ArtManifest.json). Change what a key points at, re-run the art build with `--only <Key>`, and the game picks it up. No code changes.

**A different Poly Haven asset.** Browse [polyhaven.com](https://polyhaven.com), copy the id from the URL (`polyhaven.com/a/<id>`), and put it first in the key's `"prefer"` list. If none of the preferred ids exist, the downloader picks the best match for `"categories"` and `"keywords"`.

**Your own files.** Put them in `Game/RawAssets/Custom/<Key>/`. A custom folder always wins over the Poly Haven download.

- Materials: `<name>_diff.png` (or `_albedo`, `_basecolor`, `_color`), `<name>_nor_gl.png` (or `_normal`; use `_nor_dx` / `_normal_dx` for DirectX normals), and `<name>_arm.png` (AO, roughness, metal in R, G, B) or `<name>_rough.png`.
- Meshes: a `.gltf`, `.glb`, `.fbx` or `.obj` file plus its textures named as above. One mesh object per file; merge parts in your DCC tool first.
- Skies: one equirectangular `.hdr`.

**Fab / Quixel Megascans** (free inside UE 5.4+: *Window → Fab*). Add the asset to the project as usual, then point the key at it with `"unrealAsset"`:

```json
{ "key": "Cobble", "master": "Surface", "unrealAsset": "/Game/Fab/Megascans/Surfaces/Cobblestone_Pavement/MI_Cobblestone_Pavement", ... }
{ "key": "Crate", "unrealAsset": "/Game/Fab/Megascans/3D/Wooden_Crate/SM_Wooden_Crate", ... }
```

For a material, `MI_<Key>` becomes an instance of that material. Tick *Used with Instanced Static Meshes* (Details → Usage) on the material it is ultimately based on: most of the town is instanced, and a packaged build draws instances of a material without that flag with the default grid material. For a mesh or sky, it's duplicated to `SM_<Key>` / `HDRI_<Key>`. Megascans materials use the mesh's UVs rather than world-aligned mapping, so they suit meshes better than the code-built walls and floors; for surfaces, prefer downloading the Megascans texture set into `Game/RawAssets/Custom/<Key>/`. Use the mobile or low LOD variant and keep Nanite off.

**A new key.** Add an entry to the manifest, re-run the art build, and call `InhArt::Material("MyKey")` / `InhShapes::AddArtMesh(..., "MyKey", ...)` from code with a fallback.

## Materials

The importer authors the masters with exactly these parameters (code and instances rely on the names):

| Master | Use | Parameters |
| --- | --- | --- |
| `M_InhSurface` | Opaque lit surfaces: floors, walls, wood, stone, metal | `BaseColor`, `Normal`, `ORM` (textures); `Tint` (1,1,1), `UVScale` (1), `Metallic` (0), `RoughnessScale` (1); also `AOStrength`, `NormalStrength`, `UseMeshUVs` |
| `M_InhEmissive` | Lit windows, lamp glass, the lighthouse lens, stars (unlit glow; instanced copies vary in brightness) | `EmissiveColor`, `EmissiveStrength` (10) |
| `M_InhWater` | The sea: three world-position-offset swells, two panning ripple normal layers, fresnel | `DeepColor`, `ShallowColor`, `WaveHeight` (cm), `WaveSpeed`, `Roughness`; also `Normal` |
| `M_InhGlass` | Window panes, bottles | `Tint`, `Opacity` |
| `M_InhFogCard` | Drifting fog sheets (translucent, unlit, soft edges, depth fade) | `Color`, `Opacity`, `DriftSpeed` |

`M_InhSurface` is **world-aligned**: the walls and floors are scaled cubes, so mesh UVs would stretch. It projects the texture along the face's dominant axis, relative to the object so textures ride along with moving parts. `UVScale` is repeats per metre; the importer sets it so one repeat covers the scan's real-world size (from Poly Haven, or the key's `tileMeters`), times the key's `uvScale`. Imported models use the same master with `UseMeshUVs = 1`.

When the art isn't installed, `InhShapes` uses the engine's `BasicShapeMaterial` with a flat `Color`; emissive parts show their colour without glowing.

### Packaged builds

A packaged game can't compile shaders, so it draws an instanced mesh with the default grey grid material unless the mesh's material was saved with *Used with Instanced Static Meshes*. The masters have that flag. The engine's `BasicShapeMaterial` behind the flat colours may not, and the editor sets a missing flag on the fly, so Play in the editor never shows the problem. `InhShapes::CanInstance(Key)` tells the builders when a look can't be instanced in the running build, and they then:

- build those pieces of the room one by one (about seventy components at most), at the same place and size;
- build the harbour's flat-only pieces (dark window glass, the weed at the tide line) one by one, and leave the gulls out;
- without the art, also leave out the dust and the rain. The rest of the town is thousands of pieces, far too many for a component each, so it stays instanced and shows the grid material, and the log says so once.

With the art built, a packaged build looks as it does in the editor. That is why the packaging scripts stop when `Game/Content/Inheritance/Materials/MI_FloorWood.uasset` is missing; add `--allow-no-art` to package anyway (see [MOBILE.md](MOBILE.md)). The editor's *Platforms → Launch* doesn't check, so build the art before using it as well.

## Mobile budgets

The target is a mid-range phone (Android Vulkan / ES3.1, iOS Metal, mobile deferred renderer):

- **Under 150 draw calls and 150k triangles on screen.** Each art material is one shared instance (no per-part copies), and anything repeated more than four times (windows, planks, posts, stars, rain, dust) goes through `InhShapes::AddInstanced` / `AddInstancedEmissive`, one draw call per component.
- **Meshes:** Nanite off (phones don't run it), 4 LODs at 100/50/25/10%, LOD 0 capped at 40k triangles. Use `AddArtMesh` for close-up props only; distant props stay primitives.
- **Textures:** 2k downloads at most, with mipmaps, compressed to ASTC on mobile. Colour maps are in `TEXTUREGROUP_World`, normals in `TEXTUREGROUP_WorldNormalMap`, ARM/roughness in `TEXTUREGROUP_WorldSpecular` and skies in `TEXTUREGROUP_Skybox`, so a device profile can cap them per phone tier. None does by default (texture streaming keeps memory within the pool); to cap a tier at 1k, add for example:

  ```ini
  ; Config/DefaultDeviceProfiles.ini
  [Android_Mid DeviceProfile]
  +TextureLODGroups=(Group=TEXTUREGROUP_World,MinLODSize=1,MaxLODSize=1024,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage)
  +TextureLODGroups=(Group=TEXTUREGROUP_WorldNormalMap,MinLODSize=1,MaxLODSize=1024,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage)
  +TextureLODGroups=(Group=TEXTUREGROUP_WorldSpecular,MinLODSize=1,MaxLODSize=512,LODBias=0,MinMagFilter=aniso,MipFilter=point,MipGenSettings=TMGS_SimpleAverage)
  ```

  Or download smaller files with `--resolution 1k`.
- **Shaders:** every master has the instanced-mesh usage flag set (a cooked game otherwise shows the default material on instanced meshes; see [Packaged builds](#packaged-builds) for the flat colours). The water is opaque, and glass and fog are the only translucent materials; keep their cards few and small on screen.
- **Lights:** at most 4 shadow-casting lights. Emissive parts never cast shadows.
- **Volumetric fog** only runs on high-end and desktop; mobile falls back to height fog automatically.

## Troubleshooting

- **"Could not reach Poly Haven"**: check your internet connection or proxy (`HTTPS_PROXY` is honoured). The import still runs with flat materials; re-run once you're online.
- **"Unreal Editor not found"**: pass the engine folder (the one containing `Engine/`) with `--engine` / `-EnginePath`.
- **The import says modules are missing**: open `Game/Inheritance.uproject` once and let it build, or build `InheritanceEditor` from your IDE, then re-run with `--skip-fetch`.
- **"imported as N separate meshes; using the largest"**: that model is made of several objects. Merge them in Blender and drop the result in `Game/RawAssets/Custom/<Key>/`, or choose another asset.
- **Something looks flat**: the end of the import log lists keys that got the flat fallback and any problems, all prefixed `[InhArt]`.
- **Nothing changed in the editor after an import**: press Play again. The art lookups retry a few seconds after a miss.
- **A grey checkerboard town on the phone, but not in the editor**: the build was packaged without the art, or an art key points at a Fab material without *Used with Instanced Static Meshes*. The game log names the material. Build the art (or tick the flag) and package again; see [Packaged builds](#packaged-builds).
