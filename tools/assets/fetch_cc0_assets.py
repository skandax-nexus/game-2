#!/usr/bin/env python3
"""Downloads the CC0 (public domain) art Inheritance needs from Poly Haven.

Reads Game/Content/Data/Art/ArtManifest.json, picks one Poly Haven asset per art key (the first
'prefer' id that exists, otherwise the best match by categories and keywords), downloads it into
Game/RawAssets/PolyHaven/<kind>/<id>/... and writes Game/RawAssets/PolyHaven/manifest.lock.json,
which Game/Content/Python/inh_build_art.py imports into Unreal.

  python3 tools/assets/fetch_cc0_assets.py                  # everything at the manifest resolution
  python3 tools/assets/fetch_cc0_assets.py --resolution 1k  # smaller downloads
  python3 tools/assets/fetch_cc0_assets.py --only FloorWood Table --dry-run
  python3 tools/assets/fetch_cc0_assets.py --self-test      # offline check of the selection logic

Runs are resumable: files already on disk are skipped and interrupted downloads continue.
Standard library only, Python 3.9+.
"""

import argparse
import concurrent.futures
import dataclasses
import datetime
import hashlib
import http.client
import json
import math
import os
import posixpath
import re
import shutil
import socket
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Callable, Dict, Iterable, List, Optional, Tuple

API_ROOT = "https://api.polyhaven.com"
# Poly Haven asks API users to identify themselves with a descriptive User-Agent.
USER_AGENT = "Inheritance-ArtPipeline/1.0 (fetch_cc0_assets.py; downloads CC0 assets for the Inheritance game)"
LICENSE = "CC0"

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = REPO_ROOT / "Game" / "Content" / "Data" / "Art" / "ArtManifest.json"
DEFAULT_OUT = REPO_ROOT / "Game" / "RawAssets" / "PolyHaven"
LOCK_NAME = "manifest.lock.json"

# Manifest section -> Poly Haven asset type (the 't' query parameter and the download folder).
KIND_BY_SECTION = {"materials": "textures", "meshes": "models", "skies": "hdris"}

# Names Poly Haven uses for each texture map in /files (matched case-insensitively, in order).
TEXTURE_MAPS = {
    "diffuse": ["Diffuse", "diff", "diffuse", "albedo", "basecolor", "base_color", "col", "color"],
    # OpenGL-style normals first (what glTF uses); the importer flips green for Unreal.
    "normal": ["nor_gl", "normal_gl", "nor", "normal", "nor_dx", "normal_dx"],
    "arm": ["arm"],
    "roughness": ["Rough", "rough", "roughness"],
    "ao": ["AO", "ao", "ambient_occlusion", "ambientocclusion"],
}
IMAGE_FORMATS = ["jpg", "jpeg", "png"]


