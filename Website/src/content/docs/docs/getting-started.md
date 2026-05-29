---
title: Getting Started
description: Build SolEngine from source and run the FPS demo on Windows.
---

# Getting Started

SolEngine is currently **Windows-only** (Vulkan / MSVC). Linux and macOS are planned.

## Prerequisites

- Windows 10 or 11 (64-bit)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **C++ workload**
- [CMake 3.25+](https://cmake.org/download/)
- [Vulkan SDK](https://vulkan.lunarg.com/) (1.3+) — add `VULKAN_SDK` to `PATH`
- [Qt 6.7+](https://www.qt.io/download-qt-installer) — add `Qt6_DIR` or install via Qt Installer (MSVC 2022 kit)
- [Git](https://git-scm.com/)

## Clone

```bat
git clone https://github.com/mathiassol/solengine.git
cd solengine
```

## Build

```bat
build.bat
```

This runs `cmake --preset windows-release` and compiles everything:
`sol_engine.dll`, `sol_editor.exe`, and the demo project. First build takes
**10–15 minutes** (CMake FetchContent downloads Jolt, GLM, cgltf, LuaBridge, etc.).

## Launch the Editor

```bat
editor.bat
```

Opens `sol_editor.exe` with the FPS demo project pre-loaded. Press **Play** to run
in the viewport, or open the scene hierarchy and inspector to explore the scene.

## Run the Demo Standalone

```bat
runDemo.bat
```

Starts the FPS demo directly without the editor. WASD + mouse to move.

## Install to AppData (optional)

```bat
install.bat
```

Copies binaries to `%LOCALAPPDATA%\SolEngine\bin\` so you can open any project
from the editor's file picker.

## Project Structure

```
engine/          C++ engine source + headers
editor/          Qt6 + ImGui editor source
demo/            Example FPS project
  scenes/        .solscene scene files
  scripts/       Lua scripts
  assets/        Models, textures, audio
cmake/           CMake helpers
Website/         Astro documentation site
```

## Next Steps

- [Lua API Reference](/docs/reference/lua-api/) — Script nodes with Lua
- [Node Types](/docs/reference/node-types/) — What nodes are available and what they do
- [Material Editor](/docs/reference/material-editor/) — Editing PBR materials in the editor
