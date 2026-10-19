@echo off
rem Builds, cooks and packages Inheritance for Android (arm64, ASTC textures).
rem Usage: tools\build\package-android.bat [EngineRoot] [Development|Shipping]
setlocal
set ENGINE=%~1
if "%ENGINE%"=="" set ENGINE=C:\Program Files\Epic Games\UE_5.6
set CONFIG=%~2
if "%CONFIG%"=="" set CONFIG=Development
set ROOT=%~dp0..\..
set PROJECT=%ROOT%\Game\Inheritance.uproject
set OUT=%ROOT%\Builds\Android

call "%ENGINE%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%PROJECT%" -noP4 ^
  -platform=Android -cookflavor=ASTC -clientconfig=%CONFIG% ^
  -build -cook -stage -pak -package -archive -archivedirectory="%OUT%" -utf8output
if errorlevel 1 exit /b 1
echo.
echo APK and install script are in %OUT%\Android_ASTC
echo Plug in a phone with USB debugging on and run Install_Inheritance-arm64.bat from there.