class FetchError(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# Pure selection logic (exercised by --self-test)
# ---------------------------------------------------------------------------------------------

def res_value(label) -> Optional[float]:
    """'2k' -> 2.0, '0.5k' -> 0.5; anything else -> None."""
    match = re.fullmatch(r"\s*(\d+(?:\.\d+)?)\s*k\s*", str(label).lower())
    if not match:
        return None
    value = float(match.group(1))
    return value if value > 0 else None


def pick_resolution(labels: Iterable[str], wanted: str) -> Optional[str]:
    """The exact resolution if offered, otherwise the nearest one (ties go to the smaller file)."""
    options = [(res_value(label), label) for label in labels]
    options = [(value, label) for value, label in options if value is not None]
    if not options:
        return None
    target = res_value(wanted) or 2.0
    options.sort(key=lambda item: (abs(math.log2(item[0] / target)), item[0], item[1]))
    return options[0][1]


def find_key(mapping, candidates: Iterable[str]) -> Optional[str]:
    """First candidate present in mapping, compared case-insensitively."""
    if not isinstance(mapping, dict):
        return None
    lowered = {}
    for key in mapping:
        lowered.setdefault(str(key).lower(), key)
    for candidate in candidates:
        hit = lowered.get(candidate.lower())
        if hit is not None:
            return hit
    return None


def pick_file(variants_by_res, wanted_res: str, formats: Iterable[str]) -> Optional[Tuple[str, str, dict]]:
    """From {"2k": {"jpg": {"url": ...}}} return (resolution, format, file dict) nearest to wanted_res."""
    if not isinstance(variants_by_res, dict):
        return None
    formats = list(formats)
    usable = {}
    for res, by_format in variants_by_res.items():
        if res_value(res) is None or not isinstance(by_format, dict):
            continue
        fmt = find_key(by_format, formats)
        if fmt is None:
            continue
        info = by_format.get(fmt)
        if isinstance(info, dict) and isinstance(info.get("url"), str) and info["url"].startswith(("http://", "https://")):
            usable[res] = (fmt, info)
    res = pick_resolution(usable.keys(), wanted_res)
    if res is None:
        return None
    fmt, info = usable[res]
    return res, str(fmt).lower(), info


def normalise_id(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", str(value).lower())


def score_asset(asset_id: str, info: dict, categories: List[str], keywords: List[str]) -> int:
    """Keywords in the id/name count most, then tags, then categories. Keywords are required when given."""
    if not isinstance(info, dict):
        info = {}
    name = (asset_id + " " + str(info.get("name", ""))).lower()
    tags = [str(t).lower() for t in info.get("tags") or [] if isinstance(t, (str, int))]
    cats = [str(c).lower() for c in info.get("categories") or [] if isinstance(c, (str, int))]
    keyword_score = 0
    for word in keywords:
        if word in name:
            keyword_score += 3
        elif any(word in tag for tag in tags):
            keyword_score += 2
    category_score = sum(1 for c in categories if c in cats)
    if keywords and keyword_score == 0:
        return 0
    if not keywords and category_score == 0:
        return 0
    return keyword_score + category_score


def choose_asset(source, index: dict) -> Tuple[Optional[str], str]:
    """Returns (asset id or None, why)."""
    if not isinstance(source, dict):
        return None, "no source in the manifest"
    prefer = [str(p) for p in source.get("prefer") or [] if isinstance(p, str) and p]
    for asset_id in prefer:
        if asset_id in index:
            return asset_id, "preferred"
    # Poly Haven ids mix cases (WoodenChair_01, wooden_table_02): try a forgiving match before searching.
    lowered, loose = {}, {}
    for asset_id in sorted(index):
        lowered.setdefault(asset_id.lower(), asset_id)
        loose.setdefault(normalise_id(asset_id), asset_id)
    for wanted in prefer:
        hit = lowered.get(wanted.lower()) or loose.get(normalise_id(wanted))
        if hit:
            return hit, "preferred (matched %s)" % wanted

    categories = [str(c).lower() for c in source.get("categories") or [] if isinstance(c, str) and c]
    keywords = [str(k).lower() for k in source.get("keywords") or [] if isinstance(k, str) and k]
    if not categories and not keywords:
        return None, "no preferred id exists and there are no search terms"
    best_rank, best_id = None, None
    for asset_id in sorted(index):
        info = index.get(asset_id)
        score = score_asset(asset_id, info, categories, keywords)
        if score <= 0:
            continue
        downloads = info.get("download_count", 0) if isinstance(info, dict) else 0
        rank = (score, downloads if isinstance(downloads, (int, float)) else 0)
        if best_rank is None or rank > best_rank:
            best_rank, best_id = rank, asset_id
    if best_id is None:
        return None, "nothing matches categories %s / keywords %s" % (categories, keywords)
    return best_id, "best match (score %d)" % best_rank[0]


def safe_relpath(rel) -> Optional[str]:
    """A downloaded relative path, or None if it could escape the asset folder."""
    if not isinstance(rel, str) or not rel.strip():
        return None
    rel = rel.replace("\\", "/")
    if rel.startswith("/") or re.match(r"^[A-Za-z]:", rel):
        return None
    parts = [part for part in rel.split("/") if part not in ("", ".")]
    if not parts or any(part == ".." for part in parts):
        return None
    if any(re.search(r"[\x00-\x1f]", part) for part in parts):
        return None
    return "/".join(parts)


def safe_name(value: str, fallback: str) -> str:
    name = re.sub(r"[^A-Za-z0-9._-]", "_", str(value)).strip("._")
    return name or fallback


def url_basename(url: str, fallback: str) -> str:
    path = urllib.parse.unquote(urllib.parse.urlparse(url).path)
    return safe_name(posixpath.basename(path), fallback)


def extract_texture_files(files_json, resolution: str) -> Tuple[Dict[str, Tuple[str, str, dict]], dict]:
    """Map role -> (resolution, format, file dict) for diffuse, normal and ARM (or roughness + AO)."""
    picked = {}
    extra = {}
    if not isinstance(files_json, dict):
        return picked, extra
    for role in ("diffuse", "normal", "arm", "roughness", "ao"):
        if role in ("roughness", "ao") and "arm" in picked:
            continue  # ARM already packs AO (R), roughness (G) and metal (B)
        map_key = find_key(files_json, TEXTURE_MAPS[role])
        if map_key is None:
            continue
        choice = pick_file(files_json.get(map_key), resolution, IMAGE_FORMATS)
        if choice:
            picked[role] = choice
            if role == "normal":
                extra["normal_convention"] = "dx" if "dx" in str(map_key).lower() else "gl"
    return picked, extra


def extract_model_files(files_json, resolution: str) -> Optional[dict]:
    """glTF (or FBX as a fallback) plus every file it includes, keeping their relative paths."""
    if not isinstance(files_json, dict):
        return None
    for container, formats in (("gltf", ["gltf", "glb"]), ("fbx", ["fbx"])):
        key = find_key(files_json, [container])
        if key is None:
            continue
        choice = pick_file(files_json.get(key), resolution, formats)
        if not choice:
            continue
        res, fmt, info = choice
        includes = {}
        raw_includes = info.get("include")
        if isinstance(raw_includes, dict):
            for rel, include in raw_includes.items():
                safe = safe_relpath(rel)
                if safe and isinstance(include, dict) and isinstance(include.get("url"), str) \
                        and include["url"].startswith(("http://", "https://")):
                    includes[safe] = include
        return {"resolution": res, "format": fmt, "file": info, "include": includes}
    return None


def extract_hdri_file(files_json, resolution: str) -> Optional[Tuple[str, str, dict]]:
    if not isinstance(files_json, dict):
        return None
    key = find_key(files_json, ["hdri"])
    return pick_file(files_json.get(key), resolution, ["hdr"]) if key is not None else None


# ---------------------------------------------------------------------------------------------
# Network
# ---------------------------------------------------------------------------------------------

def md5_of(path: Path) -> str:
    try:
        digest = hashlib.md5(usedforsecurity=False)
    except TypeError:
        digest = hashlib.md5()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


class HttpClient:
    """urllib with retries, resumable downloads and size/md5 checks."""

    def __init__(self, retries: int = 4, timeout: float = 60.0, sleep: Callable[[float], None] = time.sleep):
        self.retries = max(1, retries)
        self.timeout = timeout
        self.sleep = sleep

    def _request(self, url: str, headers: Optional[dict] = None):
        all_headers = {"User-Agent": USER_AGENT, "Accept": "*/*"}
        all_headers.update(headers or {})
        return urllib.request.urlopen(urllib.request.Request(url, headers=all_headers), timeout=self.timeout)

    def _backoff(self, attempt: int, error: Exception) -> None:
        delay = min(30.0, 2.0 ** attempt)
        if isinstance(error, urllib.error.HTTPError) and error.code == 429:
            try:
                delay = max(delay, float(error.headers.get("Retry-After", delay)))
            except (TypeError, ValueError):
                pass
        self.sleep(delay)

    @staticmethod
    def _retryable(error: Exception) -> bool:
        if isinstance(error, urllib.error.HTTPError):
            return error.code in (408, 425, 429) or error.code >= 500
        # http.client.HTTPException covers a connection dropped mid-body (IncompleteRead).
        return isinstance(error, (urllib.error.URLError, socket.timeout, ConnectionError, TimeoutError, OSError, ValueError,
                                  http.client.HTTPException))

    def get_json(self, url: str):
        last = None
        for attempt in range(self.retries):
            try:
                with self._request(url, {"Accept": "application/json"}) as response:
                    return json.loads(response.read().decode("utf-8"))
            except Exception as error:  # noqa: BLE001 - classified below
                last = error
                if not self._retryable(error) or attempt == self.retries - 1:
                    break
                self._backoff(attempt, error)
        raise FetchError("GET %s failed: %s" % (url, last))

    def download(self, url: str, dest: Path, size: Optional[int] = None, md5: Optional[str] = None, force: bool = False) -> str:
        """Returns 'skipped' or 'downloaded'. Raises FetchError when every attempt failed."""
        if dest.exists() and not force and (size is None or dest.stat().st_size == size):
            return "skipped"
        dest.parent.mkdir(parents=True, exist_ok=True)
        part = dest.with_name(dest.name + ".part")
        if force:
            part.unlink(missing_ok=True)
        last = None
        for attempt in range(self.retries):
            try:
                offset = part.stat().st_size if part.exists() else 0
                if size is not None and offset >= size:
                    part.unlink(missing_ok=True)
                    offset = 0
                headers = {"Range": "bytes=%d-" % offset} if offset else {}
                with self._request(url, headers) as response:
                    status = getattr(response, "status", None) or response.getcode() or 200
                    # 206 continues the partial file; anything else is the whole file again.
                    mode = "ab" if offset and status == 206 else "wb"
                    with open(part, mode) as handle:
                        shutil.copyfileobj(response, handle, 1 << 20)
                got = part.stat().st_size
                if size is not None and got != size:
                    if got > size:
                        part.unlink(missing_ok=True)
                    raise ValueError("expected %d bytes, have %d" % (size, got))
                if md5 and md5_of(part).lower() != str(md5).lower():
                    part.unlink(missing_ok=True)
                    raise ValueError("checksum mismatch")
                os.replace(part, dest)
                return "downloaded"
            except urllib.error.HTTPError as error:
                last = error
                if error.code == 416:
                    part.unlink(missing_ok=True)  # our partial file is stale; start over
                elif not self._retryable(error):
                    break
            except Exception as error:  # noqa: BLE001 - network and IO errors are retried
                last = error
                if not self._retryable(error):
                    break
            if attempt < self.retries - 1:
                self._backoff(attempt, last)
        raise FetchError("download %s failed: %s" % (url, last))


# ---------------------------------------------------------------------------------------------
# Planning, downloading and the lock file
# ---------------------------------------------------------------------------------------------

@dataclasses.dataclass
class FileTask:
    url: str
    rel_path: str  # relative to the output folder, posix separators
    size: Optional[int] = None
    md5: Optional[str] = None


@dataclasses.dataclass
class KeyPlan:
    key: str
    section: str
    kind: str
    asset_id: Optional[str] = None
    reason: str = ""
    info: dict = dataclasses.field(default_factory=dict)
    resolution: Optional[str] = None
    files: dict = dataclasses.field(default_factory=dict)  # role -> rel path, "include" -> [rel paths]
    tasks: List[FileTask] = dataclasses.field(default_factory=list)
    extra: dict = dataclasses.field(default_factory=dict)


def _int_or_none(value) -> Optional[int]:
    return value if isinstance(value, int) and value >= 0 else None


def _md5_or_none(value) -> Optional[str]:
    return value if isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{32}", value) else None


def _task(url: str, rel_path: str, info: dict) -> FileTask:
    return FileTask(url=url, rel_path=rel_path, size=_int_or_none(info.get("size")), md5=_md5_or_none(info.get("md5")))


def plan_key(entry: dict, section: str, index: dict, http, resolution: str, hdri_resolution: str) -> KeyPlan:
    kind = KIND_BY_SECTION[section]
    plan = KeyPlan(key=entry["key"], section=section, kind=kind)
    source = entry.get("source")
    if not source:
        plan.reason = "built by the importer, nothing to download"
        return plan
    asset_id, why = choose_asset(source, index)
    plan.reason = why
    if not asset_id:
        return plan
    plan.asset_id = asset_id
    plan.info = index.get(asset_id) if isinstance(index.get(asset_id), dict) else {}
    folder = "%s/%s" % (kind, safe_name(asset_id, "asset"))
    files_json = http.get_json("%s/files/%s" % (API_ROOT, urllib.parse.quote(asset_id)))

    if kind == "textures":
        picked, extra = extract_texture_files(files_json, resolution)
        plan.extra.update(extra)
        for role, (res, fmt, info) in picked.items():
            rel = "%s/%s" % (folder, url_basename(info["url"], "%s_%s_%s.%s" % (asset_id, role, res, fmt)))
            plan.files[role] = rel
            plan.tasks.append(_task(info["url"], rel, info))
            if role == "diffuse" or plan.resolution is None:
                plan.resolution = res
        if "diffuse" not in picked:
            plan.reason += "; no colour map offered"
    elif kind == "models":
        model = extract_model_files(files_json, resolution)
        if model:
            plan.resolution = model["resolution"]
            info = model["file"]
            rel = "%s/%s" % (folder, url_basename(info["url"], "%s_%s.%s" % (asset_id, model["resolution"], model["format"])))
            plan.files["model"] = rel
            plan.tasks.append(_task(info["url"], rel, info))
            plan.files["include"] = []
            for include_rel, include in sorted(model["include"].items()):
                rel_inc = "%s/%s" % (folder, include_rel)
                plan.files["include"].append(rel_inc)
                plan.tasks.append(_task(include["url"], rel_inc, include))
            plan.extra["format"] = model["format"]
        else:
            plan.reason += "; no glTF/FBX download offered"
    else:
        hdri = extract_hdri_file(files_json, hdri_resolution)
        if hdri:
            res, fmt, info = hdri
            plan.resolution = res
            rel = "%s/%s" % (folder, url_basename(info["url"], "%s_%s.hdr" % (asset_id, res)))
            plan.files["hdri"] = rel
            plan.tasks.append(_task(info["url"], rel, info))
        else:
            plan.reason += "; no .hdr download offered"
    return plan


def lock_entry(plan: KeyPlan, failed: List[str]) -> dict:
    entry = {"section": plan.section, "kind": plan.kind, "license": LICENSE, "reason": plan.reason}
    if not plan.asset_id:
        entry["status"] = "builtin" if plan.reason.startswith("built by") else "missing"
        return entry
    info = plan.info or {}
    entry.update({
        "status": "failed" if failed else ("ok" if plan.tasks else "missing"),
        "id": plan.asset_id,
        "name": info.get("name", plan.asset_id),
        "page": "https://polyhaven.com/a/%s" % urllib.parse.quote(plan.asset_id),
        "authors": info.get("authors", {}) if isinstance(info.get("authors"), dict) else {},
        "resolution": plan.resolution,
        "files": plan.files,
    })
    dimensions = info.get("dimensions")
    if isinstance(dimensions, list) and len(dimensions) >= 2 and all(isinstance(d, (int, float)) for d in dimensions[:2]):
        entry["dimensions_mm"] = [dimensions[0], dimensions[1]]
    entry.update(plan.extra)
    if failed:
        entry["failed_files"] = failed
    return entry


def load_manifest(path: Path) -> Tuple[List[Tuple[str, dict]], dict]:
    with open(path, "r", encoding="utf-8") as handle:
        manifest = json.load(handle)
    entries, seen = [], set()
    for section in KIND_BY_SECTION:
        for entry in manifest.get(section) or []:
            key = entry.get("key") if isinstance(entry, dict) else None
            if not isinstance(key, str) or not key:
                raise FetchError("%s: an entry in '%s' has no key" % (path, section))
            if key in seen:
                raise FetchError("%s: key %s is used twice" % (path, key))
            seen.add(key)
            entries.append((section, entry))
    return entries, manifest


def write_json_atomic(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    with open(tmp, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)
        handle.write("\n")
    os.replace(tmp, path)


def ensure_gitignore(out_dir: Path) -> None:
    """Downloads can always be fetched again, so only the lock file belongs in version control."""
    ignore = out_dir / ".gitignore"
    if not ignore.exists():
        out_dir.mkdir(parents=True, exist_ok=True)
        ignore.write_text("# Written by tools/assets/fetch_cc0_assets.py: re-fetchable downloads stay out of git.\n"
                          "*\n!.gitignore\n!%s\n" % LOCK_NAME, encoding="utf-8")


def run(manifest_path: Path, out_dir: Path, http, resolution: Optional[str] = None, hdri_resolution: Optional[str] = None,
        only: Optional[List[str]] = None, dry_run: bool = False, force: bool = False, jobs: int = 4,
        log: Callable[[str], None] = print) -> Tuple[int, dict]:
    """Returns (exit code, lock or plan summary). 0 ok, 1 some keys failed, 3 Poly Haven unreachable."""
    entries, manifest = load_manifest(manifest_path)
    resolution = resolution or manifest.get("textureResolution") or "2k"
    hdri_resolution = hdri_resolution or manifest.get("hdriResolution") or resolution
    if only:
        wanted = {key.lower() for key in only}
        known = {entry["key"].lower() for _, entry in entries}
        # Generated meshes (e.g. WaterGrid) are valid keys for the importer but have nothing to download.
        known |= {str(entry.get("key", "")).lower() for entry in manifest.get("generatedMeshes") or [] if isinstance(entry, dict)}
        unknown = sorted(wanted - known)
        if unknown:
            raise FetchError("unknown key(s): %s" % ", ".join(unknown))
        entries = [(section, entry) for section, entry in entries if entry["key"].lower() in wanted]

    # 1. Pick an asset for every key (a few small JSON requests).
    indexes = {}
    plans = []
    for section, entry in entries:
        kind = KIND_BY_SECTION[section]
        if entry.get("source") and kind not in indexes:
            try:
                index = http.get_json("%s/assets?t=%s" % (API_ROOT, kind))
            except FetchError as error:
                log("Could not reach Poly Haven (%s)." % error)
                log("Check your internet connection or proxy. The Unreal importer still builds flat materials without downloads.")
                return 3, {}
            indexes[kind] = index if isinstance(index, dict) else {}
        try:
            plan = plan_key(entry, section, indexes.get(kind, {}), http, resolution, hdri_resolution)
        except FetchError as error:
            plan = KeyPlan(key=entry["key"], section=section, kind=kind, reason="lookup failed: %s" % error)
        plans.append(plan)
        target = plan.asset_id or "-"
        log("  %-12s %-34s %s" % (plan.key, target, plan.reason))

    # Two keys may share an asset (PlasterDark tints Plaster's scan); fetch each file once.
    unique = {}
    for plan in plans:
        for task in plan.tasks:
            unique.setdefault(task.rel_path, task)

    if dry_run:
        total = sum(task.size or 0 for task in unique.values())
        for task in unique.values():
            log("    %s  <-  %s" % (task.rel_path, task.url))
        log("Dry run: %d files, about %.1f MB. Nothing was downloaded." % (len(unique), total / 1e6))
        return 0, {"plans": [dataclasses.asdict(plan) for plan in plans]}

    # 2. Download, a few files at a time.
    ensure_gitignore(out_dir)
    results = {}
    lock = threading.Lock()

    def fetch(task: FileTask):
        try:
            status = http.download(task.url, out_dir / task.rel_path, task.size, task.md5, force)
        except FetchError as error:
            status = "failed: %s" % error
        with lock:
            results[task.rel_path] = status
            if status != "skipped":
                log("    %-10s %s" % (status.split(":")[0], task.rel_path))
        return status

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        list(pool.map(fetch, unique.values()))

    # 3. The lock file, merged with keys this run didn't touch (e.g. with --only).
    lock_path = out_dir / LOCK_NAME
    previous = {}
    if lock_path.exists():
        try:
            with open(lock_path, "r", encoding="utf-8") as handle:
                previous = json.load(handle).get("assets", {}) or {}
        except (OSError, ValueError):
            previous = {}
    assets = dict(previous) if isinstance(previous, dict) else {}
    failures = 0
    for plan in plans:
        failed = [task.rel_path for task in plan.tasks if results.get(task.rel_path, "").startswith("failed")]
        lookup_failed = plan.reason.startswith("lookup failed")
        failures += 1 if failed or lookup_failed else 0
        kept = assets.get(plan.key)
        if lookup_failed and isinstance(kept, dict) and kept.get("files"):
            # A transient /files error must not forget files already on disk, or the importer goes flat.
            assets[plan.key] = dict(kept, last_error=plan.reason)
        else:
            assets[plan.key] = lock_entry(plan, failed)
    data = {
        "comment": "Written by tools/assets/fetch_cc0_assets.py; read by Game/Content/Python/inh_build_art.py. "
                   "Paths are relative to this file. Every asset is CC0 (public domain) from Poly Haven.",
        "generator": "tools/assets/fetch_cc0_assets.py",
        "source": "https://polyhaven.com",
        "license": LICENSE,
        "generated": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "resolution": resolution,
        "hdriResolution": hdri_resolution,
        "assets": assets,
    }
    write_json_atomic(lock_path, data)
    downloaded = sum(1 for status in results.values() if status == "downloaded")
    skipped = sum(1 for status in results.values() if status == "skipped")
    log("%d downloaded, %d already present, %d key(s) with problems. Lock: %s" % (downloaded, skipped, failures, lock_path))
    return (1 if failures else 0), data


# ---------------------------------------------------------------------------------------------
# --self-test: offline, against an embedded fake API
# ---------------------------------------------------------------------------------------------

FAKE_INDEX = {
    "textures": {
        "wood_floor_worn": {"name": "Wood Floor Worn", "type": 1, "categories": ["wood", "floor"], "tags": ["plank"],
                            "dimensions": [2000, 2000], "download_count": 900, "authors": {"Rob Tuytel": "All"}},
        "fabric_pattern_05": {"name": "Fabric Pattern 05", "type": 1, "categories": ["fabric"], "tags": ["cloth"], "download_count": 50},
        "fabric_pattern_07": {"name": "Fabric Pattern 07", "type": 1, "categories": ["fabric"], "tags": ["cloth"], "download_count": 80},
        "denim_fabric": {"name": "Denim Fabric", "type": 1, "categories": ["fabric"], "tags": ["jeans"], "download_count": 500},
        "rusty_metal_02": {"name": "Rusty Metal 02", "type": 1, "categories": ["metal"], "tags": ["rust"], "download_count": 10},
        "broken": "not a dict",
    },
    "models": {
        "WoodenTable_01": {"name": "Wooden Table 01", "type": 2, "categories": ["furniture", "table"], "tags": ["wood"]},
        "Lantern_01": {"name": "Lantern 01", "type": 2, "categories": ["lighting"], "tags": ["lamp"]},
    },
    "hdris": {
        "kloppenheim_02": {"name": "Kloppenheim 02", "type": 0, "categories": ["night", "outdoor"], "tags": []},
        "spruit_sunrise": {"name": "Spruit Sunrise", "type": 0, "categories": ["sunrise-sunset"], "tags": ["dawn"]},
    },
}


def _fake_url(path: str) -> str:
    return "https://dl.polyhaven.org/file/ph-assets/" + path


FAKE_FILES = {
    "wood_floor_worn": {
        "Diffuse": {"1k": {"jpg": {"url": _fake_url("t/1k/wood_floor_worn_diff_1k.jpg"), "size": 6}},
                    "2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_diff_2k.jpg"), "size": 6},
                           "png": {"url": _fake_url("t/2k/wood_floor_worn_diff_2k.png")}}},
        "nor_gl": {"2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_nor_gl_2k.jpg")}}},
        "nor_dx": {"2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_nor_dx_2k.jpg")}}},
        "Rough": {"2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_rough_2k.jpg")}}},
        "AO": {"2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_ao_2k.jpg")}}},
        "arm": {"2k": {"jpg": {"url": _fake_url("t/2k/wood_floor_worn_arm_2k.jpg")}}},
        "Displacement": {"2k": {"png": {"url": _fake_url("t/2k/wood_floor_worn_disp_2k.png")}}},
        "blend": {"2k": {"blend": {"url": _fake_url("t/2k/wood_floor_worn_2k.blend")}}},
    },
    # Lower-case map names, a DirectX normal and no ARM, offered only at 1k and 4k.
    "denim_fabric": {
        "diff": {"1k": {"jpg": {"url": _fake_url("t/1k/denim_fabric_diff_1k.jpg")}},
                 "4k": {"jpg": {"url": _fake_url("t/4k/denim_fabric_diff_4k.jpg")}}},
        "nor_dx": {"1k": {"jpg": {"url": _fake_url("t/1k/denim_fabric_nor_dx_1k.jpg")}},
                   "4k": {"jpg": {"url": _fake_url("t/4k/denim_fabric_nor_dx_4k.jpg")}}},
        "rough": {"1k": {"jpg": {"url": _fake_url("t/1k/denim_fabric_rough_1k.jpg")}}},
        "ao": {"4k": {"jpg": {"url": _fake_url("t/4k/denim_fabric_ao_4k.jpg")}}},
        "junk": ["not", "a", "dict"],
    },
    "WoodenTable_01": {
        "gltf": {"2k": {"gltf": {"url": _fake_url("m/2k/WoodenTable_01_2k.gltf"),
                                 "include": {"WoodenTable_01.bin": {"url": _fake_url("m/WoodenTable_01.bin")},
                                             "textures/WoodenTable_01_diff_2k.jpg": {"url": _fake_url("m/2k/WoodenTable_01_diff_2k.jpg")},
                                             "../../evil.jpg": {"url": _fake_url("evil.jpg")},
                                             "/etc/passwd": {"url": _fake_url("passwd")},
                                             "textures/no_url.jpg": {"md5": "x"}}}}},
        "fbx": {"2k": {"fbx": {"url": _fake_url("m/2k/WoodenTable_01_2k.fbx")}}},
    },
    # Only FBX, only at 4k: falls back on both counts.
    "Lantern_01": {"fbx": {"4k": {"fbx": {"url": _fake_url("m/4k/Lantern_01_4k.fbx"), "include": {}}}}},
    "kloppenheim_02": {"hdri": {"1k": {"hdr": {"url": _fake_url("h/1k/kloppenheim_02_1k.hdr")}, "exr": {"url": _fake_url("h/1k/k.exr")}},
                                "2k": {"hdr": {"url": _fake_url("h/2k/kloppenheim_02_2k.hdr")}}},
                       "tonemapped": {"url": _fake_url("h/kloppenheim_02.jpg")}},
    "spruit_sunrise": {"hdri": {"4k": {"exr": {"url": _fake_url("h/4k/spruit.exr")}}}},  # no .hdr at all
}


