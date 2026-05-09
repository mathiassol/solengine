@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "OUT=%BUILD%\out\Release"

echo [SolEngine] Building...
cd /d "%ROOT%"
cmake --build "%BUILD%" --config Release
if errorlevel 1 (
    echo.
    echo [SolEngine] Build FAILED
    pause
    exit /b 1
)

echo.
echo [SolEngine] Running model viewer...
echo   Controls: WASD = move    Mouse/Arrows = look    Click = capture mouse    Esc = release
echo             N/P = cycle models    Tab = toggle UI
echo.
cd /d "%ROOT%demo"
"%OUT%\sol.exe" run "%OUT%\demo.dll"
