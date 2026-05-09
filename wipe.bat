@echo off
setlocal

set "ROOT=%~dp0"

echo [SolEngine] Wiping build directories...

if exist "%ROOT%build" (
    rmdir /s /q "%ROOT%build"
    echo   Removed: build\
)

if exist "%ROOT%build_pack" (
    rmdir /s /q "%ROOT%build_pack"
    echo   Removed: build_pack\
)

echo [SolEngine] Wipe complete. Run build.bat to rebuild.