class FakeHttp:
    """Answers like the Poly Haven API without touching the network."""

    def __init__(self, fail_urls: Iterable[str] = ()):
        self.fail_urls = set(fail_urls)
        self.downloads = []

    def get_json(self, url: str):
        parsed = urllib.parse.urlparse(url)
        assert parsed.netloc == "api.polyhaven.com", url
        if url in self.fail_urls:
            raise FetchError("simulated failure")
        if parsed.path == "/assets":
            kind = urllib.parse.parse_qs(parsed.query).get("t", [""])[0]
            return FAKE_INDEX[kind]
        if parsed.path.startswith("/files/"):
            return FAKE_FILES.get(urllib.parse.unquote(parsed.path[len("/files/"):]), {})
        raise FetchError("unexpected url %s" % url)

    def download(self, url: str, dest: Path, size=None, md5=None, force=False) -> str:
        if url in self.fail_urls:
            raise FetchError("simulated failure")
        if dest.exists() and not force:
            return "skipped"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(b"CC0 ok" if size == 6 else b"fake")
        self.downloads.append(url)
        return "downloaded"


def self_test() -> int:
    checks = [0]

    def check(condition, message):
        checks[0] += 1
        if not condition:
            raise AssertionError(message)

    # Resolutions: exact, nearest (ties to the smaller file), labels that aren't sizes ignored.
    check(pick_resolution(["1k", "2k", "4k"], "2k") == "2k", "exact resolution")
    check(pick_resolution(["1k", "4k"], "2k") == "1k", "tie goes to the smaller resolution")
    check(pick_resolution(["4k", "8k"], "2k") == "4k", "nearest higher resolution")
    check(pick_resolution(["0.5k", "8k"], "2k") == "0.5k", "nearest by ratio")
    check(pick_resolution(["blend", "x"], "2k") is None, "no usable resolution")
    check(res_value("2k") == 2.0 and res_value("hdr") is None, "resolution parsing")

    # Choosing an asset.
    textures = FAKE_INDEX["textures"]
    check(choose_asset({"prefer": ["nope", "wood_floor_worn"]}, textures)[0] == "wood_floor_worn", "first existing preferred id")
    check(choose_asset({"prefer": ["woodentable_01"]}, FAKE_INDEX["models"])[0] == "WoodenTable_01", "case-insensitive preferred id")
    check(choose_asset({"prefer": ["wooden_table_01"]}, FAKE_INDEX["models"])[0] == "WoodenTable_01", "underscore-insensitive preferred id")
    best = choose_asset({"prefer": ["missing"], "categories": ["fabric"], "keywords": ["fabric"]}, textures)
    check(best[0] == "denim_fabric", "best keyword match, most downloaded wins ties: %s" % (best,))
    check(choose_asset({"prefer": [], "categories": ["metal"], "keywords": []}, textures)[0] == "rusty_metal_02", "category-only search")
    check(choose_asset({"prefer": ["x"], "categories": [], "keywords": []}, textures)[0] is None, "no search terms -> nothing")
    check(choose_asset({"prefer": [], "categories": ["wood"], "keywords": ["zebra"]}, textures)[0] is None, "keywords are required")
    check(choose_asset(None, textures)[0] is None, "missing source")
    check(choose_asset({"prefer": ["Broken"], "categories": ["x"], "keywords": ["broken"]}, textures)[0] == "broken",
          "malformed index entries don't crash")

    # Texture maps: canonical names, ARM preferred over roughness + AO, GL normal preferred.
    picked, extra = extract_texture_files(FAKE_FILES["wood_floor_worn"], "2k")
    check(set(picked) == {"diffuse", "normal", "arm"}, "diffuse + normal + arm: %s" % sorted(picked))
    check(picked["diffuse"][1] == "jpg" and picked["diffuse"][2]["url"].endswith("diff_2k.jpg"), "jpg preferred over png")
    check(picked["normal"][2]["url"].endswith("nor_gl_2k.jpg") and extra["normal_convention"] == "gl", "OpenGL normal")
    picked, extra = extract_texture_files(FAKE_FILES["denim_fabric"], "2k")
    check(set(picked) == {"diffuse", "normal", "roughness", "ao"}, "roughness + ao without arm: %s" % sorted(picked))
    check(picked["diffuse"][0] == "1k" and picked["ao"][0] == "4k", "per-map resolution fallback")
    check(extra["normal_convention"] == "dx", "DirectX normal detected")
    check(extract_texture_files({}, "2k") == ({}, {}) and extract_texture_files(None, "2k") == ({}, {}), "empty /files")
    check(extract_texture_files({"Diffuse": {"2k": {"jpg": {"md5": "no url"}}}}, "2k") == ({}, {}), "file without url")

    # Models: glTF with includes, unsafe paths dropped, FBX fallback.
    model = extract_model_files(FAKE_FILES["WoodenTable_01"], "2k")
    check(model["format"] == "gltf" and model["file"]["url"].endswith("_2k.gltf"), "glTF chosen")
    check(sorted(model["include"]) == ["WoodenTable_01.bin", "textures/WoodenTable_01_diff_2k.jpg"],
          "includes keep relative paths, unsafe ones dropped: %s" % sorted(model["include"]))
    model = extract_model_files(FAKE_FILES["Lantern_01"], "2k")
    check(model["format"] == "fbx" and model["resolution"] == "4k", "FBX and resolution fallback")
    check(extract_model_files({"gltf": "broken"}, "2k") is None, "malformed model files")
    check(safe_relpath("a/./b.jpg") == "a/b.jpg" and safe_relpath("C:/x") is None and safe_relpath("a/../../b") is None, "safe paths")

    # HDRIs.
    hdri = extract_hdri_file(FAKE_FILES["kloppenheim_02"], "2k")
    check(hdri[0] == "2k" and hdri[2]["url"].endswith("_2k.hdr"), "hdr at the wanted resolution")
    check(extract_hdri_file(FAKE_FILES["kloppenheim_02"], "8k")[0] == "2k", "hdr resolution fallback")
    check(extract_hdri_file(FAKE_FILES["spruit_sunrise"], "2k") is None, "exr-only HDRI is skipped")
    check(url_basename("https://x/y/a%20b.hdr?x=1", "f") == "a_b.hdr" and url_basename("https://x/", "f") == "f", "file names")

    # End to end with the fake API: plan, download, lock, resume, --only merge, dry run.
    manifest = {
        "textureResolution": "2k",
        "materials": [
            {"key": "FloorWood", "source": {"prefer": ["wood_floor_worn"], "categories": ["wood"], "keywords": ["plank"]}},
            {"key": "Fabric", "source": {"prefer": ["nope"], "categories": ["fabric"], "keywords": ["fabric"]}},
            {"key": "Glass", "master": "Glass"},
        ],
        "meshes": [{"key": "Table", "source": {"prefer": ["WoodenTable_01"], "categories": [], "keywords": []}},
                   {"key": "Ghost", "source": {"prefer": ["no_such_model"], "categories": [], "keywords": []}}],
        "skies": [{"key": "Night", "source": {"prefer": ["kloppenheim_02"]}}],
    }
    quiet = lambda message: None  # noqa: E731
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        manifest_path = tmp_path / "ArtManifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        out = tmp_path / "PolyHaven"

        fake = FakeHttp()
        code, plan = run(manifest_path, out, fake, dry_run=True, log=quiet)
        check(code == 0 and not out.exists() and not fake.downloads, "dry run downloads nothing")

        code, lock = run(manifest_path, out, fake, log=quiet)
        check(code == 0, "first run succeeds")
        assets = lock["assets"]
        check(assets["FloorWood"]["id"] == "wood_floor_worn" and assets["FloorWood"]["license"] == "CC0", "lock id + license")
        check(assets["FloorWood"]["dimensions_mm"] == [2000, 2000], "real-world size recorded")
        check(assets["FloorWood"]["files"]["diffuse"] == "textures/wood_floor_worn/wood_floor_worn_diff_2k.jpg", "lock paths")
        check((out / assets["FloorWood"]["files"]["arm"]).exists(), "arm downloaded")
        check(assets["Fabric"]["id"] == "denim_fabric" and assets["Fabric"]["normal_convention"] == "dx", "fallback search in a run")
        check(assets["Glass"]["status"] == "builtin" and assets["Ghost"]["status"] == "missing", "builtin and missing keys")
        table = assets["Table"]
        check(table["files"]["model"] == "models/WoodenTable_01/WoodenTable_01_2k.gltf", "model path")
        check("models/WoodenTable_01/textures/WoodenTable_01_diff_2k.jpg" in table["files"]["include"], "include path")
        check((out / "models/WoodenTable_01/textures/WoodenTable_01_diff_2k.jpg").exists(), "include downloaded")
        check(not (tmp_path / "evil.jpg").exists(), "nothing written outside the output folder")
        check(assets["Night"]["files"]["hdri"] == "hdris/kloppenheim_02/kloppenheim_02_2k.hdr", "hdri path")
        check((out / LOCK_NAME).exists() and (out / ".gitignore").exists(), "lock and .gitignore written")
        first_count = len(fake.downloads)

        code, _ = run(manifest_path, out, fake, log=quiet)
        check(code == 0 and len(fake.downloads) == first_count, "second run skips existing files")

        broken = FakeHttp(fail_urls=[_fake_url("h/2k/kloppenheim_02_2k.hdr")])
        (out / "hdris/kloppenheim_02/kloppenheim_02_2k.hdr").unlink()
        code, lock = run(manifest_path, out, broken, only=["Night"], log=quiet)
        check(code == 1 and lock["assets"]["Night"]["status"] == "failed", "failed download reported")
        check(lock["assets"]["FloorWood"]["status"] == "ok", "--only keeps other keys in the lock")
        flaky = FakeHttp(fail_urls=[API_ROOT + "/files/wood_floor_worn"])
        code, lock = run(manifest_path, out, flaky, only=["FloorWood"], log=quiet)
        kept = lock["assets"]["FloorWood"]
        check(code == 1 and kept["files"].get("arm", "").endswith("arm_2k.jpg") and "last_error" in kept,
              "a failed lookup keeps the previous files: %s" % (kept,))
        manifest["generatedMeshes"] = [{"key": "WaterGrid"}]
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        code, lock = run(manifest_path, out, fake, only=["WaterGrid"], log=quiet)
        check(code == 0 and "FloorWood" in lock["assets"], "a generated key is accepted and downloads nothing")
        try:
            run(manifest_path, out, fake, only=["Nope"], log=quiet)
            check(False, "unknown --only key rejected")
        except FetchError:
            check(True, "unknown --only key rejected")

        # The real downloader against local files: retries, checksum, resume of a partial file.
        source = tmp_path / "source.bin"
        payload = b"0123456789" * 1000
        source.write_bytes(payload)
        url = source.resolve().as_uri()
        client = HttpClient(retries=2, timeout=5, sleep=lambda seconds: None)
        dest = tmp_path / "dl" / "file.bin"
        check(client.download(url, dest, size=len(payload), md5=md5_of(source)) == "downloaded" and dest.read_bytes() == payload,
              "download with size and md5")
        check(client.download(url, dest, size=len(payload)) == "skipped", "existing file skipped")
        check(client.download(url, dest, size=len(payload), force=True) == "downloaded", "--force downloads again")
        try:
            client.download(url, tmp_path / "dl" / "bad.bin", md5="0" * 32)
            check(False, "checksum mismatch rejected")
        except FetchError:
            check(not (tmp_path / "dl" / "bad.bin").exists(), "checksum mismatch rejected")
        partial = tmp_path / "dl" / "resume.bin"
        partial.with_name("resume.bin.part").write_bytes(payload[:100])
        check(client.download(url, partial, size=len(payload)) == "downloaded" and partial.read_bytes() == payload,
              "a stale partial file is replaced by the full download")

    print("fetch_cc0_assets self-test passed (%d checks)." % checks[0])
    return 0


