"""Builds Inheritance's art inside Unreal: imports the CC0 downloads and authors the materials.

Everything lands under /Game/Inheritance, named by the logical keys in Content/Data/Art/ArtManifest.json,
which is where the game looks (Source/Inheritance/World/InhArt.cpp):

  /Game/Inheritance/Materials/MI_<Key>        one instance per material key (flat parameters if a download is missing)
  /Game/Inheritance/Materials/Masters/M_Inh*  M_InhSurface, M_InhEmissive, M_InhWater, M_InhGlass, M_InhFogCard
  /Game/Inheritance/Meshes/SM_<Key>           imported models, plus the generated SM_WaterGrid
  /Game/Inheritance/Sky/HDRI_<Key>            HDRI skies as TextureCube
  /Game/Inheritance/Textures/...              the textures they use

/Game/Inheritance is always cooked, so each run also deletes what earlier runs left in Textures, Meshes/Textures and
Meshes/Materials that nothing references any more (an --only run: just those keys' own leftovers).

Run it inside the editor (Tools > Execute Python Script...) or headless:
  UnrealEditor-Cmd <repo>/Game/Inheritance.uproject -run=pythonscript -script=inh_build_art.py
(the bare file name is found on the Python plugin's path, which holds <Project>/Content/Python; a full path breaks
when it contains spaces). tools/assets/build-art.sh / build-art.ps1 download the assets first and then run exactly that.

Options (arguments after the script path, or environment variables for the headless run):
  --only FloorWood,Table   INH_ART_ONLY      rebuild only these keys (masters and built-in textures always rebuild)
  --lock <path>            INH_ART_LOCK      a different manifest.lock.json

Without Unreal, `python3 inh_build_art.py --offline-check` tests the parts that don't need the editor.
Uses only Unreal Python APIs available in UE 5.4-5.6; optional ones are guarded.
"""

import argparse
import array
import json
import math
import os
import random
import re
import struct
import sys
import tempfile
import zlib
from pathlib import Path

try:
    import unreal  # only exists inside the Unreal Editor
except ImportError:
    unreal = None

ROOT = "/Game/Inheritance"
MATERIALS = ROOT + "/Materials"
MASTERS = ROOT + "/Materials/Masters"
TEXTURES = ROOT + "/Textures"
BUILTIN_TEXTURES = ROOT + "/Textures/Builtin"
MESHES = ROOT + "/Meshes"
MESH_MATERIALS = ROOT + "/Meshes/Materials"
MESH_TEXTURES = ROOT + "/Meshes/Textures"
SKY = ROOT + "/Sky"
# Raw imports land outside the always-cooked root first, so leftovers never ship; deleted at the end.
STAGING = "/Game/InheritanceImport"

IMAGE_EXTS = (".jpg", ".jpeg", ".png", ".tga", ".exr", ".tif", ".tiff", ".bmp")
MODEL_EXTS = (".gltf", ".glb", ".fbx", ".obj")
# Imported models are capped so a 200k-triangle scan can't blow the phone budget (docs/ART_PIPELINE.md).
MAX_MESH_TRIANGLES = 40000

# Map tokens in texture file names (Poly Haven style: <set>_<map>_<res>.jpg), longest first.
MAP_TOKENS = sorted([
    ("nor_gl", "normal"), ("nor_dx", "normal"), ("normal_gl", "normal"), ("normal_dx", "normal"), ("normal", "normal"),
    ("nor", "normal"), ("nrm", "normal"),
    ("diff", "diffuse"), ("diffuse", "diffuse"), ("albedo", "diffuse"), ("basecolor", "diffuse"), ("base_color", "diffuse"),
    ("col", "diffuse"), ("color", "diffuse"),
    ("arm", "arm"), ("orm", "arm"),
    ("rough", "roughness"), ("roughness", "roughness"),
    ("ao", "ao"), ("metal", "metal"), ("metallic", "metal"),
    ("spec", "other"), ("disp", "other"), ("displacement", "other"), ("bump", "other"),
    ("alpha", "opacity"), ("opacity", "opacity"), ("emission", "emissive"), ("emissive", "emissive"),
], key=lambda item: -len(item[0]))


# =============================================================================================
# Pure Python (no Unreal): file generation and parsing, covered by --offline-check
# =============================================================================================

def project_dir() -> Path:
    """Game/ (the folder holding Inheritance.uproject)."""
    here = globals().get("__file__")
    if here:
        candidate = Path(here).resolve().parents[2]
        if (candidate / "Inheritance.uproject").exists() or unreal is None:
            return candidate
    return Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def load_json(path: Path):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def png_bytes(width: int, height: int, channels: int, rows) -> bytes:
    """A minimal 8-bit PNG (grey, RGB or RGBA) from rows of raw bytes."""
    color_type = {1: 0, 3: 2, 4: 6}[channels]
    raw = bytearray()
    for row in rows:
        if len(row) != width * channels:
            raise ValueError("PNG row has the wrong length")
        raw.append(0)  # filter: none
        raw.extend(row)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")


def solid_png(rgb, size: int = 4) -> bytes:
    row = bytes(rgb) * size
    return png_bytes(size, size, 3, [row] * size)


def _tileable_waves(seed: int, count: int, min_k: float, max_k: int, falloff: float):
    """Sine waves with integer frequencies, so their sum tiles seamlessly."""
    rng = random.Random(seed)
    waves = []
    while len(waves) < count:
        kx, ky = rng.randint(-max_k, max_k), rng.randint(-max_k, max_k)
        k = math.hypot(kx, ky)
        if min_k <= k <= max_k:
            waves.append((kx, ky, rng.uniform(0.0, 2.0 * math.pi), 1.0 / (k ** falloff)))
    return waves


def _wave_tables(waves, size: int):
    """Per-axis sin/cos tables: sin(A + B) is then a multiply-add per pixel instead of a trig call."""
    tables = []
    for kx, ky, phase, amp in waves:
        a = [2.0 * math.pi * kx * x / size + phase for x in range(size)]
        b = [2.0 * math.pi * ky * y / size for y in range(size)]
        tables.append((kx, ky, amp, [math.sin(v) for v in a], [math.cos(v) for v in a],
                       [math.sin(v) for v in b], [math.cos(v) for v in b]))
    return tables


def _value_noise(size: int, period: int, rng) -> list:
    """Smooth value noise on a lattice that wraps every `period` cells, so it tiles when period divides size."""
    lattice = [[rng.random() for _ in range(period)] for _ in range(period)]
    cell = size / float(period)
    xs = []
    for x in range(size):
        fx = x / cell
        x0 = int(fx)
        t = fx - x0
        xs.append((x0 % period, (x0 + 1) % period, t * t * (3.0 - 2.0 * t)))
    out = []
    for y in range(size):
        fy = y / cell
        y0 = int(fy)
        ty = fy - y0
        ty = ty * ty * (3.0 - 2.0 * ty)
        r0, r1 = lattice[y0 % period], lattice[(y0 + 1) % period]
        row = []
        for x0, x1, tx in xs:
            top = r0[x0] + (r0[x1] - r0[x0]) * tx
            bottom = r1[x0] + (r1[x1] - r1[x0]) * tx
            row.append(top + (bottom - top) * ty)
        out.append(row)
    return out


def _fbm(size: int, seed: int, base_period: int, octaves: int, gain: float = 0.5) -> list:
    """Fractal (several octaves of) tileable value noise, normalised to 0..1."""
    rng = random.Random(seed)
    total = [[0.0] * size for _ in range(size)]
    amp, period = 1.0, base_period
    for _ in range(octaves):
        if period > size:
            break
        layer = _value_noise(size, period, rng)
        for y in range(size):
            row, src = total[y], layer[y]
            for x in range(size):
                row[x] += amp * src[x]
        amp *= gain
        period *= 2
    return _normalised(total)


def _normalised(values: list) -> list:
    low = min(min(row) for row in values)
    high = max(max(row) for row in values)
    span = max(high - low, 1e-9)
    return [[(v - low) / span for v in row] for row in values]


def _swell(size: int, seed: int) -> list:
    """A few long directional sine swells with integer frequencies (tileable), normalised to 0..1."""
    tables = _wave_tables(_tileable_waves(seed, 6, 2.0, 7, 1.0), size)
    values = []
    for y in range(size):
        row = []
        for x in range(size):
            h = 0.0
            for kx, ky, amp, sa, ca, sb, cb in tables:
                h += amp * (sa[x] * cb[y] + ca[x] * sb[y])  # sin(A + B)
            row.append(h)
        values.append(row)
    return _normalised(values)


def water_normal_png(size: int = 256) -> bytes:
    """A tileable ripple normal map (Unreal/DirectX convention) for M_InhWater's panning detail:
    wind-driven swells broken up by fractal chop, slopes from wrapped central differences."""
    swell = _swell(size, 1907)
    chop = _fbm(size, 4242, 8, 5, 0.55)
    height = [[0.55 * swell[y][x] + 0.45 * chop[y][x] for x in range(size)] for y in range(size)]
    slopes = []
    peak = 1e-9
    for y in range(size):
        up, here, down = height[(y - 1) % size], height[y], height[(y + 1) % size]
        row = []
        for x in range(size):
            dx = (here[(x + 1) % size] - here[(x - 1) % size]) * 0.5
            dy = (down[x] - up[x]) * 0.5
            row.append((dx, dy))
            peak = max(peak, abs(dx), abs(dy))
        slopes.append(row)
    scale = 0.7 / peak  # the steepest ripple leans about 35 degrees
    rows = []
    for row in slopes:
        out = bytearray()
        for dx, dy in row:
            nx, ny, nz = -dx * scale, -dy * scale, 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            out.extend((int(round((nx * inv * 0.5 + 0.5) * 255)), int(round((ny * inv * 0.5 + 0.5) * 255)),
                        int(round((nz * inv * 0.5 + 0.5) * 255))))
        rows.append(bytes(out))
    return png_bytes(size, size, 3, rows)


def noise_png(size: int = 256) -> bytes:
    """Soft tileable cloud noise (grey in RGB) for M_InhFogCard."""
    values = _fbm(size, 1312, 4, 6, 0.55)
    rows = []
    for row in values:
        out = bytearray()
        for h in row:
            v = int(round(h ** 1.4 * 255))  # a little contrast: clearer gaps between the wisps
            out.extend((v, v, v))
        rows.append(bytes(out))
    return png_bytes(size, size, 3, rows)


