#!/usr/bin/env bash
# Installs everything needed to develop Inheritance on a Mac and build it for iOS (and Android).
#
#   chmod +x tools/setup/setup-macos.sh && ./tools/setup/setup-macos.sh [engine-version]
#
# Installs Homebrew, Git (+LFS), Node.js, Xcode (from the App Store via `mas`), Android Studio and
# the Epic Games Launcher. Unreal Engine itself must be installed from the launcher because it
# needs your Epic account; the script pauses for that.
set -euo pipefail

ENGINE_VERSION="${1:-5.6}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }
done_() { printf '    \033[32m%s\033[0m\n' "$1"; }
warn() { printf '    \033[33m%s\033[0m\n' "$1"; }

step "Homebrew"
if ! command -v brew >/dev/null; then
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  eval "$(/opt/homebrew/bin/brew shellenv 2>/dev/null || /usr/local/bin/brew shellenv)"
fi
done_ "brew $(brew --version | head -1)"

step "Git, Git LFS, Node.js"
brew install git git-lfs node mas
git lfs install >/dev/null

step "Xcode (required for iOS builds, about 15 GB)"
if ! xcode-select -p >/dev/null 2>&1 || [ ! -d /Applications/Xcode.app ]; then
  warn "Sign in to the App Store first if this fails."
  mas install 497799835 || warn "Install Xcode from the App Store manually, then re-run."
fi
if [ -d /Applications/Xcode.app ]; then
  sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
  sudo xcodebuild -license accept
  xcodebuild -runFirstLaunch
  done_ "$(xcodebuild -version | head -1)"
fi

step "Android Studio (for Android builds from this Mac)"
brew install --cask android-studio || true

step "Epic Games Launcher"
brew install --cask epic-games || true

ENGINE_ROOT="/Users/Shared/Epic Games/UE_${ENGINE_VERSION}"
step "Unreal Engine ${ENGINE_VERSION}"
if [ ! -d "${ENGINE_ROOT}/Engine" ]; then
  warn "Install Unreal Engine from the Epic Games Launcher (needs your Epic account):"
  warn "  Unreal Engine > Library > '+' > ${ENGINE_VERSION} > Install > Options: tick iOS (and Android)."
  open -a "Epic Games Launcher" || true
  read -r -p "    Press Enter once Unreal Engine ${ENGINE_VERSION} is installed..."
fi
done_ "Engine at ${ENGINE_ROOT}"

if [ -x "${ENGINE_ROOT}/Engine/Extras/Android/SetupAndroid.command" ]; then
  step "Android SDK/NDK for Unreal"
  warn "Open Android Studio once and install 'Android SDK Command-line Tools' from SDK Manager first."
  "${ENGINE_ROOT}/Engine/Extras/Android/SetupAndroid.command" || warn "Run SetupAndroid.command again after Android Studio setup."
fi

step "Server dependencies and tests"
(cd "${REPO_ROOT}/server" && npm test)

step "Generate the Xcode workspace"
"${ENGINE_ROOT}/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="${REPO_ROOT}/Game/Inheritance.uproject" -game || \
  warn "Right-click Game/Inheritance.uproject > Services > Generate Xcode Project instead."

cat <<MSG

All set. For iOS you also need:
  - An Apple Developer account (https://developer.apple.com/programs/) for device builds.
  - In Unreal: Project Settings > Platforms > iOS > set your Team ID and signing, or use
    'Automatic Signing'.
Next: docs/SETUP.md "First run", then docs/MOBILE.md.
MSG
