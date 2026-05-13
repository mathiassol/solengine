@echo off
REM Build and launch the SolEngine editor
setlocal

set "REPO=%~dp0"
set "REPO=%REPO:~0,-1%"

echo [sol] Building engine + editor...
cmake --build "%REPO%\build" --config Release --target sol_editor
if errorlevel 1 (
    echo [sol] Build failed.
    pause
    exit /b 1
)

echo [sol] Launching sol_editor...
start "" "%REPO%\build\out\Release\sol_editor.exe" "%CD%"

endlocal