# ---------------------------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Download Inheritance's CC0 art from Poly Haven.")
    parser.add_argument("--resolution", help="texture/model resolution, e.g. 1k, 2k, 4k (default: the manifest's)")
    parser.add_argument("--hdri-resolution", help="sky resolution (default: the manifest's hdriResolution)")
    parser.add_argument("--only", nargs="+", metavar="KEY", help="only these art keys (space or comma separated)")
    parser.add_argument("--dry-run", action="store_true", help="show what would be downloaded")
    parser.add_argument("--self-test", action="store_true", help="run the offline self-test and exit")
    parser.add_argument("--force", action="store_true", help="download again even if files exist")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST, help="path to ArtManifest.json")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="download folder (default Game/RawAssets/PolyHaven)")
    parser.add_argument("--jobs", type=int, default=4, help="parallel downloads (default 4)")
    parser.add_argument("--retries", type=int, default=4, help="attempts per request (default 4)")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds per request (default 60)")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    only = None
    if args.only:
        # Split like the importer does: PowerShell turns -Only A,B into the single argument "A B".
        only = [key for chunk in args.only for key in re.split(r"[,\s]+", chunk) if key]
    for label in (args.resolution, args.hdri_resolution):
        if label and res_value(label) is None:
            parser.error("resolutions look like 1k, 2k or 4k, not %r" % label)

    print("Poly Haven CC0 assets -> %s" % args.out)
    try:
        code, _ = run(args.manifest, args.out, HttpClient(args.retries, args.timeout), args.resolution, args.hdri_resolution,
                      only, args.dry_run, args.force, args.jobs)
    except FetchError as error:
        print("error: %s" % error, file=sys.stderr)
        return 2
    except (OSError, ValueError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\nInterrupted. Run again to resume where it stopped.", file=sys.stderr)
        return 130
    return code


if __name__ == "__main__":
    sys.exit(main())
