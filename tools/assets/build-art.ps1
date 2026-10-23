<#
.SYNOPSIS
  One command for Inheritance's real art: downloads CC0 assets from Poly Haven and imports them into Unreal.

.DESCRIPTION
  1. Runs tools/assets/fetch_cc0_assets.py, which downloads the Poly Haven assets named in
     Game/Content/Data/Art/ArtManifest.json into Game/RawAssets/PolyHaven (resumable).
  2. Builds the Inheritance editor module if it has never been built.
  3. Runs Game/Content/Python/inh_build_art.py headless in UnrealEditor-Cmd, which imports everything
     and authors the materials under /Game/Inheritance.
  See docs/ART_PIPELINE.md.

    .\tools\assets\build-art.ps1
    .\tools\assets\build-art.ps1 -Resolution 1k -Only FloorWood,Table
    .\tools\assets\build-art.ps1 -EnginePath "D:\Epic\UE_5.5" -SkipFetch

.PARAMETER EnginePath
  The engine folder that contains Engine\ (default: $env:UE_ROOT, else C:\Program Files\Epic Games\UE_<EngineVersion>).

.PARAMETER EngineVersion
  Used for the default EnginePath (default 5.6).

.PARAMETER Resolution
  Texture/model resolution to download: 1k, 2k or 4k (default: the manifest's, 2k).

.PARAMETER Only
  Comma-separated art keys to rebuild, e.g. FloorWood,Table,Night.

.PARAMETER SkipFetch
  Import what is already downloaded.

.PARAMETER SkipImport
  Only download.
#>
param(
    [string]$EnginePath = "",
    [string]$EngineVersion = "5.6",
    [string]$Resolution = "",
    [string]$Only = "",
    [switch]$SkipFetch,
    [switch]$SkipImport
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$UProject = Join-Path $RepoRoot "Game\Inheritance.uproject"
$ImportScript = Join-Path $RepoRoot "Game\Content\Python\inh_build_art.py"
$Fetcher = Join-Path $RepoRoot "tools\assets\fetch_cc0_assets.py"

function Step($Message) { Write-Host "`n==> $Message" -ForegroundColor Cyan }
function Done($Message) { Write-Host "    $Message" -ForegroundColor Green }
function Warn($Message) { Write-Host "    $Message" -ForegroundColor Yellow }
function Fail($Message) { Write-Host "`n$Message" -ForegroundColor Red; exit 1 }

function Find-Python {
    # Windows PowerShell turns redirected stderr into terminating errors under "Stop"; probing must not abort.
    $ErrorActionPreference = "Continue"
    # The py launcher first: "python" on a fresh Windows can be the Microsoft Store stub.
    foreach ($candidate in @(@("py", "-3"), @("python"), @("python3"))) {
        $command = Get-Command $candidate[0] -ErrorAction SilentlyContinue
        if (-not $command) { continue }
        $extra = @($candidate | Select-Object -Skip 1)
        # $null = keeps any probe output (e.g. the Store stub's message) out of this function's return value.
        $null = & $command.Source @extra -c "import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)" 2>$null
        # The comma stops PowerShell unrolling a one-element array into a bare string ($python[0] would be 'C').
        if ($LASTEXITCODE -eq 0) { return ,(@($command.Source) + $extra) }
    }
    return $null
}

if (-not $EnginePath) {
    $EnginePath = if ($env:UE_ROOT) { $env:UE_ROOT } else { "C:\Program Files\Epic Games\UE_$EngineVersion" }
}

# 1. Download ---------------------------------------------------------------------------------
if (-not $SkipFetch) {
    Step "Downloading CC0 assets from Poly Haven into Game\RawAssets\PolyHaven"
    $python = Find-Python
    if (-not $python) { Fail "Python 3.9+ is needed for the downloader. Install it with: winget install Python.Python.3.12" }
    $pythonExe = $python[0]
    $pythonArgs = @($python | Select-Object -Skip 1)
    $fetchArgs = @($Fetcher)
    if ($Resolution) { $fetchArgs += @("--resolution", $Resolution) }
    if ($Only) { $fetchArgs += @("--only", $Only) }
    & $pythonExe @pythonArgs @fetchArgs
    switch ($LASTEXITCODE) {
        0 { Done "Downloads complete." }
        1 { Warn "Some assets failed to download; importing what arrived. Run this again later to resume." }
        3 { Warn "Poly Haven is unreachable; importing whatever was downloaded before (flat materials for the rest)." }
        default { Fail "The downloader stopped with an error (exit $LASTEXITCODE)." }
    }
}

if ($SkipImport) { Done "Skipping the Unreal import (-SkipImport)."; exit 0 }

# 2. Find the editor and make sure the game module is built -----------------------------------
$editorCmd = if ($env:UE_EDITOR_CMD) { $env:UE_EDITOR_CMD } else { Join-Path $EnginePath "Engine\Binaries\Win64\UnrealEditor-Cmd.exe" }
if (-not (Test-Path $editorCmd)) {
    Fail ("Unreal Editor not found at '$editorCmd'.`n" +
        "Pass -EnginePath <folder that contains Engine\>, set `$env:UE_ROOT, or set `$env:UE_EDITOR_CMD.`n" +
        "The downloads are kept; re-run with -SkipFetch once the engine path is right.")
}
Done "Editor: $editorCmd"

if (-not (Test-Path (Join-Path $RepoRoot "Game\Binaries\Win64\UnrealEditor-Inheritance.dll"))) {
    Step "Building the Inheritance editor module (first run only)"
    $buildBat = Join-Path $EnginePath "Engine\Build\BatchFiles\Build.bat"
    if (Test-Path $buildBat) {
        & $buildBat InheritanceEditor Win64 Development "-Project=$UProject" -WaitMutex
        if ($LASTEXITCODE -ne 0) { Fail "The C++ build failed. Open Game\Inheritance.uproject once to see the errors, then run this again." }
    } else {
        Warn "No $buildBat; if the import below says modules are missing, open the project once to build them."
    }
}

# 3. Import -----------------------------------------------------------------------------------
Step "Importing into Unreal headless (the first run compiles textures and shaders; it can take a while)"
if (-not (Test-Path $ImportScript)) { Fail "Missing $ImportScript." }
$env:INH_ART_ONLY = $Only
# Only the file name: the commandlet un-escapes a quoted -script= value (so C:\Users breaks) and splits at spaces;
# a bare name is found on the Python plugin's sys.path, which always holds <Project>\Content\Python.
$importArgs = "`"$UProject`" -run=pythonscript -script=$(Split-Path $ImportScript -Leaf) -unattended -nop4 -nosplash -NullRHI " +
    "-stdout -FullStdOutLogOutput -EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities"
$process = Start-Process -FilePath $editorCmd -ArgumentList $importArgs -NoNewWindow -Wait -PassThru
Remove-Item Env:\INH_ART_ONLY -ErrorAction SilentlyContinue
if ($process.ExitCode -ne 0) {
    Fail "The Unreal import reported problems (exit $($process.ExitCode)). Search the output above, or Game\Saved\Logs\Inheritance.log, for [InhArt]."
}
Done "Art built under /Game/Inheritance. Open the project and press Play."
