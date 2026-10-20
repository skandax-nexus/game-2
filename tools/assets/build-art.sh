#!/usr/bin/env bash
# One command for Inheritance's real art: downloads CC0 assets from Poly Haven, then imports them into
# Unreal headless (materials, meshes and skies under /Game/Inheritance). See docs/ART_PIPELINE.md.
#
#   tools/assets/build-art.sh [--engine <UE root>] [--resolution 1k|2k|4k] [--only Key,Key] [--skip-fetch] [--skip-import]
#
# The engine defaults to $UE_ROOT, else the usual install folder for UE ${UE_VERSION:-5.6} on this OS.
# Set UE_EDITOR_CMD to point at the editor executable directly.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
UPROJECT="${ROOT}/Game/Inheritance.uproject"
SCRIPT="${ROOT}/Game/Content/Python/inh_build_art.py"
FETCHER="${ROOT}/tools/assets/fetch_cc0_assets.py"
ENGINE_VERSION="${UE_VERSION:-5.6}"
ENGINE="${UE_ROOT:-}"
RESOLUTION=""
ONLY=""
SKIP_FETCH=0
SKIP_IMPORT=0

step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
done_() { printf '    \033[32m%s\033[0m\n' "$1"; }
warn() { printf '    \033[33m%s\033[0m\n' "$1"; }
fail() { printf '\n\033[31m%s\033[0m\n' "$1" >&2; exit 1; }
need_value() { [ "$2" -ge 2 ] || fail "$1 needs a value (see --help)."; }

while [ $# -gt 0 ]; do
  case "$1" in
    --engine) need_value "$1" $#; ENGINE="$2"; shift 2 ;;
    --engine=*) ENGINE="${1#*=}"; shift ;;
    --resolution) need_value "$1" $#; RESOLUTION="$2"; shift 2 ;;
    --resolution=*) RESOLUTION="${1#*=}"; shift ;;
    --only) need_value "$1" $#; ONLY="$2"; shift 2 ;;
    --only=*) ONLY="${1#*=}"; shift ;;
    --skip-fetch) SKIP_FETCH=1; shift ;;
    --skip-import) SKIP_IMPORT=1; shift ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) fail "Unknown option: $1 (see --help)." ;;
  esac
done

case "$(uname -s)" in
  Darwin) PLATFORM=Mac ;;
  Linux) PLATFORM=Linux ;;
  *) PLATFORM=Win64 ;;  # Git Bash / MSYS on Windows
esac

if [ -z "${ENGINE}" ]; then
  case "${PLATFORM}" in
    Mac) ENGINE="/Users/Shared/Epic Games/UE_${ENGINE_VERSION}" ;;
    Win64) ENGINE="/c/Program Files/Epic Games/UE_${ENGINE_VERSION}" ;;
    Linux)
      for candidate in "${HOME}/UnrealEngine" "${HOME}/UE_${ENGINE_VERSION}" "/opt/UnrealEngine" "/opt/UE_${ENGINE_VERSION}"; do
        if [ -d "${candidate}/Engine" ]; then ENGINE="${candidate}"; break; fi
      done
      ENGINE="${ENGINE:-${HOME}/UnrealEngine}" ;;
  esac
fi

PY="$(command -v python3 || command -v python || true)"
[ -n "${PY}" ] || fail "Python 3.9+ is needed for the downloader (https://www.python.org/downloads/)."
"${PY}" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' || fail "${PY} is older than Python 3.9."

# 1. Download ---------------------------------------------------------------------------------
if [ "${SKIP_FETCH}" -eq 0 ]; then
  step "Downloading CC0 assets from Poly Haven into Game/RawAssets/PolyHaven"
  FETCH_ARGS=()
  [ -n "${RESOLUTION}" ] && FETCH_ARGS+=(--resolution "${RESOLUTION}")
  [ -n "${ONLY}" ] && FETCH_ARGS+=(--only "${ONLY}")
  set +e
  "${PY}" "${FETCHER}" ${FETCH_ARGS[@]+"${FETCH_ARGS[@]}"}
  STATUS=$?
  set -e
  case "${STATUS}" in
    0) done_ "Downloads complete." ;;
    1) warn "Some assets failed to download; importing what arrived. Run this again later to resume." ;;
    3) warn "Poly Haven is unreachable; importing whatever was downloaded before (flat materials for the rest)." ;;
    *) fail "The downloader stopped with an error (exit ${STATUS})." ;;
  esac
fi

[ "${SKIP_IMPORT}" -eq 1 ] && { done_ "Skipping the Unreal import (--skip-import)."; exit 0; }

# 2. Find the editor and make sure the game module is built -----------------------------------
find_editor() {
  for candidate in \
    "${ENGINE}/Engine/Binaries/Linux/UnrealEditor-Cmd" \
    "${ENGINE}/Engine/Binaries/Linux/UnrealEditor" \
    "${ENGINE}/Engine/Binaries/Mac/UnrealEditor-Cmd" \
    "${ENGINE}/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" \
    "${ENGINE}/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"; do
    if [ -x "${candidate}" ]; then echo "${candidate}"; return 0; fi
  done
  return 1
}
UE_CMD="${UE_EDITOR_CMD:-$(find_editor || true)}"
if [ -z "${UE_CMD}" ] || [ ! -x "${UE_CMD}" ]; then
  fail "Unreal Editor not found under '${ENGINE}'.
Pass --engine <folder that contains Engine/>, set UE_ROOT, or set UE_EDITOR_CMD to the editor executable.
The downloads are kept; re-run with --skip-fetch once the engine path is right."
fi
done_ "Editor: ${UE_CMD}"

if ! ls "${ROOT}/Game/Binaries/${PLATFORM}/"*UnrealEditor-Inheritance* >/dev/null 2>&1; then
  step "Building the Inheritance editor module (first run only)"
  BUILD_SCRIPT="${ENGINE}/Engine/Build/BatchFiles/${PLATFORM}/Build.sh"
  if [ -x "${BUILD_SCRIPT}" ]; then
    "${BUILD_SCRIPT}" InheritanceEditor "${PLATFORM}" Development -Project="${UPROJECT}" -WaitMutex \
      || fail "The C++ build failed. Open Game/Inheritance.uproject once to see the errors, then run this again."
  else
    warn "No ${BUILD_SCRIPT}; if the import below says modules are missing, open the project once to build them."
  fi
fi

# 3. Import -----------------------------------------------------------------------------------
step "Importing into Unreal headless (the first run compiles textures and shaders; it can take a while)"
[ -f "${SCRIPT}" ] || fail "Missing ${SCRIPT}."
set +e
# Only the file name: the commandlet splits a -script= path at spaces and runs the pieces as code;
# a bare name is found on the Python plugin's sys.path, which always holds <Project>/Content/Python.
INH_ART_ONLY="${ONLY}" "${UE_CMD}" "${UPROJECT}" -run=pythonscript -script="$(basename "${SCRIPT}")" \
  -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput \
  -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities
STATUS=$?
set -e
if [ "${STATUS}" -ne 0 ]; then
  fail "The Unreal import reported problems (exit ${STATUS}). Search the output above, or Game/Saved/Logs/Inheritance.log, for [InhArt]."
fi
done_ "Art built under /Game/Inheritance. Open the project and press Play."
