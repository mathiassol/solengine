@echo off
setlocal EnableDelayedExpansion

:: ─────────────────────────────────────────────────────────────────────────────
::  SolEngine install script
::  - Builds a Release of the full engine + editor
::  - Installs everything to %LOCALAPPDATA%\SolEngine\bin
::  - Creates Desktop + Start Menu shortcuts → sol_editor.exe (project launcher)
::  - Registers in Windows "Apps & Features" (user-scope, no admin needed)
::  - Adds bin dir to the current user's PATH
:: ─────────────────────────────────────────────────────────────────────────────

set "REPO=%~dp0"
set "REPO=%REPO:~0,-1%"
set "BUILD_DIR=%REPO%\build"
set "BIN_SRC=%BUILD_DIR%\out\Release"
set "INSTALL_DIR=%LOCALAPPDATA%\SolEngine\bin"
set "APP_VERSION=0.1.0"

echo.
echo  SolEngine Installer  v%APP_VERSION%
echo  ════════════════════════════════════════════
echo  Repo    : %REPO%
echo  Install : %INSTALL_DIR%
echo  ════════════════════════════════════════════
echo.

:: ── Step 1: CMake configure (only if needed) ──────────────────────────────────
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [1/5] Configuring CMake...
    cmake -S "%REPO%" -B "%BUILD_DIR%"
    if errorlevel 1 (
        echo  ERROR: CMake configure failed.
        pause & exit /b 1
    )
) else (
    echo [1/5] CMake already configured - skipping.
)

:: ── Step 2: Build Release ─────────────────────────────────────────────────────
echo.
echo [2/5] Building Release...
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo  ERROR: Build failed.
    pause & exit /b 1
)
echo   Build complete.

:: ── Step 3: Copy all binaries to install dir ──────────────────────────────────
echo.
echo [3/5] Installing to %INSTALL_DIR%...

if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

:: Core engine binaries
copy /Y "%BIN_SRC%\sol.exe"         "%INSTALL_DIR%\sol.exe"         >nul 2>&1
copy /Y "%BIN_SRC%\sol_engine.dll"  "%INSTALL_DIR%\sol_engine.dll"  >nul 2>&1

:: Editor + all Qt DLLs/plugins windeployqt placed next to it
if exist "%BIN_SRC%\sol_editor.exe" (
    xcopy /Y /E /Q "%BIN_SRC%\*" "%INSTALL_DIR%\" >nul 2>&1
    echo   Installed editor + Qt runtime.
) else (
    echo   WARNING: sol_editor.exe not found in %BIN_SRC% - editor not installed.
)

echo   Installed: sol.exe  sol_engine.dll  sol_editor.exe

:: ── Step 4: Shortcuts + App registration (PowerShell) ─────────────────────────
echo.
echo [4/5] Creating shortcuts and registering application...

powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%\tools\install_shortcuts.ps1" -InstallDir "%INSTALL_DIR%" -AppVersion "%APP_VERSION%"
if errorlevel 1 (
    echo  WARNING: Shortcut creation failed - you can still run sol_editor.exe directly.
)

:: ── Step 5: Add bin dir to user PATH ──────────────────────────────────────────
echo.
echo [5/5] Updating PATH...

powershell -NoProfile -Command ^
    "$dir = '%INSTALL_DIR%';" ^
    "$cur = [Environment]::GetEnvironmentVariable('PATH','User');" ^
    "$entries = ($cur -split ';') | Where-Object { $_ -and $_ -notlike '*SolEngine*' };" ^
    "$entries += $dir;" ^
    "[Environment]::SetEnvironmentVariable('PATH', ($entries -join ';'), 'User');" ^
    "Write-Host '  PATH updated'"

:: ── Done ──────────────────────────────────────────────────────────────────────
echo.
echo  ════════════════════════════════════════════
echo   Install complete!
echo.
echo   Double-click the SolEngine shortcut on your
echo   Desktop (or search Start Menu) to open the
echo   project launcher.
echo.
echo   Command-line tools (new terminal):
echo     sol run .        (run project in cwd)
echo     sol version
echo.
echo   To reinstall / update, run install.bat again.
echo  ════════════════════════════════════════════
echo.

endlocal
pause
