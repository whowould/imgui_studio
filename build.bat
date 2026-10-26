@echo off
rem Builds bin\imgui_studio.exe with Visual Studio 2022's CMake.
setlocal
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -property installationPath`) do set VS=%%i
set CMAKE="%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist %CMAKE% (
    echo Visual Studio 2022 with C++ CMake tools is required.
    exit /b 1
)
%CMAKE% -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A x64 || exit /b 1
%CMAKE% --build "%~dp0build" --config Release || exit /b 1
echo.
echo Built: %~dp0bin\imgui_studio.exe
