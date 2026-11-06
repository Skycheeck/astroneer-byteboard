@echo off
rem Builds overlay\bin\Byteboard.dll, ByteboardPreview.dll, preview.exe and byteboard-extract.exe
rem (Release) with the CMake and MSVC of the newest Visual Studio or Build Tools that has the C++ x64
rem tools, then copies Byteboard.dll into mod\Byteboard, where the Lua mod loads it.
setlocal

rem The only build prerequisites are the git submodules (Dear ImGui and zlib).
if not exist "%~dp0third_party\imgui\imgui.cpp" goto :nosubmodules
if not exist "%~dp0third_party\zlib\inflate.c" goto :nosubmodules

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
echo Built: %~dp0bin\Byteboard.dll

rem The game keeps the loaded DLL locked, so the copy fails while the game runs.
copy /y "%~dp0bin\Byteboard.dll" "%~dp0..\mod\Byteboard\Byteboard.dll" >nul || (
    echo Cannot copy Byteboard.dll into mod\Byteboard: close the game and run this script again.
    exit /b 1
)
echo Copied to: mod\Byteboard\Byteboard.dll
exit /b 0

:nosubmodules
echo Dear ImGui or zlib is missing: run "git submodule update --init" in the repository first.
exit /b 1
