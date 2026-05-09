@echo off
setlocal

set "ROOT=%~dp0"
set "BUILD=%ROOT%build"

echo [SolEngine] Configuring...
cd /d "%ROOT%"
cmake -S . -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [SolEngine] CMake configure FAILED
    pause
    exit /b 1
)

echo.
echo [SolEngine] Building...
cmake --build "%BUILD%" --config Release
if errorlevel 1 (
    echo.
    echo [SolEngine] Build FAILED
    pause
    exit /b 1
)

echo.
echo [SolEngine] Build complete. Binaries in: %BUILD%\out\Release\