def grid_gltf(divisions: int = 64):
    """A 1 m (100 cm in Unreal) square grid facing up, centred on the origin, as (gltf json text, bin bytes).
    glTF is Y-up in metres; Unreal's importer converts that to Z-up centimetres."""
    n = divisions + 1
    pos, nor, uv, idx = array.array("f"), array.array("f"), array.array("f"), array.array("H" if n * n < 65536 else "I")
    for j in range(n):
        for i in range(n):
            pos.extend((i / divisions - 0.5, 0.0, j / divisions - 0.5))
            nor.extend((0.0, 1.0, 0.0))
            uv.extend((i / divisions, j / divisions))
    for j in range(divisions):
        for i in range(divisions):
            a = j * n + i
            b, c = a + 1, a + n
            d = c + 1
            idx.extend((a, c, b, b, c, d))  # counter-clockwise seen from +Y, glTF's front face
    if sys.byteorder != "little":
        for buf in (pos, nor, uv, idx):
            buf.byteswap()
    blobs = [pos.tobytes(), nor.tobytes(), uv.tobytes(), idx.tobytes()]
    data, views = bytearray(), []
    for k, blob in enumerate(blobs):
        while len(data) % 4:
            data.append(0)
        view = {"buffer": 0, "byteOffset": len(data), "byteLength": len(blob), "target": 34963 if k == 3 else 34962}
        views.append(view)
        data.extend(blob)
    while len(data) % 4:
        data.append(0)
    gltf = {
        "asset": {"version": "2.0", "generator": "inh_build_art.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "WaterGrid"}],
        "materials": [{"name": "Water", "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.1}}],
        "meshes": [{"name": "WaterGrid", "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                       "indices": 3, "material": 0, "mode": 4}]}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": n * n, "type": "VEC3", "min": [-0.5, 0.0, -0.5], "max": [0.5, 0.0, 0.5]},
            {"bufferView": 1, "componentType": 5126, "count": n * n, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": n * n, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123 if idx.typecode == "H" else 5125, "count": len(idx), "type": "SCALAR"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(data), "uri": "WaterGrid.bin"}],
    }
    return json.dumps(gltf, indent=1), bytes(data)


def parse_texture_name(stem: str):
    """'wooden_table_02_nor_gl_2k' -> ('wooden_table_02', 'normal', 'gl'); None if no map token is found."""
    name = stem.lower()
    name = re.sub(r"[_\-. ](\d+(\.\d+)?k)$", "", name)
    for token, role in MAP_TOKENS:
        for sep in ("_", "-", " ", "."):
            suffix = sep + token
            if name.endswith(suffix) and len(name) > len(suffix):
                convention = "dx" if "dx" in token else "gl"
                return name[: -len(suffix)], role, convention
    return None


def group_texture_sets(paths):
    """{set name: {role: path}} for the colour/normal/ARM/roughness maps among paths."""
    sets = {}
    for path in sorted(paths, key=lambda p: str(p)):
        path = Path(path)
        if path.suffix.lower() not in IMAGE_EXTS:
            continue
        parsed = parse_texture_name(path.stem)
        if not parsed or parsed[1] not in ("diffuse", "normal", "arm", "roughness", "ao"):
            continue
        set_name, role, convention = parsed
        maps = sets.setdefault(set_name, {})
        if role not in maps:
            maps[role] = path
            if role == "normal":
                maps["normal_convention"] = convention
    return sets


def _loose(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", str(value).lower())


def choose_texture_set(sets: dict, hints) -> str:
    """The texture set whose name best matches any hint (slot or material names); the first one otherwise."""
    if not sets:
        return ""
    names = sorted(sets)
    best, best_len = names[0], 0
    for hint in hints:
        hint = _loose(hint)
        if not hint:
            continue
        for name in names:
            loose = _loose(name)
            if loose == hint:
                return name
            if hint in loose or loose in hint:
                overlap = min(len(hint), len(loose))
                if overlap > best_len:
                    best, best_len = name, overlap
    return best


def surface_uv_scale(entry: dict, dimensions_mm=None) -> float:
    """Repeats per metre for M_InhSurface's world-aligned mapping: one repeat covers the scan's real size.
    Poly Haven's measured size wins when it agrees with the manifest's tileMeters to within 4x, which also
    guards against a unit mix-up (a value read as millimetres that was meant as centimetres is 10x off)."""
    tile = min(max(float(entry.get("tileMeters", 2.0) or 2.0), 0.1), 20.0)
    if isinstance(dimensions_mm, (list, tuple)) and len(dimensions_mm) >= 2:
        try:
            measured = float(max(dimensions_mm[0], dimensions_mm[1])) / 1000.0
            if tile / 4.0 <= measured <= tile * 4.0:
                tile = measured
        except (TypeError, ValueError):
            pass
    return float(entry.get("uvScale", 1.0) or 1.0) / tile


def resolve_files(key: str, lock_assets: dict, lock_dir: Path, custom_root: Path) -> dict:
    """Existing local files for a key: Game/RawAssets/Custom/<Key>/ wins over the Poly Haven download."""
    result = {"source": None}
    custom = custom_root / key
    if custom.is_dir():
        files = sorted(p for p in custom.rglob("*") if p.is_file())
        models = [p for p in files if p.suffix.lower() in MODEL_EXTS]
        hdris = [p for p in files if p.suffix.lower() == ".hdr"]
        sets = group_texture_sets(files)
        if models:
            result.update(source="custom", model=models[0], include=[p for p in files if p.suffix.lower() in IMAGE_EXTS])
        if hdris:
            result.update(source="custom", hdri=hdris[0])
        if sets and not models:
            maps = sets[sorted(sets)[0]]
            result["source"] = "custom"
            result.update({role: value for role, value in maps.items()})
        if result["source"]:
            result["id"] = "custom"
            return result

    entry = lock_assets.get(key) if isinstance(lock_assets, dict) else None
    if not isinstance(entry, dict):
        return result
    files = entry.get("files") if isinstance(entry.get("files"), dict) else {}

    def existing(rel):
        if not isinstance(rel, str) or not rel:
            return None
        path = (lock_dir / rel).resolve()
        try:
            path.relative_to(lock_dir.resolve())  # the lock can't point outside its folder
        except ValueError:
            return None
        return path if path.is_file() else None

    for role in ("diffuse", "normal", "arm", "roughness", "ao", "model", "hdri"):
        path = existing(files.get(role))
        if path:
            result[role] = path
    if "model" in result:
        result["include"] = [p for p in (existing(rel) for rel in files.get("include") or []) if p]
    if any(role in result for role in ("diffuse", "normal", "arm", "roughness", "model", "hdri")):
        result["source"] = "polyhaven"
        result["id"] = entry.get("id")
        result["normal_convention"] = entry.get("normal_convention", "gl")
        result["dimensions_mm"] = entry.get("dimensions_mm")
    return result


def shared_texture_files(entries, lock_assets: dict, lock_dir: Path, custom_root: Path) -> set:
    """Texture files more than one Surface key resolves to (the fetcher downloads a scan two keys share only once)."""
    seen, shared = set(), set()
    for entry in entries:
        if entry.get("master", "Surface") != "Surface" or not entry.get("key"):
            continue
        files = resolve_files(entry["key"], lock_assets, lock_dir, custom_root)
        for role in ("diffuse", "normal", "arm", "roughness"):
            path = files.get(role)
            if path:
                (shared if path in seen else seen).add(path)
    return shared


def parse_only(values) -> set:
    keys = set()
    for value in values or []:
        for key in re.split(r"[,\s]+", str(value)):
            if key:
                keys.add(key.lower())
    return keys


def key_asset_pattern(keys):
    """Matches the texture and mesh-material names a key's build writes (T_<Key>_ORM, T_SM_<Key>_0_Normal,
    MI_SM_<Key>_2, ...) and no other key's, so an --only run prunes just its own keys' leftovers."""
    alternatives = "|".join(re.escape(key) for key in sorted(set(keys)) if key)
    if not alternatives:
        return re.compile(r"(?!)")  # matches nothing
    return re.compile(r"^(?:T_(?:{0})_(?:BaseColor|Normal|ORM|Roughness)|T_SM_(?:{0})_\d+_(?:BaseColor|Normal|ORM)"
                      r"|MI_SM_(?:{0})_\d+)$".format(alternatives), re.IGNORECASE)


def unreferenced(referencers: dict) -> set:
    """{candidate: packages referencing it} -> the candidates nothing but other such candidates uses. A leftover
    material doesn't keep its leftover textures alive, while a referenced one keeps everything it uses."""
    doomed = set(referencers)
    changed = True
    while changed:
        changed = False
        for package in sorted(doomed):
            if referencers[package] - doomed - {package}:
                doomed.discard(package)
                changed = True
    return doomed


# =============================================================================================
# Unreal helpers
# =============================================================================================

class BuildStats:
    def __init__(self):
        self.built = []
        self.flat = []
        self.problems = []

    def problem(self, message: str):
        self.problems.append(message)
        warn(message)


STATS = BuildStats()
# Scan files several Surface keys share (set by build() from the whole manifest) and what each imported as this run.
SHARED_FILES = set()
SHARED_TEXTURES = {}
# Lower-cased package paths of every asset this run saved; prune_leftovers() never deletes these.
WRITTEN = set()


def log(message: str):
    if unreal:
        unreal.log("[InhArt] " + message)
    else:
        print("[InhArt] " + message)


def warn(message: str):
    if unreal:
        unreal.log_warning("[InhArt] " + message)
    else:
        print("[InhArt] warning: " + message)


_MISSING_ENUMS = set()


def enum_value(enum_name: str, *members):
    """unreal.<enum_name>.<first member that exists>, or None (warned once). Shields against renamed enum entries."""
    enum_cls = getattr(unreal, enum_name, None)
    for member in members if enum_cls is not None else ():
        value = getattr(enum_cls, member, None)
        if value is not None:
            return value
    if (enum_name, members) not in _MISSING_ENUMS:
        _MISSING_ENUMS.add((enum_name, members))
        warn("unreal.%s has none of %s in this engine version; that setting is skipped" % (enum_name, ", ".join(members)))
    return None


def set_props(obj, quiet: bool = False, **props) -> bool:
    ok = True
    for name, value in props.items():
        if value is None:
            continue
        try:
            obj.set_editor_property(name, value)
        except Exception as error:  # property missing in this engine version
            ok = False
            if not quiet:
                warn("could not set %s.%s: %s" % (obj.get_class().get_name() if hasattr(obj, "get_class") else obj, name, error))
    return ok


def assets():
    return unreal.EditorAssetLibrary


def exists(path: str) -> bool:
    return bool(assets().does_asset_exist(path))


def load(path: str):
    return assets().load_asset(path) if exists(path) else None


def package_of(asset) -> str:
    return asset.get_path_name().split(".")[0]


def save(asset):
    try:
        WRITTEN.add(package_of(asset).lower())
        assets().save_loaded_asset(asset, False)
    except Exception as error:
        STATS.problem("could not save %s: %s" % (asset.get_path_name(), error))


def delete(path: str) -> bool:
    if not exists(path):
        return True
    try:
        return bool(assets().delete_asset(path))
    except Exception as error:
        warn("could not delete %s: %s" % (path, error))
        return False


def asset_tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def create_asset(name: str, folder: str, cls, factory):
    path = folder + "/" + name
    existing = load(path)
    if existing is not None:
        if isinstance(existing, cls):
            return existing
        delete(path)
    return asset_tools().create_asset(name, folder, cls, factory)


def import_file(filename: Path, folder: str, name: str, options=None, factory=None):
    """Imports one file unattended, replacing what's there; returns the imported object paths."""
    task = unreal.AssetImportTask()
    set_props(task, filename=str(filename), destination_path=folder, destination_name=name,
              automated=True, replace_existing=True, save=True)
    if options is not None:
        set_props(task, options=options)
    if factory is not None:
        set_props(task, factory=factory)
    asset_tools().import_asset_tasks([task])
    try:
        return [str(p) for p in (task.get_editor_property("imported_object_paths") or [])]
    except Exception:
        return []


def find_imported(expected: str, imported_paths, cls):
    """The imported asset of type cls, moved to `expected` if the importer picked another name."""
    asset = load(expected)
    if isinstance(asset, cls):
        return asset
    for path in imported_paths:
        candidate = assets().load_asset(path)
        if isinstance(candidate, cls):
            if package_of(candidate) != expected:
                delete(expected)
                if not assets().rename_asset(package_of(candidate), expected):
                    return candidate
                return load(expected)
            return candidate
    return None


TEXTURE_KINDS = {
    # kind: (compression, sRGB, LOD group candidates)
    "color": (("TC_DEFAULT",), True, ("TEXTUREGROUP_WORLD",)),
    "normal": (("TC_NORMALMAP",), False, ("TEXTUREGROUP_WORLD_NORMAL_MAP", "TEXTUREGROUP_WORLDNORMALMAP")),
    "mask": (("TC_MASKS",), False, ("TEXTUREGROUP_WORLD_SPECULAR", "TEXTUREGROUP_WORLDSPECULAR")),
}


def configure_texture(texture, kind: str, normal_convention: str = "dx"):
    compression, srgb, groups = TEXTURE_KINDS[kind]
    set_props(texture, compression_settings=enum_value("TextureCompressionSettings", *compression), srgb=srgb,
              lod_group=enum_value("TextureGroup", *groups))
    # Virtual textures would need different samplers in the masters; keep everything a plain 2D texture.
    set_props(texture, quiet=True, virtual_texture_streaming=False)
    if kind == "normal":
        # Unreal expects DirectX normals; Poly Haven and glTF ship OpenGL ones (green up).
        set_props(texture, flip_green_channel=(normal_convention == "gl"))


def import_texture(path: Path, folder: str, name: str, kind: str, normal_convention: str = "gl"):
    imported = import_file(path, folder, name)
    texture = find_imported(folder + "/" + name, imported, unreal.Texture2D)
    if texture is None:
        STATS.problem("texture import failed: %s" % path)
        return None
    configure_texture(texture, kind, normal_convention)
    save(texture)
    return texture


# ---------------------------------------------------------------------------------------------
# Built-in textures (generated, so the masters never depend on a download)
# ---------------------------------------------------------------------------------------------

def build_builtin_textures(work_dir: Path) -> dict:
    work_dir.mkdir(parents=True, exist_ok=True)
    files = {
        "white": ("InhWhite.png", solid_png((255, 255, 255))),
        "flat": ("InhFlatNormal.png", solid_png((128, 128, 255))),
        "water": ("InhWaterNormal.png", water_normal_png(256)),
        "noise": ("InhNoise.png", noise_png(256)),
    }
    for _, (filename, data) in files.items():
        (work_dir / filename).write_bytes(data)
    plan = [
        ("white", "T_InhWhite", "color", "dx"),
        ("white", "T_InhWhiteMask", "mask", "dx"),
        ("flat", "T_InhFlatNormal", "normal", "dx"),
        ("water", "T_InhWaterNormal", "normal", "dx"),
        ("noise", "T_InhNoise", "mask", "dx"),
    ]
    textures = {}
    for source, name, kind, convention in plan:
        textures[name] = import_texture(work_dir / files[source][0], BUILTIN_TEXTURES, name, kind, convention)
    missing = [name for name, texture in textures.items() if texture is None]
    if missing:
        raise RuntimeError("built-in textures failed to import: %s" % ", ".join(missing))
    return textures


# ---------------------------------------------------------------------------------------------
# Master materials, authored node by node with MaterialEditingLibrary
# ---------------------------------------------------------------------------------------------

class Graph:
    """Small wrapper over MaterialEditingLibrary that counts every failed step instead of stopping."""

    def __init__(self, material, name: str):
        self.material = material
        self.name = name
        self.lib = unreal.MaterialEditingLibrary
        self.count = 0
        self.problems = 0

    def node(self, class_name: str, **props):
        cls = getattr(unreal, class_name)
        self.count += 1
        x = -260 * (1 + (self.count - 1) // 14)
        y = 110 * ((self.count - 1) % 14)
        expr = self.lib.create_material_expression(self.material, cls, x, y)
        if expr is None:
            self.problems += 1
            raise RuntimeError("%s: could not create %s" % (self.name, class_name))
        if not set_props(expr, **props):
            self.problems += 1
        return expr

    def link(self, src, dst, inputs, out: str = "") -> bool:
        names = (inputs,) if isinstance(inputs, str) else tuple(inputs)
        for name in names:
            try:
                if self.lib.connect_material_expressions(src, out, dst, name):
                    return True
            except Exception:
                pass
        self.problems += 1
        warn("%s: could not connect %s[%s] -> %s.%s" % (self.name, src.get_class().get_name(), out,
                                                         dst.get_class().get_name(), "/".join(names)))
        return False

    def output(self, src, prop: str, out: str = "") -> bool:
        value = getattr(unreal.MaterialProperty, prop, None)
        if value is not None:
            try:
                if self.lib.connect_material_property(src, out, value):
                    return True
            except Exception:
                pass
        self.problems += 1
        warn("%s: could not connect %s to %s" % (self.name, src.get_class().get_name(), prop))
        return False

    # building blocks -------------------------------------------------------------------------

    def scalar(self, name: str, default: float, group: str):
        return self.node("MaterialExpressionScalarParameter", parameter_name=name, default_value=float(default), group=group)

    def vector(self, name: str, rgb, group: str):
        return self.node("MaterialExpressionVectorParameter", parameter_name=name,
                         default_value=unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), 1.0), group=group)

    def texture(self, name: str, texture, sampler: str, uv, group: str, uv_out: str = ""):
        expr = self.node("MaterialExpressionTextureSampleParameter2D", parameter_name=name, texture=texture, group=group)
        set_props(expr, sampler_type=enum_value("MaterialSamplerType", sampler))
        self.link(uv, expr, ("UVs", "Coordinates", ""), uv_out)
        return expr

    def sample(self, texture, sampler: str, uv, uv_out: str = ""):
        expr = self.node("MaterialExpressionTextureSample", texture=texture)
        set_props(expr, sampler_type=enum_value("MaterialSamplerType", sampler))
        self.link(uv, expr, ("UVs", "Coordinates", ""), uv_out)
        return expr

    def const(self, value: float):
        return self.node("MaterialExpressionConstant", r=float(value))

    def const2(self, r: float, g: float):
        return self.node("MaterialExpressionConstant2Vector", r=float(r), g=float(g))

    def const3(self, r: float, g: float, b: float):
        return self.node("MaterialExpressionConstant3Vector", constant=unreal.LinearColor(float(r), float(g), float(b), 1.0))

    def op(self, class_name: str, a=None, b=None, a_out: str = "", b_out: str = "", **props):
        """Two-input maths (Add/Subtract/Multiply/Divide/DotProduct/AppendVector); unconnected sides use const_a/const_b."""
        expr = self.node(class_name, **props)
        if a is not None:
            self.link(a, expr, ("A",), a_out)
        if b is not None:
            self.link(b, expr, ("B",), b_out)
        return expr

    def unary(self, class_name: str, src, out: str = "", **props):
        expr = self.node(class_name, **props)
        self.link(src, expr, ("", "Input", "VectorInput"), out)
        return expr

    def mask(self, src, channels: str, out: str = ""):
        flags = {c: (c in channels) for c in "rgba"}
        expr = self.node("MaterialExpressionComponentMask", **flags)
        self.link(src, expr, ("", "Input"), out)
        return expr

    def lerp(self, a, b, alpha, a_out: str = "", b_out: str = "", alpha_out: str = "", const_a=None, const_b=None):
        expr = self.node("MaterialExpressionLinearInterpolate", const_a=const_a, const_b=const_b)
        if a is not None:
            self.link(a, expr, ("A",), a_out)
        if b is not None:
            self.link(b, expr, ("B",), b_out)
        self.link(alpha, expr, ("Alpha",), alpha_out)
        return expr

    def mix(self, a, b, alpha, alpha_out: str = ""):
        """a + (b - a) * alpha with plain arithmetic, which also takes large-world-coordinate inputs."""
        delta = self.op("MaterialExpressionSubtract", b, a)
        return self.op("MaterialExpressionAdd", a, self.op("MaterialExpressionMultiply", delta, alpha, b_out=alpha_out))


def make_master(name: str, setup):
    material = create_asset(name, MASTERS, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("could not create %s" % name)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    graph = Graph(material, name)
    setup(material, graph)
    if hasattr(unreal.MaterialEditingLibrary, "layout_material_expressions"):
        unreal.MaterialEditingLibrary.layout_material_expressions(material)
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(material)
    if graph.problems:
        STATS.problem("%s built with %d problem(s); open it in the material editor to check" % (name, graph.problems))
    else:
        log("%s: %d nodes" % (name, graph.count))
    return material


def material_flags(material, blend: str, shading: str, two_sided: bool = False, instanced: bool = True, **extra):
    set_props(material, blend_mode=enum_value("BlendMode", blend), shading_model=enum_value("MaterialShadingModel", shading),
              two_sided=two_sided)
    # Without the usage flag a cooked game silently swaps in the default material on instanced meshes.
    set_props(material, used_with_instanced_static_meshes=instanced)
    if extra:
        set_props(material, **extra)


def setup_surface(builtin):
    def setup(material, g: Graph):
        material_flags(material, "BLEND_OPAQUE", "MSM_DEFAULT_LIT")
        group = "Surface"
        # World-aligned box mapping: parts are scaled basic shapes, so mesh UVs would stretch. Positions are taken
        # relative to the object so textures ride along with moving parts instead of swimming.
        uv_scale = g.scalar("UVScale", 1.0, group)
        rel = g.op("MaterialExpressionSubtract", g.node("MaterialExpressionWorldPosition"), g.node("MaterialExpressionObjectPositionWS"))
        p = g.op("MaterialExpressionMultiply", rel, g.op("MaterialExpressionMultiply", uv_scale, const_b=0.01))
        n = g.unary("MaterialExpressionAbs", g.node("MaterialExpressionVertexNormalWS"))
        nx, ny, nz = g.mask(n, "r"), g.mask(n, "g"), g.mask(n, "b")
        w_top = g.unary("MaterialExpressionSaturate", g.op("MaterialExpressionMultiply",
                                                           g.op("MaterialExpressionSubtract", nz, const_b=0.55), const_b=1000.0))
        w_x = g.unary("MaterialExpressionSaturate", g.op("MaterialExpressionMultiply",
                                                         g.op("MaterialExpressionSubtract", nx, ny), const_b=1000.0))
        side = g.mix(g.mask(p, "rb"), g.mask(p, "gb"), w_x)
        side = g.op("MaterialExpressionMultiply", side, g.const2(1.0, -1.0))  # walls: V runs down, so textures stand upright
        box = g.mix(side, g.mask(p, "rg"), w_top)
        mesh_uv = g.op("MaterialExpressionMultiply", g.node("MaterialExpressionTextureCoordinate"), uv_scale)
        uv = g.mix(box, mesh_uv, g.scalar("UseMeshUVs", 0.0, group))

        base = g.texture("BaseColor", builtin["T_InhWhite"], "SAMPLERTYPE_COLOR", uv, group)
        normal = g.texture("Normal", builtin["T_InhFlatNormal"], "SAMPLERTYPE_NORMAL", uv, group)
        orm = g.texture("ORM", builtin["T_InhWhiteMask"], "SAMPLERTYPE_MASKS", uv, group)

        g.output(g.op("MaterialExpressionMultiply", base, g.vector("Tint", (1.0, 1.0, 1.0), group), a_out="RGB"), "MP_BASE_COLOR")
        g.output(g.lerp(g.const3(0.0, 0.0, 1.0), normal, g.scalar("NormalStrength", 1.0, group), b_out="RGB"), "MP_NORMAL")
        g.output(g.lerp(None, orm, g.scalar("AOStrength", 1.0, group), b_out="R", const_a=1.0), "MP_AMBIENT_OCCLUSION")
        g.output(g.unary("MaterialExpressionSaturate",
                         g.op("MaterialExpressionMultiply", orm, g.scalar("RoughnessScale", 1.0, group), a_out="G")), "MP_ROUGHNESS")
        # MetallicFromMap 0 ignores ORM.B: a lone roughness map in the ORM slot has roughness there, not metalness.
        metal_map = g.lerp(None, orm, g.scalar("MetallicFromMap", 1.0, group), b_out="B", const_a=1.0)
        g.output(g.op("MaterialExpressionMultiply", metal_map, g.scalar("Metallic", 0.0, group)), "MP_METALLIC")
    return setup


def setup_emissive(material, g: Graph):
    material_flags(material, "BLEND_OPAQUE", "MSM_UNLIT")
    group = "Emissive"
    glow = g.op("MaterialExpressionMultiply", g.vector("EmissiveColor", (1.0, 0.72, 0.42), group),
                g.scalar("EmissiveStrength", 10.0, group))
    # Instanced windows each get their own brightness; a plain mesh reads PerInstanceRandom as 0 (full brightness).
    variation = g.op("MaterialExpressionSubtract", None,
                     g.op("MaterialExpressionMultiply", g.node("MaterialExpressionPerInstanceRandom"), const_b=0.35), const_a=1.0)
    g.output(g.op("MaterialExpressionMultiply", glow, variation), "MP_EMISSIVE_COLOR")


# (wave vector kx, ky in radians per cm; angular speed from deep-water dispersion w = sqrt(g k); share of WaveHeight)
WAVES = [(0.0042, 0.0018, 2.12, 0.55), (-0.0025, 0.0060, 2.53, 0.30), (0.0105, -0.0072, 3.53, 0.15)]


def setup_water(builtin):
    def setup(material, g: Graph):
        # World-space normals: the wave slopes are computed in world space below.
        material_flags(material, "BLEND_OPAQUE", "MSM_DEFAULT_LIT", tangent_space_normal=False)
        group = "Water"
        height_param = g.scalar("WaveHeight", 18.0, group)
        time = g.op("MaterialExpressionMultiply", g.node("MaterialExpressionTime"), g.scalar("WaveSpeed", 1.0, group))
        pxy = g.mask(g.node("MaterialExpressionWorldPosition"), "rg")

        height_sum, slope_sum = None, None
        for kx, ky, omega, share in WAVES:
            phase = g.op("MaterialExpressionAdd", g.op("MaterialExpressionDotProduct", pxy, g.const2(kx, ky)),
                         g.op("MaterialExpressionMultiply", time, const_b=omega))
            h = g.op("MaterialExpressionMultiply", g.unary("MaterialExpressionSine", phase, period=0.0), const_b=share)
            s = g.op("MaterialExpressionMultiply", g.unary("MaterialExpressionCosine", phase, period=0.0),
                     g.const2(share * kx, share * ky))
            height_sum = h if height_sum is None else g.op("MaterialExpressionAdd", height_sum, h)
            slope_sum = s if slope_sum is None else g.op("MaterialExpressionAdd", slope_sum, s)

        # BuildSea butts this grid edge to edge against plain sheets that only have corner vertices, so the swell fades
        # out over the grid's outer two quads (UVs run 0..1 on both meshes): every border stays at sea level and the
        # seams can't open. The shading keeps the full swell, which is continuous across the sheets anyway.
        border = g.unary("MaterialExpressionAbs",
                         g.op("MaterialExpressionSubtract", g.node("MaterialExpressionTextureCoordinate"), const_b=0.5))
        edge = g.op("MaterialExpressionSubtract", None, g.op("MaterialExpressionMax", g.mask(border, "r"), g.mask(border, "g")),
                    const_a=0.5)
        fade = g.unary("MaterialExpressionSaturate", g.op("MaterialExpressionMultiply", edge, const_b=32.0))
        height = g.op("MaterialExpressionMultiply", g.op("MaterialExpressionMultiply", height_sum, height_param), fade)
        g.output(g.op("MaterialExpressionAppendVector", g.const2(0.0, 0.0), height), "MP_WORLD_POSITION_OFFSET")

        # Two ripple layers panning across each other on top of the analytic swell slope.
        uv1 = g.op("MaterialExpressionAdd", g.op("MaterialExpressionMultiply", pxy, const_b=0.0023),
                   g.op("MaterialExpressionMultiply", time, g.const2(0.012, 0.007)))
        uv2 = g.op("MaterialExpressionAdd", g.op("MaterialExpressionMultiply", pxy, const_b=0.0041),
                   g.op("MaterialExpressionMultiply", time, g.const2(-0.009, 0.013)))
        n1 = g.texture("Normal", builtin["T_InhWaterNormal"], "SAMPLERTYPE_NORMAL", uv1, group)
        n2 = g.texture("Normal", builtin["T_InhWaterNormal"], "SAMPLERTYPE_NORMAL", uv2, group)
        detail = g.op("MaterialExpressionMultiply", g.mask(g.op("MaterialExpressionAdd", n1, n2, a_out="RGB", b_out="RGB"), "rg"),
                      const_b=0.35)
        slope = g.op("MaterialExpressionMultiply", slope_sum, height_param)
        normal = g.unary("MaterialExpressionNormalize",
                         g.op("MaterialExpressionAppendVector", g.op("MaterialExpressionSubtract", detail, slope), g.const(1.0)))
        g.output(normal, "MP_NORMAL")

        # Crests scatter more light (shallow colour); grazing angles show the sky reflection instead of the body colour.
        fresnel = g.node("MaterialExpressionFresnel", exponent=5.0, base_reflect_fraction=0.04)
        crest = g.unary("MaterialExpressionSaturate",
                        g.op("MaterialExpressionAdd", g.op("MaterialExpressionMultiply", height_sum, const_b=0.5), const_b=0.5))
        body = g.op("MaterialExpressionMultiply", crest, g.unary("MaterialExpressionOneMinus", fresnel))
        g.output(g.lerp(g.vector("DeepColor", (0.004, 0.018, 0.028), group), g.vector("ShallowColor", (0.03, 0.09, 0.10), group), body),
                 "MP_BASE_COLOR")
        g.output(g.scalar("Roughness", 0.08, group), "MP_ROUGHNESS")
        g.output(g.const(0.5), "MP_SPECULAR")
    return setup


def setup_glass(material, g: Graph):
    material_flags(material, "BLEND_TRANSLUCENT", "MSM_DEFAULT_LIT")
    set_props(material, quiet=True, translucency_lighting_mode=enum_value("TranslucencyLightingMode", "TLM_SURFACE_PER_PIXEL_LIGHTING"))
    group = "Glass"
    g.output(g.vector("Tint", (0.8, 0.9, 0.95), group), "MP_BASE_COLOR")
    fresnel = g.node("MaterialExpressionFresnel", exponent=4.0, base_reflect_fraction=0.04)
    opacity = g.op("MaterialExpressionAdd", g.scalar("Opacity", 0.18, group), g.op("MaterialExpressionMultiply", fresnel, const_b=0.5))
    g.output(g.unary("MaterialExpressionSaturate", opacity), "MP_OPACITY")
    g.output(g.const(0.04), "MP_ROUGHNESS")
    g.output(g.const(0.0), "MP_METALLIC")
    g.output(g.const(1.0), "MP_SPECULAR")


def setup_fog_card(builtin):
    def setup(material, g: Graph):
        material_flags(material, "BLEND_TRANSLUCENT", "MSM_UNLIT", two_sided=True)
        group = "Fog"
        uv = g.node("MaterialExpressionTextureCoordinate")
        # Soft round edges: 1 - (2r)^2, squared, so the card's outline never shows.
        offset = g.op("MaterialExpressionSubtract", uv, const_b=0.5)
        radial = g.unary("MaterialExpressionSaturate",
                         g.op("MaterialExpressionSubtract", None,
                              g.op("MaterialExpressionMultiply", g.op("MaterialExpressionDotProduct", offset, offset), const_b=4.0),
                              const_a=1.0))
        soft = g.op("MaterialExpressionMultiply", radial, radial)
        # Two layers of tileable noise drifting at different speeds, so the fog slowly churns.
        drift = g.op("MaterialExpressionMultiply", g.node("MaterialExpressionTime"), g.scalar("DriftSpeed", 1.0, group))
        uv1 = g.op("MaterialExpressionAdd", g.op("MaterialExpressionMultiply", uv, const_b=0.7),
                   g.op("MaterialExpressionMultiply", drift, g.const2(0.021, 0.008)))
        uv2 = g.op("MaterialExpressionAdd", g.op("MaterialExpressionMultiply", uv, const_b=1.3),
                   g.op("MaterialExpressionMultiply", drift, g.const2(-0.013, 0.017)))
        n1 = g.sample(builtin["T_InhNoise"], "SAMPLERTYPE_MASKS", uv1)
        n2 = g.sample(builtin["T_InhNoise"], "SAMPLERTYPE_MASKS", uv2)
        wisps = g.unary("MaterialExpressionSaturate",
                        g.op("MaterialExpressionMultiply", g.op("MaterialExpressionMultiply", n1, n2, a_out="R", b_out="R"), const_b=2.5))
        density = g.op("MaterialExpressionMultiply", g.op("MaterialExpressionMultiply", soft, wisps), g.scalar("Opacity", 0.35, group))
        # Fade where the card meets geometry and right in front of the camera.
        depth = g.node("MaterialExpressionDepthFade", fade_distance_default=150.0)
        g.link(density, depth, ("Opacity", "InOpacity", ""))
        camera = g.node("MaterialExpressionCameraDepthFade", fade_length_default=400.0, fade_offset_default=60.0)
        g.output(g.op("MaterialExpressionMultiply", depth, camera), "MP_OPACITY")
        g.output(g.vector("Color", (0.55, 0.62, 0.70), group), "MP_EMISSIVE_COLOR")
    return setup


def build_masters(builtin) -> dict:
    return {
        "Surface": make_master("M_InhSurface", setup_surface(builtin)),
        "Emissive": make_master("M_InhEmissive", setup_emissive),
        "Water": make_master("M_InhWater", setup_water(builtin)),
        "Glass": make_master("M_InhGlass", setup_glass),
        "FogCard": make_master("M_InhFogCard", setup_fog_card(builtin)),
    }


# ---------------------------------------------------------------------------------------------
# Material instances
# ---------------------------------------------------------------------------------------------

def make_instance(name: str, folder: str, parent):
    lib = unreal.MaterialEditingLibrary
    instance = create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    if instance is None:
        raise RuntimeError("could not create %s/%s" % (folder, name))
    lib.set_material_instance_parent(instance, parent)
    # Drop overrides from a previous build (e.g. a texture whose download has since been removed).
    if hasattr(lib, "clear_all_material_instance_parameters"):
        lib.clear_all_material_instance_parameters(instance)
    return instance


def set_scalar(instance, name: str, value):
    unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, name, float(value))


def set_vector(instance, name: str, rgb):
    a = float(rgb[3]) if len(rgb) > 3 else 1.0
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        instance, name, unreal.LinearColor(float(rgb[0]), float(rgb[1]), float(rgb[2]), a))


def set_texture(instance, name: str, texture):
    if texture is not None:
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(instance, name, texture)


def finish_instance(instance):
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    save(instance)


def apply_params(instance, params: dict):
    for name, value in (params or {}).items():
        if isinstance(value, (list, tuple)) and len(value) >= 3:
            set_vector(instance, name, value)
        elif isinstance(value, (int, float)):
            set_scalar(instance, name, value)


def surface_texture(key: str, files: dict, role: str, suffix: str, kind: str, convention: str = "gl"):
    """One of a key's maps as T_<Key>_<Suffix>. A scan several keys share is imported once as T_<file name> instead:
    phones don't load the same pixels twice, and pointing one key at new art later can't repaint the other."""
    path = files.get(role)
    if not path:
        return None
    if path not in SHARED_FILES:
        return import_texture(path, TEXTURES, "T_%s_%s" % (key, suffix), kind, convention)
    if path not in SHARED_TEXTURES:
        SHARED_TEXTURES[path] = import_texture(path, TEXTURES, "T_" + re.sub(r"[^A-Za-z0-9_]", "_", path.stem), kind, convention)
    return SHARED_TEXTURES[path]


def build_surface_instance(entry: dict, files: dict, master):
    key = entry["key"]
    convention = files.get("normal_convention", "gl")
    color = surface_texture(key, files, "diffuse", "BaseColor", "color")
    normal = surface_texture(key, files, "normal", "Normal", "normal", convention)
    orm, packed = None, False
    if files.get("arm"):
        orm = surface_texture(key, files, "arm", "ORM", "mask")
        packed = orm is not None
    elif files.get("roughness"):
        # A lone roughness map goes in the ORM slot: its G channel is roughness; AO and map metalness are switched off.
        orm = surface_texture(key, files, "roughness", "Roughness", "mask")

    instance = make_instance("MI_" + key, MATERIALS, master)
    set_texture(instance, "BaseColor", color)
    set_texture(instance, "Normal", normal)
    set_texture(instance, "ORM", orm)
    fallback = entry.get("fallback") or {}
    set_scalar(instance, "UVScale", surface_uv_scale(entry, files.get("dimensions_mm")))
    set_scalar(instance, "Metallic", entry.get("metallic", 0.0))
    # Only an ARM map carries metalness (B) and AO (R); otherwise metals like Brass get the manifest value as a constant.
    set_scalar(instance, "MetallicFromMap", 1.0 if packed else 0.0)
    set_scalar(instance, "AOStrength", 1.0 if packed else 0.0)
    set_scalar(instance, "RoughnessScale", 1.0 if orm is not None else fallback.get("roughness", 0.8))
    if color is not None:
        set_vector(instance, "Tint", entry.get("tint") or (1.0, 1.0, 1.0))
    else:
        set_vector(instance, "Tint", fallback.get("color") or entry.get("tint") or (0.5, 0.5, 0.5))
        STATS.flat.append(key)
    finish_instance(instance)
    return instance


def build_material(entry: dict, files: dict, masters: dict):
    key = entry["key"]
    override = entry.get("unrealAsset")
    if override:
        parent = assets().load_asset(override) if exists(override) else None
        if isinstance(parent, unreal.MaterialInterface):
            # A Fab/Megascans material: MI_<Key> simply inherits from it.
            finish_instance(make_instance("MI_" + key, MATERIALS, parent))
            return
        STATS.problem("%s: unrealAsset %s is not a material in this project; building from the download" % (key, override))
    master_name = entry.get("master", "Surface")
    master = masters.get(master_name)
    if master is None:
        STATS.problem("%s: unknown master %s" % (key, master_name))
        return
    if master_name == "Surface":
        build_surface_instance(entry, files, master)
        return
    instance = make_instance("MI_" + key, MATERIALS, master)
    apply_params(instance, entry.get("params"))
    if master_name == "Water" and files.get("normal"):
        set_texture(instance, "Normal", import_texture(files["normal"], TEXTURES, "T_%s_Normal" % key, "normal",
                                                       files.get("normal_convention", "gl")))
    finish_instance(instance)


# ---------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------

def interchange_imports_fbx() -> bool:
    """Whether Interchange rather than the legacy FBX importer takes .fbx files (the default from UE 5.5, switched by
    this console variable). Each reads its own options type, so the merge setting goes in the one that runs."""
    try:
        return bool(unreal.SystemLibrary.get_console_variable_bool_value("Interchange.FeatureFlags.Import.FBX"))
    except Exception:
        return False


def mesh_import_options(path: Path):
    """Ask the importer to merge a model's parts into one static mesh (SM_<Key> is a single asset)."""
    interchange = hasattr(unreal, "InterchangeGenericAssetsPipeline")
    if path.suffix.lower() == ".fbx" and hasattr(unreal, "FbxImportUI") and not (interchange and interchange_imports_fbx()):
        ui = unreal.FbxImportUI()
        set_props(ui, quiet=True, import_mesh=True, import_as_skeletal=False, import_materials=True, import_textures=True,
                  import_animations=False, create_physics_asset=False, automated_import_should_detect_type=False,
                  mesh_type_to_import=enum_value("FBXImportType", "FBXIT_STATIC_MESH"))
        try:
            set_props(ui.get_editor_property("static_mesh_import_data"), quiet=True, combine_meshes=True, auto_generate_collision=True)
        except Exception:
            pass
        return ui
    if interchange:
        try:
            pipeline = unreal.InterchangeGenericAssetsPipeline()
            set_props(pipeline.get_editor_property("mesh_pipeline"), quiet=True, combine_static_meshes=True)
        except Exception:
            return None
        try:
            # Like FbxImportUI's import_as_skeletal=False: a rigged model still comes out as a static mesh.
            set_props(pipeline.get_editor_property("common_meshes_properties"), quiet=True,
                      force_all_mesh_as_type=enum_value("InterchangeForceMeshType", "IFMT_STATIC_MESH"))
        except Exception:
            pass
        return pipeline
    return None


def static_meshes_in(folder: str):
    found = []
    if not assets().does_directory_exist(folder):
        return found
    for path in assets().list_assets(folder, True, False):
        asset = assets().load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            found.append(asset)
    return found


def mesh_volume(mesh) -> float:
    try:
        extent = mesh.get_bounds().box_extent
        return extent.x * extent.y * extent.z
    except Exception:
        return 0.0


def import_model(path: Path, key: str, name: str):
    """Imports a model into a staging folder and returns its static mesh moved to /Game/Inheritance/Meshes/<name>."""
    staging = STAGING + "/" + key
    if assets().does_directory_exist(staging):
        assets().delete_directory(staging)
    # No destination name: some importers would apply it to every asset in the file. The mesh is renamed below.
    import_file(path, staging, "", options=mesh_import_options(path))
    meshes = static_meshes_in(staging)
    if not meshes:
        # Some engine versions reject custom pipeline options; try the project's default import settings.
        import_file(path, staging, "")
        meshes = static_meshes_in(staging)
    if not meshes:
        STATS.problem("%s: no static mesh came out of %s" % (key, path.name))
        return None
    meshes.sort(key=mesh_volume, reverse=True)
    if len(meshes) > 1:
        STATS.problem("%s: %s imported as %d separate meshes; using the largest. Merge it in a DCC tool or swap it "
                      "(docs/ART_PIPELINE.md)" % (key, path.name, len(meshes)))
    target = MESHES + "/" + name
    if not delete(target):
        STATS.problem("%s: %s is in use and could not be replaced" % (key, target))
        return None
    if not assets().rename_asset(package_of(meshes[0]), target):
        STATS.problem("%s: could not move the mesh to %s" % (key, target))
        return None
    return load(target)


def mesh_tools():
    if hasattr(unreal, "StaticMeshEditorSubsystem") and hasattr(unreal, "get_editor_subsystem"):
        subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
        if subsystem is not None:
            return subsystem
    return getattr(unreal, "EditorStaticMeshLibrary", None)


def first_type(*names):
    """The first unreal.<name> that exists. UE 5's StaticMeshEditorSubsystem takes its own types; the old
    EditorScriptingUtilities names only survive as deprecated variants that the subsystem rejects."""
    for name in names:
        value = getattr(unreal, name, None)
        if value is not None:
            return value
    return None


def triangle_count(mesh) -> int:
    """LOD 0's triangles from the render data, else from the source mesh description; -1 if neither can be read."""
    readers = (lambda: mesh.get_num_triangles(0),
               lambda: mesh.get_static_mesh_description(0).get_triangle_count())
    for read in readers:
        try:
            count = int(read())
        except Exception:
            continue
        if count > 0:
            return count
    return -1


def prepare_mesh(mesh, generate_lods: bool = True, collision: bool = True):
    """Mobile-friendly settings: no Nanite, LODs, and a simple box so taps (visibility traces) hit it."""
    try:
        nanite = mesh.get_editor_property("nanite_settings")
        if nanite.get_editor_property("enabled"):
            nanite.set_editor_property("enabled", False)
            mesh.set_editor_property("nanite_settings", nanite)
    except Exception:
        pass
    tools = mesh_tools()
    if tools is None:
        STATS.problem("%s: no static mesh editing API, LODs and collision skipped" % mesh.get_name())
        return
    if collision and hasattr(tools, "add_simple_collisions"):
        shape_type = first_type("ScriptCollisionShapeType", "ScriptingCollisionShapeType")
        box = getattr(shape_type, "BOX", None) if shape_type is not None else None
        if box is None:
            STATS.problem("%s: no collision shape type in this engine version, collision skipped" % mesh.get_name())
        else:
            try:
                if not hasattr(tools, "get_simple_collision_count") or tools.get_simple_collision_count(mesh) == 0:
                    tools.add_simple_collisions(mesh, box)
            except Exception as error:
                warn("%s: collision not added: %s" % (mesh.get_name(), error))
    if generate_lods and hasattr(tools, "set_lods"):
        options_type = first_type("StaticMeshReductionOptions", "EditorScriptingMeshReductionOptions")
        settings_type = first_type("StaticMeshReductionSettings", "EditorScriptingMeshReductionSettings")
        if options_type is None or settings_type is None:
            # Without LODs the 40k-triangle cap is skipped too, so this must not pass silently.
            STATS.problem("%s: no mesh reduction types in this engine version, LODs skipped" % mesh.get_name())
        else:
            try:
                triangles = triangle_count(mesh)
                if triangles < 0:
                    STATS.problem("%s: triangle count unreadable, the %d-triangle LOD 0 cap is not applied"
                                  % (mesh.get_name(), MAX_MESH_TRIANGLES))
                lod0 = min(1.0, MAX_MESH_TRIANGLES / float(triangles)) if triangles > MAX_MESH_TRIANGLES else 1.0
                settings = []
                for share, screen in ((1.0, 1.0), (0.5, 0.45), (0.25, 0.2), (0.1, 0.08)):
                    level = settings_type()
                    set_props(level, percent_triangles=share * lod0, screen_size=screen)
                    settings.append(level)
                options = options_type()
                set_props(options, auto_compute_lod_screen_size=False, reduction_settings=settings)
                tools.set_lods(mesh, options)
            except Exception as error:
                warn("%s: LODs not generated: %s" % (mesh.get_name(), error))


def slot_hints(static_material):
    """Names that tell which texture set a material slot used: slot, material and its texture names."""
    hints = []
    for attr in ("material_slot_name", "imported_material_slot_name"):
        try:
            hints.append(str(static_material.get_editor_property(attr)))
        except Exception:
            pass
    material = None
    try:
        material = static_material.get_editor_property("material_interface")
    except Exception:
        pass
    if material is not None:
        hints.append(material.get_name())
        try:
            for value in material.get_editor_property("texture_parameter_values") or []:
                texture = value.get_editor_property("parameter_value")
                if texture is not None:
                    parsed = parse_texture_name(texture.get_name())
                    if parsed:
                        hints.insert(0, parsed[0])
        except Exception:
            pass
    return hints


def build_mesh_materials(mesh, key: str, include, masters: dict):
    """Replaces the importer's materials with M_InhSurface instances (mesh UVs) using the model's own textures.
    Self-made materials keep the cook free of importer plugin content and mobile-cheap."""
    sets = group_texture_sets(include or [])
    imported_sets = {}
    glass = load(MATERIALS + "/MI_Glass")
    try:
        slots = list(mesh.get_editor_property("static_materials"))
    except Exception:
        slots = []
    for index, slot in enumerate(slots):
        hints = slot_hints(slot)
        if glass is not None and any("glass" in hint.lower() for hint in hints):
            mesh.set_material(index, glass)
            continue
        set_name = choose_texture_set(sets, hints)
        if set_name not in imported_sets:
            maps = sets.get(set_name, {})
            prefix = "T_SM_%s_%d" % (key, len(imported_sets))
            convention = maps.get("normal_convention", "gl")
            imported_sets[set_name] = {
                "BaseColor": import_texture(maps["diffuse"], MESH_TEXTURES, prefix + "_BaseColor", "color") if maps.get("diffuse") else None,
                "Normal": import_texture(maps["normal"], MESH_TEXTURES, prefix + "_Normal", "normal", convention) if maps.get("normal") else None,
                "ORM": import_texture(maps.get("arm") or maps.get("roughness"), MESH_TEXTURES, prefix + "_ORM", "mask")
                if (maps.get("arm") or maps.get("roughness")) else None,
                "packed": bool(maps.get("arm")),
            }
        textures = imported_sets[set_name]
        instance = make_instance("MI_SM_%s_%d" % (key, index), MESH_MATERIALS, masters["Surface"])
        for param in ("BaseColor", "Normal", "ORM"):
            set_texture(instance, param, textures[param])
        set_scalar(instance, "UseMeshUVs", 1.0)
        set_scalar(instance, "UVScale", 1.0)
        # ARM maps carry real metalness in blue; a lone roughness map must not turn the model metallic.
        set_scalar(instance, "Metallic", 1.0 if textures["packed"] else 0.0)
        set_scalar(instance, "AOStrength", 1.0 if textures["packed"] else 0.0)
        if textures["BaseColor"] is None:
            set_vector(instance, "Tint", (0.45, 0.42, 0.4))
        finish_instance(instance)
        mesh.set_material(index, instance)


def build_mesh(entry: dict, files: dict, masters: dict):
    key = entry["key"]
    name = "SM_" + key
    override = entry.get("unrealAsset")
    if override:
        source = assets().load_asset(override) if exists(override) else None
        if not isinstance(source, unreal.StaticMesh):
            STATS.problem("%s: unrealAsset %s is not a static mesh in this project; using the download" % (key, override))
        elif delete(MESHES + "/" + name) and assets().duplicate_asset(package_of(source), MESHES + "/" + name):
            STATS.built.append(name)
            return
        else:
            STATS.problem("%s: could not copy %s to %s/%s" % (key, override, MESHES, name))
    if not files.get("model"):
        return
    mesh = import_model(files["model"], key, name)
    if mesh is None:
        return
    build_mesh_materials(mesh, key, files.get("include"), masters)
    prepare_mesh(mesh)
    save(mesh)
    STATS.built.append(name)


def build_water_grid(entry: dict, work_dir: Path):
    """SM_WaterGrid: a finely divided plane so M_InhWater's world-position-offset waves have vertices to move."""
    divisions = int(entry.get("divisions", 64) or 64)
    text, data = grid_gltf(max(4, min(divisions, 200)))
    (work_dir / "WaterGrid.gltf").write_text(text, encoding="utf-8")
    (work_dir / "WaterGrid.bin").write_bytes(data)
    name = "SM_" + entry["key"]
    mesh = import_model(work_dir / "WaterGrid.gltf", entry["key"], name)
    if mesh is None:
        return
    water = load(MATERIALS + "/MI_" + entry.get("material", "Water"))
    if water is not None:
        mesh.set_material(0, water)
    # No LODs: the waves need the full density; no collision: the sea is never traced.
    prepare_mesh(mesh, generate_lods=False, collision=False)
    save(mesh)
    STATS.built.append(name)


# ---------------------------------------------------------------------------------------------
# Skies
# ---------------------------------------------------------------------------------------------

def build_sky(entry: dict, files: dict):
    key = entry["key"]
    name = "HDRI_" + key
    target = SKY + "/" + name
    override = entry.get("unrealAsset")
    if override:
        source = assets().load_asset(override) if exists(override) else None
        if not isinstance(source, unreal.TextureCube):
            STATS.problem("%s: unrealAsset %s is not a cubemap in this project; using the download" % (key, override))
        elif delete(target) and assets().duplicate_asset(package_of(source), target):
            STATS.built.append(name)
            return
        else:
            STATS.problem("%s: could not copy %s to %s" % (key, override, target))
    if not files.get("hdri"):
        return
    imported = import_file(files["hdri"], SKY, name)
    cube = find_imported(target, imported, unreal.Texture)
    if not isinstance(cube, unreal.TextureCube):
        # The classic texture factory always turns a long-lat .hdr into a cubemap.
        delete(target)
        imported = import_file(files["hdri"], SKY, name, factory=unreal.TextureFactory())
        cube = find_imported(target, imported, unreal.Texture)
    if not isinstance(cube, unreal.TextureCube):
        delete(target)
        STATS.problem("%s: %s did not import as a cubemap; the sky stays procedural" % (key, files["hdri"].name))
        return
    # The skies only feed the SkyLight, which resamples them to its 128 px capture; TC_HDR is uncompressed
    # RGBA16F that doesn't stream, so 256 px faces keep each sky near 4 MB instead of 17+ MB on phones.
    set_props(cube, compression_settings=enum_value("TextureCompressionSettings", "TC_HDR"), srgb=False,
              lod_group=enum_value("TextureGroup", "TEXTUREGROUP_SKYBOX"), max_texture_size=256)
    save(cube)
    STATS.built.append(name)


# ---------------------------------------------------------------------------------------------
# Leftovers
# ---------------------------------------------------------------------------------------------

# Folders only this script writes and only other assets reference (the game loads MI_, SM_ and HDRI_ by path, so
# their folders, the masters and Textures/Builtin are never pruned). Materials first: their textures go after them.
PRUNED_FOLDERS = (MESH_MATERIALS, MESH_TEXTURES, TEXTURES)


def prune_leftovers(pattern=None) -> int:
    """Deletes what earlier runs left in the always-cooked PRUNED_FOLDERS that nothing uses any more, so it doesn't
    ship: T_<file>_2k after a 1k rebuild, MI_SM_<Key>_3 after a swap to a simpler model, T_<Key>_Roughness once an
    ARM map arrives. A full run (pattern None) looks at everything there it didn't write, an --only run just at the
    names pattern matches. Anything still referenced stays, with whatever it uses."""
    try:
        # Dependencies as saved by this run, not as the scan at the start found them.
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([ROOT], True)
    except Exception:
        pass
    candidates = []
    for folder in PRUNED_FOLDERS:
        if not assets().does_directory_exist(folder):
            continue
        for path in assets().list_assets(folder, False, False):
            package = str(path).split(".")[0]
            if package.lower() not in WRITTEN and (pattern is None or pattern.match(package.rsplit("/", 1)[-1])):
                candidates.append(package)
    referencers = {}
    for package in candidates:
        try:
            # True: loads the referencers to confirm, so a reference this run replaced doesn't count.
            found = assets().find_package_referencers_for_asset(package, True) or []
        except Exception as error:
            warn("could not check what uses %s (%s); kept" % (package, error))
            continue
        referencers[package.lower()] = {str(p).split(".")[0].lower() for p in found}
    doomed = unreferenced(referencers)
    removed = 0
    for package in candidates:
        if package.lower() in doomed and delete(package):
            log("removed %s, an earlier build's leftover that nothing uses" % package)
            removed += 1
    return removed


# ---------------------------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------------------------

class Progress:
    """unreal.ScopedSlowTask when available (editor progress bar), plain logging otherwise."""

    def __init__(self, total: int):
        self.task = None
        if hasattr(unreal, "ScopedSlowTask"):
            try:
                self.task = unreal.ScopedSlowTask(total, "Building Inheritance art")
                self.task.__enter__()
                self.task.make_dialog(True)
            except Exception:
                self.task = None

    def step(self, message: str):
        log(message)
        if self.task is not None:
            try:
                self.task.enter_progress_frame(1, message)
            except Exception:
                pass

    def close(self):
        if self.task is not None:
            try:
                self.task.__exit__(None, None, None)
            except Exception:
                pass


def build(only: set, lock_override: str = "") -> int:
    game = project_dir()
    manifest = load_json(game / "Content" / "Data" / "Art" / "ArtManifest.json")
    lock_path = Path(lock_override) if lock_override else game / "RawAssets" / "PolyHaven" / "manifest.lock.json"
    lock_assets = {}
    if lock_path.is_file():
        lock_assets = (load_json(lock_path) or {}).get("assets") or {}
        log("Using %s" % lock_path)
    else:
        warn("No %s yet: building flat materials only. Run tools/assets/fetch_cc0_assets.py first for real art." % lock_path)
    lock_dir = lock_path.parent
    custom_root = game / "RawAssets" / "Custom"
    work_dir = game / "Saved" / "InhArtBuild"

    # Commandlets (the headless run) don't scan the asset registry at startup, and every EditorAssetLibrary lookup
    # only sees registered assets: without this a rerun can't find, reuse or replace what the last run saved.
    # All of /Game, so unrealAsset overrides elsewhere in the project resolve too.
    try:
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game"], True)
    except Exception as error:
        warn("could not scan the asset registry (%s); a headless rerun may not find what the last run saved" % error)

    # From every key, not just --only ones, so a shared scan keeps one texture name whichever keys a run rebuilds.
    SHARED_FILES.clear()
    SHARED_FILES.update(shared_texture_files(manifest.get("materials") or [], lock_assets, lock_dir, custom_root))
    SHARED_TEXTURES.clear()
    WRITTEN.clear()

    if only and not exists(MATERIALS + "/MI_FloorWood"):
        # InhArt treats MI_FloorWood as "the art is installed"; without it the game would ignore everything else.
        only = set(only) | {"floorwood"}

    def wanted(entry):
        return not only or entry.get("key", "").lower() in only

    materials = [e for e in manifest.get("materials") or [] if wanted(e)]
    meshes = [e for e in manifest.get("meshes") or [] if wanted(e)]
    generated = [e for e in manifest.get("generatedMeshes") or [] if wanted(e)]
    skies = [e for e in manifest.get("skies") or [] if wanted(e)]
    progress = Progress(2 + len(materials) + len(meshes) + len(generated) + len(skies))
    failed = 0
    try:
        progress.step("Built-in textures")
        builtin = build_builtin_textures(work_dir)
        progress.step("Master materials")
        masters = build_masters(builtin)

        jobs = [("material", e) for e in materials] + [("mesh", e) for e in meshes] + \
               [("generated", e) for e in generated] + [("sky", e) for e in skies]
        for kind, entry in jobs:
            key = entry.get("key", "?")
            progress.step("%s %s" % (kind, key))
            try:
                files = resolve_files(key, lock_assets, lock_dir, custom_root)
                if kind == "material":
                    build_material(entry, files, masters)
                elif kind == "mesh":
                    build_mesh(entry, files, masters)
                elif kind == "generated":
                    build_water_grid(entry, work_dir)
                else:
                    build_sky(entry, files)
            except Exception as error:  # one broken asset must not stop the rest
                failed += 1
                STATS.problem("%s %s failed: %s" % (kind, key, error))
    finally:
        progress.close()

    if assets().does_directory_exist(STAGING):
        try:
            assets().delete_directory(STAGING)
        except Exception as error:
            warn("could not remove %s: %s" % (STAGING, error))
    try:
        assets().save_directory(ROOT, False, True)
    except Exception as error:
        STATS.problem("saving %s failed: %s" % (ROOT, error))

    if failed:
        warn("leftovers from earlier builds kept: %d job(s) failed, so the run doesn't know all it uses" % failed)
    else:
        rebuilt = [e.get("key", "") for e in materials + meshes + generated + skies]
        try:
            removed = prune_leftovers(key_asset_pattern(rebuilt) if only else None)
            if removed:
                log("Removed %d leftover(s) from earlier builds." % removed)
        except Exception as error:
            warn("could not remove leftovers from earlier builds: %s" % error)

    log("Done: %d material keys, %d meshes/skies imported%s." % (
        len(materials), len(STATS.built), (", flat look for " + ", ".join(STATS.flat)) if STATS.flat else ""))
    if STATS.problems:
        warn("%d problem(s):\n  %s" % (len(STATS.problems), "\n  ".join(STATS.problems)))
    return 1 if STATS.problems else 0


# =============================================================================================
# --offline-check
# =============================================================================================

def _read_png(data: bytes):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature"
    pos, chunks = 8, {}
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        crc = struct.unpack(">I", data[pos + 8 + length:pos + 12 + length])[0]
        assert crc == zlib.crc32(tag + body) & 0xFFFFFFFF, "PNG CRC"
        chunks.setdefault(tag, b"")
        chunks[tag] += body
        pos += 12 + length
    width, height, depth, color_type = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    channels = {0: 1, 2: 3, 6: 4}[color_type]
    raw = zlib.decompress(chunks[b"IDAT"])
    assert len(raw) == height * (1 + width * channels), "PNG size"
    rows = [raw[y * (1 + width * channels) + 1:(y + 1) * (1 + width * channels)] for y in range(height)]
    return width, height, channels, rows


def offline_check() -> int:
    checks = [0]

    def check(condition, message):
        checks[0] += 1
        if not condition:
            raise AssertionError(message)

    width, height, channels, rows = _read_png(solid_png((128, 128, 255)))
    check((width, height, channels) == (4, 4, 3) and rows[0][:3] == bytes((128, 128, 255)), "flat normal PNG")
    width, height, channels, rows = _read_png(water_normal_png(64))
    check((width, height) == (64, 64), "water normal size")
    blues = [rows[y][x * 3 + 2] for y in range(64) for x in range(64)]
    check(min(blues) > 128, "water normals all face up")
    # Tileable: the left/right and top/bottom edges continue each other as smoothly as neighbouring pixels do.
    edge = sum(abs(rows[y][0] - rows[y][63 * 3]) for y in range(64)) / 64.0
    inner = sum(abs(rows[y][30 * 3] - rows[y][31 * 3]) for y in range(64)) / 64.0
    check(edge < inner * 3 + 6, "water normal tiles horizontally (%.1f vs %.1f)" % (edge, inner))
    width, height, channels, rows = _read_png(noise_png(64))
    values = [rows[y][x * 3] for y in range(64) for x in range(64)]
    check(min(values) < 20 and max(values) > 235, "noise spans the full range")

    text, data = grid_gltf(8)
    gltf = json.loads(text)
    accessors = gltf["accessors"]
    check(accessors[0]["count"] == 81 and accessors[3]["count"] == 8 * 8 * 6, "grid vertex and index counts")
    check(gltf["buffers"][0]["byteLength"] == len(data) and len(data) % 4 == 0, "grid buffer length")
    for view in gltf["bufferViews"]:
        check(view["byteOffset"] % 4 == 0 and view["byteOffset"] + view["byteLength"] <= len(data), "grid buffer views")
    index_view = gltf["bufferViews"][3]
    indices = array.array("H", data[index_view["byteOffset"]:index_view["byteOffset"] + index_view["byteLength"]])
    if sys.byteorder != "little":
        indices.byteswap()
    check(max(indices) == 80, "grid indices stay in range")
    position_view = gltf["bufferViews"][0]
    positions = array.array("f", data[position_view["byteOffset"]:position_view["byteOffset"] + position_view["byteLength"]])
    if sys.byteorder != "little":
        positions.byteswap()
    a, c, b = indices[0], indices[1], indices[2]
    pa, pb, pc = positions[a * 3:a * 3 + 3], positions[b * 3:b * 3 + 3], positions[c * 3:c * 3 + 3]
    u = [pc[i] - pa[i] for i in range(3)]
    v = [pb[i] - pa[i] for i in range(3)]
    check(u[2] * v[0] - u[0] * v[2] > 0, "grid triangles face up (+Y in glTF)")

    check(parse_texture_name("wooden_table_02_nor_gl_2k") == ("wooden_table_02", "normal", "gl"), "normal GL name")
    check(parse_texture_name("Lantern_01_arm_1k") == ("lantern_01", "arm", "gl"), "arm name")
    check(parse_texture_name("brick_wall_001_nor_dx_4k")[2] == "dx", "normal DX name")
    check(parse_texture_name("rock_diff") == ("rock", "diffuse", "gl"), "name without resolution")
    check(parse_texture_name("readme") is None, "not a texture map")
    sets = group_texture_sets([Path("t/lantern_01_glass_diff_2k.jpg"), Path("t/lantern_01_metal_diff_2k.jpg"),
                               Path("t/lantern_01_metal_nor_gl_2k.jpg"), Path("t/lantern_01_metal_arm_2k.jpg"), Path("x.bin")])
    check(sorted(sets) == ["lantern_01_glass", "lantern_01_metal"] and sets["lantern_01_metal"]["normal_convention"] == "gl",
          "texture sets grouped")
    check(choose_texture_set(sets, ["Metal"]) == "lantern_01_metal", "slot name picks its set")
    check(choose_texture_set(sets, ["lantern_01_glass"]) == "lantern_01_glass", "texture name picks its set")
    check(choose_texture_set(sets, ["unrelated"]) == "lantern_01_glass", "first set by default")

    check(abs(surface_uv_scale({"uvScale": 1.0, "tileMeters": 2.0}) - 0.5) < 1e-9, "uv scale from tileMeters")
    check(abs(surface_uv_scale({"uvScale": 2.0}, [1000, 1000]) - 2.0) < 1e-9, "uv scale from Poly Haven dimensions")
    check(abs(surface_uv_scale({"tileMeters": 4.0}, [5, 5]) - 0.25) < 1e-9, "implausible dimensions ignored")
    check(abs(surface_uv_scale({"tileMeters": 2.0}, [200, 200]) - 0.5) < 1e-9, "a 10x unit mix-up is ignored")
    check(parse_only(["FloorWood,Table", "night"]) == {"floorwood", "table", "night"}, "--only parsing")
    pattern = key_asset_pattern(["Table", "FloorWood", ""])
    check(all(pattern.match(n) for n in ("T_FloorWood_Roughness", "t_floorwood_orm", "MI_SM_Table_3", "T_SM_Table_12_Normal")),
          "--only prunes the rebuilt keys' own textures and mesh materials")
    check(not any(pattern.match(n) for n in ("T_FloorWoodDark_ORM", "MI_SM_TableBig_0", "MI_SM_Table", "MI_Table",
                                               "T_wood_floor_diff_2k", "T_SM_Table_1_Roughness", "T_Brick_ORM")),
          "--only leaves other keys' and shared assets alone")
    check(not key_asset_pattern([]).match("T_FloorWood_ORM"), "no rebuilt keys, nothing pruned")
    check(unreferenced({"mi_old": set(), "t_old": {"mi_old"}, "t_used": {"/game/mi_x"}, "t_chain": {"t_used"},
                        "t_self": {"t_self"}}) == {"mi_old", "t_old", "t_self"},
          "only leftovers nothing outside the leftovers uses are removed")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        lock_dir = root / "PolyHaven"
        (lock_dir / "textures" / "w").mkdir(parents=True)
        (lock_dir / "textures" / "w" / "w_diff_2k.jpg").write_bytes(b"x")
        (lock_dir / "models" / "t" / "textures").mkdir(parents=True)
        (lock_dir / "models" / "t" / "t_2k.gltf").write_text("{}")
        (lock_dir / "models" / "t" / "textures" / "t_diff_2k.jpg").write_bytes(b"x")
        lock_assets = {
            "FloorWood": {"id": "w", "files": {"diffuse": "textures/w/w_diff_2k.jpg", "normal": "textures/w/missing.jpg"},
                          "dimensions_mm": [2000, 2000]},
            "Table": {"id": "t", "files": {"model": "models/t/t_2k.gltf", "include": ["models/t/textures/t_diff_2k.jpg", "../../etc"]}},
            "Evil": {"id": "e", "files": {"diffuse": "../outside.jpg"}},
        }
        (root / "outside.jpg").write_bytes(b"x")
        custom = root / "Custom"
        (custom / "Brick").mkdir(parents=True)
        (custom / "Brick" / "mybrick_basecolor.png").write_bytes(b"x")
        (custom / "Brick" / "mybrick_normal_dx.png").write_bytes(b"x")
        files = resolve_files("FloorWood", lock_assets, lock_dir, custom)
        check(files["source"] == "polyhaven" and files["diffuse"].name == "w_diff_2k.jpg" and "normal" not in files,
              "existing lock files only")
        files = resolve_files("Table", lock_assets, lock_dir, custom)
        check(files["model"].name == "t_2k.gltf" and [p.name for p in files["include"]] == ["t_diff_2k.jpg"], "model + includes")
        check(resolve_files("Evil", lock_assets, lock_dir, custom)["source"] is None, "lock paths can't escape")
        files = resolve_files("Brick", lock_assets, lock_dir, custom)
        check(files["source"] == "custom" and files["diffuse"].name == "mybrick_basecolor.png"
              and files["normal_convention"] == "dx", "custom folder overrides")
        check(resolve_files("Nothing", {}, lock_dir, custom)["source"] is None, "missing key")
        lock_assets["Plaster"] = lock_assets["Water"] = {"id": "w", "files": {"diffuse": "textures/w/w_diff_2k.jpg"}}
        entries = [{"key": "FloorWood", "master": "Surface"}, {"key": "Water", "master": "Water"}, {"key": "Brick"}]
        check(not shared_texture_files(entries, lock_assets, lock_dir, custom), "only Surface keys share textures")
        shared = shared_texture_files(entries + [{"key": "Plaster"}], lock_assets, lock_dir, custom)
        check(shared == {(lock_dir / "textures" / "w" / "w_diff_2k.jpg").resolve()}, "a scan two keys share is found")

    manifest = load_json(project_dir() / "Content" / "Data" / "Art" / "ArtManifest.json")
    keys = [e["key"] for section in ("materials", "meshes", "generatedMeshes", "skies") for e in manifest.get(section, [])]
    check(len(keys) == len(set(keys)), "manifest keys are unique")
    check("FloorWood" in keys, "MI_FloorWood (the installed marker) is always built")
    masters = {e.get("master") for e in manifest["materials"]}
    check(masters <= {"Surface", "Glass", "Water", "Emissive", "FogCard"}, "manifest masters are known: %s" % masters)

    print("inh_build_art offline check passed (%d checks)." % checks[0])
    return 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Import Inheritance's art into Unreal.")
    parser.add_argument("--only", nargs="*", default=None, help="rebuild only these keys")
    parser.add_argument("--lock", default="", help="path to manifest.lock.json")
    parser.add_argument("--offline-check", action="store_true", help="test the parts that don't need Unreal")
    args, _ = parser.parse_known_args(sys.argv[1:] if argv is None else argv)
    if args.offline_check:
        return offline_check()
    if unreal is None:
        print("Run this inside the Unreal Editor, or use tools/assets/build-art.sh / build-art.ps1.")
        return 1
    only = parse_only(args.only or [os.environ.get("INH_ART_ONLY", "")])
    return build(only, args.lock or os.environ.get("INH_ART_LOCK", ""))


if __name__ == "__main__":
    _code = main()
    if unreal is None:
        sys.exit(_code)
    elif _code:
        # Inside Unreal an exception is the reliable way to make the commandlet report failure.
        raise RuntimeError("inh_build_art finished with problems; see the [InhArt] warnings above.")
