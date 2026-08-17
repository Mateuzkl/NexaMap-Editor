<p align="center">
  <a href="https://postimg.cc/Xp0WGdxP">
    <img src="https://i.postimg.cc/1583dKFy/Chat-GPT-Image-17-de-ago-de-2026-10-00-00.png" alt="NexaMap Editor" width="100%" />
  </a>
</p>

<div align="center">

# NexaMap Editor

**Create. Convert. Build Worlds.**

A modern native map editor for OpenTibia projects, focused on OTBM editing, ClientID workflows, asset compatibility, conversion tools and large-world performance.

[![Version](https://img.shields.io/badge/version-5.0.0-00B8C8?style=flat-square)](https://github.com/Mateuzkl/NexaMap-Editor)
![C++](https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![wxWidgets](https://img.shields.io/badge/UI-wxWidgets-007ACC?style=flat-square)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-44545F?style=flat-square)
[![Formatting](https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/clang-format.yml/badge.svg)](https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/clang-format.yml)
[![Repository size](https://img.shields.io/github/repo-size/Mateuzkl/NexaMap-Editor?style=flat-square)](https://github.com/Mateuzkl/NexaMap-Editor)
[![Issues](https://img.shields.io/github/issues/Mateuzkl/NexaMap-Editor?style=flat-square)](https://github.com/Mateuzkl/NexaMap-Editor/issues)
[![License](https://img.shields.io/badge/license-see%20LICENSE-F6C445?style=flat-square)](LICENSE.rtf)

<br />

![Mapping](https://img.shields.io/badge/MAPPING-OTBM-00B8C8?style=for-the-badge)
![ClientID](https://img.shields.io/badge/WORKFLOW-ClientID-2563EB?style=for-the-badge)
![Canary](https://img.shields.io/badge/SUPPORT-Canary-F59E0B?style=for-the-badge)
![Crystal](https://img.shields.io/badge/SUPPORT-Crystal-8B5CF6?style=for-the-badge)

<br />
<br />

[Repository](https://github.com/Mateuzkl/NexaMap-Editor) ·
[Issues](https://github.com/Mateuzkl/NexaMap-Editor/issues) ·
[Pull Requests](https://github.com/Mateuzkl/NexaMap-Editor/pulls)

</div>

---

## Overview

**NexaMap Editor** is a native C++ desktop application for professional OpenTibia world development.

It combines established OTBM editing workflows with modern conversion tools, ClientID-oriented project support, a responsive wxWidgets interface and optimized handling for large maps.

NexaMap is **not tied to OTG or one specific server distribution**.

It is intended for compatible OpenTibia ecosystems and workflows, including:

- The Forgotten Server-based projects.
- Canary-based projects.
- Crystal-based projects.
- Custom OpenTibia distributions.
- ClientID-oriented servers and datapacks.
- Legacy-to-modern map conversion workflows.

The goal is simple: provide one reliable environment for creating, maintaining and converting OpenTibia worlds without unnecessarily coupling the editor to one server base.

---

## Highlights

| Area | Capabilities |
|---|---|
| Map editing | Create and maintain cities, hunting grounds, dungeons, mountains, castles and complete OTBM worlds |
| ClientID workflow | Work with ClientID-oriented maps, items and modern asset pipelines |
| Map conversion | Convert supported maps and item identifiers between ServerID and ClientID workflows |
| Spawn / NPC conversion | Convert supported spawn and NPC formats for TFS, Canary and Crystal-based projects |
| Asset support | Support for compatible modern client assets, including `appearances.dat` workflows and supported OTC sprite resources |
| Visual tools | Brushes, palettes, workspaces, search tools, zones and procedural map generation |
| Navigation | Fast map navigation with responsive zoom support |
| Interface | DPI-aware native desktop UI with System, Dark and Light editor themes |
| Performance | Cached visual resources and optimized workflows for large maps |
| Project access | Open recent maps directly from the welcome screen with keyboard and mouse support |

---

## Universal OpenTibia Workflow

NexaMap is designed as a general-purpose OpenTibia map editor.

```text
OpenTibia Project
      |
      +-- TFS
      |
      +-- Canary
      |
      +-- Crystal
      |
      +-- Custom Server
              |
              +-- OTBM Maps
              +-- ServerID / ClientID
              +-- Spawn / NPC Data
              +-- Client Assets
              +-- appearances.dat
              +-- OTC Sprite Resources
```

Server-specific data still needs to match the format expected by that server, but the editor itself is not restricted to a single project family.

---

## ClientID and Asset Support

Modern OpenTibia projects increasingly use ClientID-oriented asset workflows.

NexaMap provides tooling intended to work with these environments while preserving support for established OTBM editing.

Supported project workflows include:

- ClientID-based item workflows.
- ServerID-to-ClientID conversion.
- ClientID-to-supported target conversions.
- `appearances.dat`-based asset workflows.
- Supported OTC sprite resources.
- Compatible client data selected through editor preferences.
- Canary and Crystal-oriented datapack conversion workflows.

> [!IMPORTANT]
> Client assets, item identifiers and server data must belong to compatible versions.
>
> Mixing unrelated assets or ID mappings can produce incorrect items, missing sprites or invalid map data.

---

## Performance and Navigation

NexaMap is built for practical work on large OpenTibia maps.

Performance-focused behavior includes:

- Cached visual resources.
- Optimized editing workflows.
- Responsive map rendering.
- Fast navigation across large areas.
- Zoom support for detailed editing and broader world inspection.
- Native desktop rendering through wxWidgets and OpenGL.
- DPI-aware interface behavior.

The editor is intended to remain usable during large-world maintenance, conversion and normal mapping work.

---

## Welcome Screen

The NexaMap welcome screen provides direct access to:

- **New Map** — create a new OTBM project.
- **Open Project** — open an existing map from disk.
- **Map Converter** — access map and item-ID conversion tools.
- **Spawn / NPC Converter** — convert supported spawn and NPC formats.
- **Preferences** — configure editor settings, client assets and appearance.
- **Recent Projects** — reopen recently used maps from a scrollable project list.

The welcome screen keeps the NexaMap dark visual identity.

The **System**, **Dark** and **Light** options control the main editor appearance after the application is restarted.

---

## Conversion Tools

### Map / Item Conversion

NexaMap provides workflows for converting supported maps and item identifiers between server-oriented and client-oriented formats.

Typical use cases:

```text
Legacy map
    |
    +-- ServerID data
    |
    +-- NexaMap conversion
    |
    +-- ClientID-oriented map
```

Always validate the converted map with the intended client assets and server datapack.

### Spawn / NPC Conversion

The editor also provides conversion tools for supported spawn and NPC data.

Target workflows can include:

- TFS
- Canary
- Crystal
- Compatible custom server structures

---

## Requirements

- C++20-compatible compiler.
- CMake 3.10 or newer for CMake-based builds.
- wxWidgets with:
  - `html`
  - `aui`
  - `gl`
  - `adv`
  - `core`
  - `net`
  - `base`
- OpenGL.
- Zlib.
- Dependencies declared in [`vcpkg.json`](vcpkg.json).
- Compatible client assets for the protocol/client version you intend to edit.

---

# Compilation

## Windows — Visual Studio + vcpkg

Install [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg.exe integrate install
```

Clone the **official NexaMap Editor repository**:

```powershell
git clone https://github.com/Mateuzkl/NexaMap-Editor.git
cd NexaMap-Editor
```

Open:

```text
vcproj/Editor.sln
```

Recommended configuration:

```text
Platform: x64
Configuration: Release
```

`Debug` can be used for development/debugging.

The Visual Studio project uses manifest mode through [`vcpkg.json`](vcpkg.json), allowing required dependencies to be restored through vcpkg.

Install the MSVC toolset declared by the project, or deliberately retarget the solution to a compatible installed toolset.

---

## Windows — CMake

Set `VCPKG_ROOT`:

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
```

Clone and enter the repository:

```powershell
git clone https://github.com/Mateuzkl/NexaMap-Editor.git
cd NexaMap-Editor
```

Configure:

```powershell
cmake -S . -B out/build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

Build and install:

```powershell
cmake --build out/build/release
cmake --install out/build/release --prefix out/install/release
```

---

## Linux — CMake + vcpkg

Install Git, CMake, Ninja, a C++20 compiler and development packages required by OpenGL and your desktop environment.

Clone and bootstrap vcpkg:

```bash
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh
```

Clone NexaMap Editor:

```bash
git clone https://github.com/Mateuzkl/NexaMap-Editor.git
cd NexaMap-Editor
```

Configure:

```bash
cmake -S . -B out/build/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake"
```

Build and install:

```bash
cmake --build out/build/release --parallel
cmake --install out/build/release --prefix out/install/release
```

---

## Resource Layout

Official NexaMap visual resources are stored under:

```text
data/images/
data/images/welcome/
```

During development, the editor resolves welcome-screen resources from the repository.

Do not manually duplicate generated resources inside Visual Studio `Debug` or `Release` output directories unless a specific development workflow requires it.

CMake install rules package required welcome assets for installed builds.

---

## First Launch

1. Start **NexaMap Editor**.
2. Open **Preferences**.
3. Select/configure the client version used by your project.
4. Configure compatible client assets.
5. Create a new map or open an existing `.otbm` file.
6. Verify sprites/items before editing important areas.
7. Keep a backup before any conversion.

---

## Recommended Project Workflow

```text
Configure client assets
        |
        v
Open / create OTBM
        |
        v
Edit map
        |
        +--> Terrain / brushes
        +--> Items
        +--> Spawns / NPCs
        +--> Zones
        |
        v
Optional conversion
        |
        +--> ServerID <-> ClientID workflow
        +--> Spawn / NPC conversion
        |
        v
Save
        |
        v
Test with target server + client
```

---

## Conversion Safety

> [!WARNING]
> Keep backups before converting maps, IDs, spawns, NPCs or client asset mappings.

Recommended:

```text
world.otbm
world.backup.otbm
```

After conversion:

1. Reopen the map.
2. Inspect representative areas.
3. Verify item sprites.
4. Verify borders and ground tiles.
5. Verify containers and stackable items.
6. Verify houses.
7. Verify spawns and NPCs.
8. Load the map on the target server.
9. Test it with the intended client.

---

## Repository Structure

Important project areas include:

```text
.github/
brushes/
data/
extensions/
icons/
source/
tests/
tools/
vcproj/
CMakeLists.txt
vcpkg.json
```

Visual resources:

```text
data/images/
data/images/welcome/
```

---

## Contributing

Bug reports and focused pull requests are welcome.

When contributing:

- Keep changes limited to a clear purpose.
- Preserve OTBM compatibility and existing map data unless a migration is explicitly documented.
- Keep specialized/experimental workflows optional when they are not appropriate for every user.
- Follow existing C++20 and wxWidgets conventions.
- Preserve normal parent-owned control lifetime.
- Preserve keyboard navigation and accessibility.
- Preserve DPI behavior when changing interface code.
- Do not commit executables, compiler output, logs or temporary files.
- Avoid duplicated build resources.
- Describe the affected workflow.
- Document manual validation performed for the pull request.
- Test map-format/conversion changes carefully.

Before submitting:

```text
Review diff
    |
    +-- Run formatting
    +-- Build project
    +-- Test affected workflow
    +-- Verify map compatibility
    +-- Open focused PR
```

---

## Bug Reports

Use:

**[GitHub Issues](https://github.com/Mateuzkl/NexaMap-Editor/issues)**

Include:

- NexaMap version.
- Operating system.
- Client/protocol version.
- Server family when relevant: TFS, Canary, Crystal or custom.
- Asset format used.
- Clear reproduction steps.
- Expected behavior.
- Actual behavior.
- Logs/crash output when available.
- Screenshot/video when useful.
- Small test map when the bug depends on map data.

---

## Credits

NexaMap Editor is developed by:

- [Mateuzkl](https://github.com/Mateuzkl)
- [Skyyzyy](https://github.com/Skyyzyy)

Thanks to the OpenTibia mapping and development community and the upstream projects that made modern map-editor development possible.

---

## License

See [`LICENSE.rtf`](LICENSE.rtf) for the terms that apply to this repository and its distributions.

---

<div align="center">

### NexaMap Editor 5.0.0

**Universal ClientID Map Editor for OpenTibia**

Create. Convert. Build Worlds.

[Mateuzkl/NexaMap-Editor](https://github.com/Mateuzkl/NexaMap-Editor)

</div>
