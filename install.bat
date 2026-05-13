@echo off
setlocal EnableDelayedExpansion

:: ─────────────────────────────────────────────────────────────────────────────
::  SolEngine dev-install script
::  Builds sol.exe + sol_engine.dll and installs them to %LOCALAPPDATA%\SolEngine\bin
::  Adds that directory to the current user's PATH (permanent, no admin needed).
:: ─────────────────────────────────────────────────────────────────────────────

set "REPO=%~dp0"
set "REPO=%REPO:~0,-1%"
set "BUILD_DIR=%REPO%\build"
set "BIN_SRC=%BUILD_DIR%\out\Release"
set "INSTALL_DIR=%LOCALAPPDATA%\SolEngine\bin"

echo.
echo  SolEngine Installer
echo  ════════════════════════════════════════════
echo  Repo     : %REPO%
echo  Build    : %BUILD_DIR%
echo  Install  : %INSTALL_DIR%
echo  ════════════════════════════════════════════
echo.

:: ── Step 1: Build ─────────────────────────────────────────────────────────────
echo [1/3] Building SolEngine...
echo.

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo   Configuring CMake...
    cmake -S "%REPO%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo.
        echo  ERROR: CMake configure failed.
        pause & exit /b 1
    )
)

cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 (
    echo.
    echo  ERROR: Build failed.
    pause & exit /b 1
)
echo.
echo   Build complete.

:: ── Step 2: Install files ─────────────────────────────────────────────────────
echo.
echo [2/3] Installing to %INSTALL_DIR%...

if not exist "%INSTALL_DIR%" (
    mkdir "%INSTALL_DIR%"
)

copy /Y "%BIN_SRC%\sol.exe"         "%INSTALL_DIR%\sol.exe"         >nul
copy /Y "%BIN_SRC%\sol_engine.dll"  "%INSTALL_DIR%\sol_engine.dll"  >nul
if exist "%BIN_SRC%\sol_editor.exe" (
    copy /Y "%BIN_SRC%\sol_editor.exe" "%INSTALL_DIR%\sol_editor.exe" >nul
    echo   Installed: sol_editor.exe
    REM Copy Qt runtime DLLs if present
    for %%f in ("%BIN_SRC%\Qt6*.dll" "%BIN_SRC%\platforms" "%BIN_SRC%\styles") do (
        if exist "%%f" xcopy /Y /E /Q "%%f" "%INSTALL_DIR%\" >nul 2>&1
    )
)

if errorlevel 1 (
    echo  ERROR: Failed to copy files.
    pause & exit /b 1
)
echo   Installed: sol.exe
echo   Installed: sol_engine.dll

:: ── Step 3: Add to user PATH (via PowerShell — no 1024-char limit) ────────────
echo.
echo [3/3] Adding to user PATH...

powershell -NoProfile -Command ^
    "$dir = '%INSTALL_DIR%';" ^
    "$cur = [Environment]::GetEnvironmentVariable('PATH','User');" ^
    "$entries = ($cur -split ';') | Where-Object { $_ -and $_ -notlike '*SolEngine*' };" ^
    "$entries += $dir;" ^
    "[Environment]::SetEnvironmentVariable('PATH', ($entries -join ';'), 'User');" ^
    "Write-Host '  PATH updated: ' $dir"

:: ── Done ──────────────────────────────────────────────────────────────────────
echo.
echo  ════════════════════════════════════════════
echo   Install complete!
echo.
echo   Open a NEW terminal and run:
echo.
echo     sol version
echo     sol help
echo     sol run .             (from a project dir)
echo.
echo   To update, just run install.bat again.
echo  ════════════════════════════════════════════
echo.

endlocal
pause
