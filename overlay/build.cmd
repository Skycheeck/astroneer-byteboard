@echo off
rem Builds overlay\bin\ResearchOverlay.dll, ResearchOverlayPreview.dll and preview.exe (Release) with
rem the CMake and MSVC of the newest Visual Studio or Build Tools that has the C++ x64 tools.
setlocal

rem The only build prerequisite is the Dear ImGui submodule.
if not exist "%~dp0third_party\imgui\imgui.cpp" (
    echo Dear ImGui is missing: run "git submodule update --init" in the repository first.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio or its Build Tools are not installed.
    exit /b 1
)
set "VSQUERY=-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
set "CMAKE="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" %VSQUERY% -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) do set "CMAKE=%%i"
if not defined CMAKE (
    echo CMake from Visual Studio was not found: add the component "C++ CMake tools for Windows".
    exit /b 1
)

rem The generator matches that installation: "Visual Studio <major> <year>".
set "VSMAJOR="
set "VSYEAR="
for /f "usebackq tokens=1 delims=." %%i in (`"%VSWHERE%" %VSQUERY% -property installationVersion`) do set "VSMAJOR=%%i"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" %VSQUERY% -property catalog_productLineVersion`) do set "VSYEAR=%%i"
if not defined VSMAJOR (
    echo Cannot read the Visual Studio version.
    exit /b 1
)

"%CMAKE%" -S "%~dp0." -B "%~dp0build" -G "Visual Studio %VSMAJOR% %VSYEAR%" -A x64 || exit /b 1
"%CMAKE%" --build "%~dp0build" --config Release || exit /b 1
echo Built: %~dp0bin\ResearchOverlay.dll

rem The overlay reads the game data only at run time, so the build does not need it (a machine
rem without the game can still compile). Without it the overlay stays off or loses its groups.
if not exist "%~dp0catalog.tsv" echo Warning: overlay\catalog.tsv is missing, so the overlay stays off: run "node tools/extract.mjs" in the repository.
if not exist "%~dp0taxonomy.tsv" echo Warning: overlay\taxonomy.tsv is missing, so all items fall into one fallback group: run "node tools/extract.mjs" in the repository.
exit /b 0
