#!/usr/bin/env bash
# Builds, cooks and packages Inheritance for iOS. Run on a Mac with Xcode and signing set up.
# Usage: tools/build/package-ios.sh [engine-root] [Development|Shipping] [--allow-no-art]
# Stops if the art hasn't been built (tools/assets/build-art.sh); --allow-no-art packages the flat fallback anyway.
set -euo pipefail
ALLOW_NO_ART=0
ARGS=()
for arg in "$@"; do
  if [ "${arg}" = "--allow-no-art" ]; then ALLOW_NO_ART=1; else ARGS+=("${arg}"); fi
done
ENGINE="${ARGS[0]:-/Users/Shared/Epic Games/UE_5.6}"
CONFIG="${ARGS[1]:-Development}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# The game finds the art by MI_FloorWood, which every art build writes (World/InhArt.cpp).
if [ "${ALLOW_NO_ART}" -eq 0 ] && [ ! -f "${ROOT}/Game/Content/Inheritance/Materials/MI_FloorWood.uasset" ]; then
  echo "The art hasn't been built: Game/Content/Inheritance/Materials/MI_FloorWood.uasset is missing." >&2
  echo "Without it a phone build may draw most of the town with the engine's grey grid material." >&2
  echo "Run tools/assets/build-art.sh first (docs/ART_PIPELINE.md), or add --allow-no-art to package the flat-colour fallback anyway." >&2
  exit 1
fi

"${ENGINE}/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="${ROOT}/Game/Inheritance.uproject" -noP4 \
  -platform=IOS -clientconfig="${CONFIG}" \
  -build -cook -stage -pak -package -archive -archivedirectory="${ROOT}/Builds/IOS" -utf8output

echo "The .ipa is in ${ROOT}/Builds/IOS. Install it with Xcode (Window > Devices and Simulators) or upload to TestFlight."
