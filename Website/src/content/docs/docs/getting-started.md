---
title: Getting Started
description: Install SolEngine and create your first project.
---

# Getting Started

This guide walks you through installing SolEngine and setting up your first project.

## Prerequisites

Before you begin, make sure you have:

- A supported operating system (Windows 10+, macOS 12+, or a modern Linux distro)
- Any prerequisites specific to your platform

## Installation

Download the latest release from the [Download](/download) page and follow the
platform-specific instructions below.

### Windows

1. Download the `.exe` installer
2. Run it and follow the setup wizard
3. Verify the install: `solengine --version`

### macOS

1. Download the `.dmg` file
2. Open it and drag **SolEngine** to your Applications folder
3. Verify: `solengine --version`

### Linux

```bash
# Debian / Ubuntu
sudo dpkg -i solengine_*.deb

# Arch / Manjaro
# package coming soon — build from source for now
```

## Your First Project

```bash
solengine new my-project
cd my-project
solengine run
```

## Next Steps

- Explore the [Reference](/reference/) for a full API overview
- Check [GitHub](https://github.com/mathiassol/solengine) for examples and issues
