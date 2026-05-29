@echo off
cd /d "%~dp0"

echo.
echo === SolEngine — push to GitHub ===
echo.

git status --short
echo.

set /p MSG="Commit message (leave blank for timestamp): "
if "%MSG%"=="" (
    for /f "tokens=1-3 delims=/ " %%a in ("%date%") do set D=%%c-%%b-%%a
    for /f "tokens=1-2 delims=: " %%a in ("%time: =0%") do set T=%%a:%%b
    set MSG=update %D% %T%
)

git add .
git commit -m "%MSG%"
if errorlevel 1 (
    echo.
    echo Nothing to commit.
    pause
    exit /b 0
)

git push
if errorlevel 1 (
    echo.
    echo Push failed. Check your remote: git remote -v
    pause
    exit /b 1
)

echo.
echo Done.
echo.
pause
