<#
.SYNOPSIS
  Installs everything needed to develop Inheritance on Windows and build it for Android.

.DESCRIPTION
  Installs with winget: Git (+LFS), Node.js LTS, Visual Studio 2022 with the C++ game workloads,
  Android Studio, and the Epic Games Launcher. Unreal Engine itself must be installed from the
  launcher (it needs your Epic account), so the script pauses for that, then runs the engine's
  own Android SDK/NDK setup and installs the server's dependencies.

  Run from an elevated PowerShell:
    Set-ExecutionPolicy -Scope Process Bypass
    .\tools\setup\setup-windows.ps1

.PARAMETER EngineVersion
  The Unreal Engine version you install from the launcher (default 5.6).
#>
param(
    [string]$EngineVersion = "5.6"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

function Step($Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Done($Message) { Write-Host "    $Message" -ForegroundColor Green }
function Warn($Message) { Write-Host "    $Message" -ForegroundColor Yellow }

function Install-Winget($Id, $ExtraArgs = @()) {
    $installed = winget list --id $Id --exact --accept-source-agreements 2>$null | Select-String $Id
    if ($installed) { Done "$Id already installed"; return }
    winget install --id $Id --exact --silent --accept-package-agreements --accept-source-agreements @ExtraArgs
    Done "$Id installed"
}

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Warn "Not running as Administrator. Some installers may ask for permission."
}
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw "winget is missing. Install 'App Installer' from the Microsoft Store, then run this again."
}

Step "Git and Git LFS"
Install-Winget "Git.Git"
Install-Winget "GitHub.GitLFS"
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
git lfs install | Out-Null

Step "Node.js LTS (runs the Inheritance server)"
Install-Winget "OpenJS.NodeJS.LTS"

Step "Visual Studio 2022 Community with C++ game development (about 25 GB)"
$vsWorkloads = @(
    "--add Microsoft.VisualStudio.Workload.NativeGame",
    "--add Microsoft.VisualStudio.Workload.NativeDesktop",
    "--add Microsoft.VisualStudio.Workload.ManagedDesktop",
    "--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
    "--add Microsoft.VisualStudio.Component.Windows11SDK.22621",
    "--add Microsoft.Net.Component.4.6.2.TargetingPack",
    "--includeRecommended",
    "--passive",
    "--norestart"
) -join " "
Install-Winget "Microsoft.VisualStudio.2022.Community" @("--override", $vsWorkloads)

Step "Android Studio (Android SDK, build tools and the Java runtime Unreal uses)"
Install-Winget "Google.AndroidStudio"

Step "Epic Games Launcher"
Install-Winget "EpicGames.EpicGamesLauncher"

Step "Unreal Engine $EngineVersion"
$engineRoot = "C:\Program Files\Epic Games\UE_$EngineVersion"
if (-not (Test-Path "$engineRoot\Engine\Binaries\Win64\UnrealEditor.exe")) {
    Warn "Unreal Engine has to be installed from the Epic Games Launcher (it needs your Epic account):"
    Warn "  1. Open the Epic Games Launcher and sign in."
    Warn "  2. Unreal Engine > Library > '+' > choose $EngineVersion > Install."
    Warn "  3. In the install Options, tick 'Android' and (optional) 'iOS'."
    Start-Process "com.epicgames.launcher://ue/library" -ErrorAction SilentlyContinue
    Read-Host "Press Enter once Unreal Engine $EngineVersion has finished installing"
}
if (-not (Test-Path $engineRoot)) {
    $engineRoot = Read-Host "Engine not found at the default path. Paste your UE_$EngineVersion folder"
}
Done "Engine at $engineRoot"

Step "Android SDK, NDK and platform tools for Unreal"
$androidStudio = "C:\Program Files\Android\Android Studio"
if (Test-Path "$androidStudio\jbr") {
    [Environment]::SetEnvironmentVariable("JAVA_HOME", "$androidStudio\jbr", "User")
    $env:JAVA_HOME = "$androidStudio\jbr"
    Done "JAVA_HOME = $env:JAVA_HOME"
}
$sdkRoot = Join-Path $env:LOCALAPPDATA "Android\Sdk"
if (-not (Test-Path "$sdkRoot\cmdline-tools")) {
    Warn "Open Android Studio once, finish its setup wizard (Standard), then:"
    Warn "  More Actions > SDK Manager > SDK Tools > tick 'Android SDK Command-line Tools (latest)' > Apply."
    Start-Process "$androidStudio\bin\studio64.exe" -ErrorAction SilentlyContinue
    Read-Host "Press Enter when that is done"
}
$setupAndroid = Join-Path $engineRoot "Engine\Extras\Android\SetupAndroid.bat"
if (Test-Path $setupAndroid) {
    # The engine's script installs exactly the SDK platform and NDK version this engine expects.
    & cmd /c "`"$setupAndroid`""
    Done "Android toolchain ready (restart your PC so new environment variables apply)"
} else {
    Warn "SetupAndroid.bat not found. Re-run the launcher install with the Android option ticked."
}

Step "Server dependencies and tests"
Push-Location (Join-Path $RepoRoot "server")
npm test
Pop-Location

Step "Generate Visual Studio project files"
$uproject = Join-Path $RepoRoot "Game\Inheritance.uproject"
& "$engineRoot\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="$uproject" -game -progress
Done "Open Game\Inheritance.sln in Visual Studio, or double-click Game\Inheritance.uproject."

Write-Host "`nAll set. Next: docs\SETUP.md 'First run'." -ForegroundColor Green
