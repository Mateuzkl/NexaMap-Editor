# PR25 audit baseline — 2026-09-05

Recorded before production fixes. A detached worktree preserves pre-existing edits in the user's checkout.

- PR: https://github.com/Mateuzkl/NexaMap-Editor/pull/25 (OPEN, verified with `gh pr view`).
- HEAD: `27a1ec533dc10a920e2c2afb41358490722fb235`; origin branch matches after `git fetch --all --prune`.
- Base: `e5d75c1c7c8a5d7ecceaffc796d0696d1fc77458` (`origin/main`).
- 38 commits, 519 changed files; 21,522 additions and 1,813,897 deletions.
- Existing user changes: `data/editor/borders.xml`, `source/server_workspace.cpp`; untracked `build.loading-tooltip.log`, `build.playtest-validation-output.log`. None included in baseline.
- Compiler: MSVC 14.51.36231, Visual Studio 18 2026 / v145, x64, Release, C++20; wxWidgets 3.3.1.
- Configure: `cmake -S . -B build-audit -G "Visual Studio 18 2026" -A x64 -T v145 -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR="C:/Users/Mateus/Desktop/RME MELHORAR O NEXA/NexaMap-Editor/vcpkg_installed/x64-windows" -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_TARGET_TRIPLET=x64-windows -DBUILD_TESTING=ON -DENABLE_MULTIPLAYER_SESSION_TESTS=ON -DENABLE_GL_CACHE_TESTS=ON`.
- Full `cmake --build build-audit --config Release --parallel 8`: **FAIL**. Unity `rme` unit 15 combines `server_workspace.cpp` and `spawn_xml_converter.cpp`, colliding on anonymous-namespace `IsRegularFile`. The integration executable still builds because its unity grouping differs.
- 30 unit/GL tests PASS (4.12 s). Four full editor tests PASS (14.99 s): multiplayer session, startup, playtest, collections.
- OpenGL 4.6 / Intel UHD Graphics, driver 27.20.100.9664. Existing synthetic 16-ground test: exact pixel equivalence; cache OFF/ON stream uploads 1760/480 bytes; 13 draws each. Median submit times: OFF 0.0592/0.0632 ms, ON 0.0678/0.0499 ms. These are baseline cache-mode comparisons, not audit improvements.
- Representative map RAM, repeated-tab RAM, VRAM, and sanitizer leak counts: **not measured at this baseline**. No memory/performance improvement may be inferred from compilation or these synthetic cache results.

Logs and commit/file inventories are retained in the isolated worktree's `build-audit` directory. The final report must distinguish inventory coverage, manually inspected code, verified fixes, and unexecuted stress scenarios.
