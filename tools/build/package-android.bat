@echo off
rem Builds, cooks and packages Inheritance for Android (arm64, ASTC textures).
rem Usage: tools\build\package-android.bat [EngineRoot] [Development|Shipping] [--allow-no-art]
rem Stops if the art hasn't been built (tools\assets\build-art.ps1); --allow-no-art packages the flat fallback anyway.
setlocal
set ALLOW_NO_ART=0
for %%A in (%*) do if /i "%%~A"=="--allow-no-art" set ALLOW_NO_ART=1
set ENGINE=%~1
if /i "%ENGINE%"=="--allow-no-art" set ENGINE=
if "%ENGINE%"=="" set ENGINE=C:\Program Files\Epic Games\UE_5.6
set CONFIG=%~2
if /i "%CONFIG%"=="--allow-no-art" set CONFIG=
if "%CONFIG%"=="" set CONFIG=Development
set ROOT=%~dp0..\..
set PROJECT=%ROOT%\Game\Inheritance.uproject
set OUT=%ROOT%\Builds\Android

rem The game finds the art by MI_FloorWood, which every art build writes (World/InhArt.cpp).
if "%ALLOW_NO_ART%"=="1" goto art_checked
if exist "%ROOT%\Game\Content\Inheritance\Materials\MI_FloorWood.uasset" goto art_checked
echo The art hasn't been built: Game\Content\Inheritance\Materials\MI_FloorWood.uasset is missing.
echo Without it a phone build may draw most of the town with the engine's grey grid material.
echo Run tools\assets\build-art.ps1 first, see docs\ART_PIPELINE.md.
echo To package the flat-colour fallback anyway, add --allow-no-art.
exit /b 1
:art_checked

call "%ENGINE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 ^
  -platform=Android -cookflavor=ASTC -clientconfig=%CONFIG% ^
  -build -cook -stage -pak -package -archive -archivedirectory="%OUT%" -utf8output
if errorlevel 1 exit /b 1
echo.
echo APK and install script are in %OUT%\Android_ASTC
echo Plug in a phone with USB debugging on and run Install_Inheritance-arm64.bat from there.
