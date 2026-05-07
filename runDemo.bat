@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"
set "OUT=%BUILD%\out"

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
echo [SolEngine] Running demo...
cd /d "%ROOT%demo"
"%OUT%\sol.exe" "%OUT%\demo.dll"
