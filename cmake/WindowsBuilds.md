# Windows CI builds

The workflow builds and packages Release for x86 and x64 using the pinned
vcpkg baseline, Ninja and unity compilation. Files with conflicting private
helpers are compiled separately without changing editor behavior.

The supported Windows configuration is the one CI builds: CMake with the
`x86-windows-static` or `x64-windows-static` triplet, the static CRT (`/MT`) and
no link-time optimization. `CMakeSettings.json` and `vcproj/Editor.sln` use the
same static triplets and CRT, so a local build matches the tested binary. x86
links with `/LARGEADDRESSAWARE`. All Release binaries use `/GS`, `/sdl`,
`/guard:cf` and `/CETCOMPAT`; `ENABLE_HARDENING=OFF` removes them.

Dependency downloads and vcpkg binary packages are cached separately for each
triplet. Keys include the MSVC toolset and manifest hash. Prefix restoration
can seed a new manifest from older packages; vcpkg checks package ABIs before
reuse. Only successful configuration saves a new cache, preventing an incomplete
dependency install from occupying an immutable exact key. Build directories,
executables and test results are never restored from this dependency cache.

Tagged releases run from `release.yml`, which calls this workflow with
`cold_cache`. That run restores no cache and uses no compiler cache, so every
dependency and object in a published binary is built from source in that run.
The release job refuses to touch an existing release: published assets are never
replaced. It publishes `SHA256SUMS.txt`, a build provenance attestation and
vcpkg's SPDX documents next to the packages. Binaries are not code signed.

C/C++ object compilation uses the pinned Mozilla sccache release with GitHub
Actions storage. Cache keys account for compiler inputs; compiler options and
source semantics are preserved. Server communication failures fall back to the
compiler. The diagnostics artifact includes hit/miss statistics. Linking, tests
and package validation still execute on every run.

Full-editor test harnesses share one object library, compiling identical editor
sources once while retaining separate executables. Tests run in parallel, with their existing individual timeouts and a 120-second
default. Test targets compile with `__DEBUG__` and without `NDEBUG` even in
Release, so `assert()` and the editor's `ASSERT()` are active. The editor itself
retains normal Release compilation options.

Every run uploads a `Windows-<arch>-diagnostics` artifact, including stage logs,
CTest JUnit results when tests run, CMake configuration records and vcpkg build
logs. The job summary reports stage exit codes and elapsed seconds. Environment
metadata records the commit, runner image, dependency baseline and cache hit.
Diagnostics expire after seven days; validated runtime packages after fourteen.

Compare successful runs on the same runner/toolchain with cold and warm caches
before attributing a speedup to a change. Cache restoration and upload also cost
time; the workflow deliberately retains compiler optimizations, packaging checks
and both architectures. A green workflow validates builds, automated tests and
package structure, but does not replace interactive editor testing.
