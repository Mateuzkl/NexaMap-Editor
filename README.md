<p align="center">
  <a href="https://mateuzkl.github.io/NexaMap-Showcase/">
    <img src="https://i.postimg.cc/1583dKFy/Chat-GPT-Image-17-de-ago-de-2026-10-00-00.png" alt="NexaMap Editor" width="760" />
  </a>
</p>

<h1 align="center">NexaMap Editor</h1>

<p align="center">
  <strong>Create. Convert. Build Worlds.</strong><br />
  A modern native map editor for OpenTibia projects.
</p>

<p align="center">
  <a href="https://github.com/Mateuzkl/NexaMap-Editor"><img src="https://img.shields.io/badge/version-5.0.0-00B8C8?style=flat-square" alt="Version 5.0.0" /></a>
  <img src="https://img.shields.io/badge/C++-20-00599C?style=flat-square&logo=cplusplus&logoColor=white" alt="C++20" />
  <img src="https://img.shields.io/badge/UI-wxWidgets-007ACC?style=flat-square" alt="wxWidgets" />
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-44545F?style=flat-square" alt="Windows and Linux" />
  <a href="https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/build-windows.yml"><img src="https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/build-windows.yml/badge.svg" alt="Windows builds" /></a>
  <a href="https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/clang-format.yml"><img src="https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/clang-format.yml/badge.svg" alt="Code style" /></a>
</p>

<p align="center">
  <a href="https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/build-windows.yml"><strong>Download</strong></a> ·
  <a href="https://mateuzkl.github.io/NexaMap-Showcase/"><strong>Showcase</strong></a> ·
  <a href="https://github.com/Mateuzkl/NexaMap-Editor/issues"><strong>Report a bug</strong></a> ·
  <a href="https://github.com/Mateuzkl/NexaMap-Editor/pulls"><strong>Contribute</strong></a>
</p>

## About

NexaMap Editor is an open-source C++ desktop editor for creating, maintaining, testing, and converting OTBM worlds. It supports classic OpenTibia workflows and modern ClientID projects without tying the editor to one server distribution.

> See the bilingual [NexaMap 5.0 Showcase](https://mateuzkl.github.io/NexaMap-Showcase/) for a visual tour of the latest update.

## Highlights

| Area | What NexaMap provides |
|---|---|
| Workspace | Automatic discovery of maps, items, monsters, NPCs, and client assets |
| Map sessions | Independent resources for each open map tab |
| Cross-client paste | Analyze and remap items before pasting between clients |
| Editing tools | Favorites, Command Palette, minimap, diagnostics, tooltips, and container previews |
| Playtest | Local movement, interaction, HUD, lighting, and weather preview |
| Multiplayer | Host/Join editing with permissions, locks, chat, reconnection, and resync |
| Conversion | ServerID ↔ ClientID maps plus TFS, Canary, and Crystal spawn/NPC formats |
| Performance | Chunk revisions, CPU/GPU caches, atlas tracking, and optimized large-map navigation |

## NexaMap 5.0

- New Client + Server Workspace with persistent project discovery.
- Independent map tabs with isolated resource sessions.
- Cross-client copy and paste with a review and remapping flow.
- Real-time multiplayer editing with authenticated sessions.
- Local Playtest mode using the active map renderer.
- Dockable minimap, Favorites 2.0, Command Palette, and Map Diagnostics.
- Safer ownership, callbacks, threading, socket shutdown, and OpenGL teardown.
- Windows x86/x64 CI builds, clang-format 16 checks, and an expanded native test suite.

## Compatibility

| Project type | Support |
|---|---|
| TFS and classic OTBM | DAT/SPR, OTC resources, items.otb, and ServerID workflows |
| Canary and Crystal | `appearances.dat`, ClientID resources, maps, monsters, and NPCs |
| Dragon Souls TFS 1.4 | Native 16-bit item count/subtype compatibility mode |
| Custom OpenTibia projects | Supported when map, item, client, and server formats are compatible |

> [!IMPORTANT]
> For Dragon Souls maps, enable **Dragon Souls map compatibility (16-bit item count/subtype)** before opening the map. Leave this option disabled for standard TFS, Canary, Crystal, and other regular OTBM maps.

Client assets, item identifiers, and server data must use compatible versions. Mixing unrelated resources can produce missing sprites, incorrect items, or invalid map data.

## Download and first launch

Successful GitHub Actions runs provide ready-to-use **Windows x86** and **Windows x64** Release artifacts:

1. Open [Build Windows](https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/build-windows.yml).
2. Select the latest successful run.
3. Download the artifact for your architecture.
4. Extract the complete ZIP before starting `NexaMap Editor.exe`.

On first launch, select a compatible client folder and server folder in the Workspace. NexaMap will discover available maps and resources. Keep a backup before converting or saving important projects.

## Build from source

Requirements: Git, CMake 3.10+, a C++20 compiler, OpenGL, and the dependencies declared in [`vcpkg.json`](vcpkg.json).

<details>
<summary><strong>Windows — CMake + vcpkg</strong></summary>

```powershell
git clone https://github.com/Mateuzkl/NexaMap-Editor.git
cd NexaMap-Editor

$env:VCPKG_ROOT = "C:\vcpkg"
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release --parallel
```

The Visual Studio solution is also available at `vcproj/Editor.sln`.

</details>

<details>
<summary><strong>Linux — CMake + vcpkg</strong></summary>

```bash
git clone https://github.com/Mateuzkl/NexaMap-Editor.git
cd NexaMap-Editor

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel
```

</details>

## Contributing

Bug reports and focused pull requests are welcome.

- Open an [issue](https://github.com/Mateuzkl/NexaMap-Editor/issues) with clear reproduction steps.
- Include the NexaMap version, operating system, client/server version, logs, and screenshots when relevant.
- Keep code changes focused and preserve existing map compatibility.
- Run clang-format 16, build the project, and test the affected workflow.
- Submit editor fixes and features through a [pull request](https://github.com/Mateuzkl/NexaMap-Editor/pulls).

> [!WARNING]
> Always keep backups before converting maps, IDs, spawns, NPCs, or asset mappings. Reopen converted maps and test them with the intended server and client.

## Project links

| | Link |
|---|---|
| Website | [NexaMap 5.0 Showcase](https://mateuzkl.github.io/NexaMap-Showcase/) |
| Source | [Mateuzkl/NexaMap-Editor](https://github.com/Mateuzkl/NexaMap-Editor) |
| Windows builds | [GitHub Actions artifacts](https://github.com/Mateuzkl/NexaMap-Editor/actions/workflows/build-windows.yml) |
| Feedback | [Issues](https://github.com/Mateuzkl/NexaMap-Editor/issues) |
| Development | [Pull requests](https://github.com/Mateuzkl/NexaMap-Editor/pulls) |

## Credits

Developed by [Mateuzkl](https://github.com/Mateuzkl), [Skyyzyy](https://github.com/Skyyzyy), and [SoyFabi](https://github.com/soyfabi), with help from the OpenTibia mapping and development community.

## License

See [`LICENSE.rtf`](LICENSE.rtf) for the terms that apply to this repository and its distributions.
