#!/usr/bin/env bash
# Builds, cooks and packages Inheritance for iOS. Run on a Mac with Xcode and signing set up.
# Usage: tools/build/package-ios.sh [engine-root] [Development|Shipping]
set -euo pipefail
ENGINE="${1:-/Users/Shared/Epic Games/UE_5.6}"
CONFIG="${2:-Development}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

"${ENGINE}/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="${ROOT}/Game/Inheritance.uproject" -noP4 \
  -platform=IOS -clientconfig="${CONFIG}" \
  -build -cook -stage -pak -package -archive -archivedirectory="${ROOT}/Builds/IOS" -utf8output

echo "The .ipa is in ${ROOT}/Builds/IOS. Install it with Xcode (Window > Devices and Simulators) or upload to TestFlight."
