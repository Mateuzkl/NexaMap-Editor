#!/usr/bin/env bash
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/linux-release"
prefix=''
jobs=2
tests=OFF
package=0
vcpkg_root=${VCPKG_ROOT:-}
dependencies_dir=${NEXAMAP_DEPENDENCIES_DIR:-}
triplet=${NEXAMAP_VCPKG_TRIPLET:-x64-linux-release}
usage() {
  cat <<'EOF'
Usage: bash tools/build-linux.sh [options]
  --build-dir PATH    Build directory (default: build/linux-release)
  --prefix PATH       Install prefix (default: BUILD_DIR/install)
  --vcpkg-root PATH   Use an existing checkout at the manifest baseline
  --dependencies-dir PATH  vcpkg install directory (default: cache inside vcpkg)
  --jobs NUMBER       Parallel build jobs (default: 2; full editor tests use RAM)
  --tests             Build and run all tests, including wxGTK/OpenGL tests
  --package           Produce a .deb with automatically detected dependencies
  --print-deps        Print the Ubuntu system dependency installation command
  --help              Show this help

The build runs as your normal user. It never runs sudo or deletes directories.
Without --vcpkg-root, downloads Microsoft's vcpkg at the pinned manifest baseline.
EOF
}
dependencies() {
  echo 'sudo apt-get update'
  local packages=(
    build-essential cmake ninja-build git curl zip unzip tar pkg-config
    autoconf autoconf-archive automake libtool bison flex linux-libc-dev
    libgtk-3-dev libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxinerama-dev
    libxcursor-dev libxxf86vm-dev libxfixes-dev libxrender-dev libxtst-dev
    libxcomposite-dev libxdamage-dev libwayland-dev libxkbcommon-dev
    xvfb xauth xdotool mesa-utils desktop-file-utils python3 python3-venv python3-pil
  )
  printf 'sudo apt-get install -y'
  printf ' %s' "${packages[@]}"
  printf '\n'
}
while (($#)); do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --print-deps) dependencies; exit 0 ;;
    --tests) tests=ON; shift ;;
    --package) package=1; shift ;;
    --build-dir|--prefix|--vcpkg-root|--dependencies-dir|--jobs)
      (($# >= 2)) || { echo "Missing value for $1" >&2; exit 2; }
      case "$1" in
        --build-dir) build=$2 ;;
        --prefix) prefix=$2 ;;
        --vcpkg-root) vcpkg_root=$2 ;;
        --dependencies-dir) dependencies_dir=$2 ;;
        --jobs) jobs=$2 ;;
      esac
      shift 2 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo '--jobs must be a positive integer' >&2; exit 2; }
export VCPKG_MAX_CONCURRENCY=${VCPKG_MAX_CONCURRENCY:-$jobs}
[[ $(uname -s) == Linux ]] || { echo 'Run this script on Linux.' >&2; exit 1; }
if [[ -r /etc/os-release ]]; then
  . /etc/os-release
  echo "Distribution: ${PRETTY_NAME:-unknown}"
  if [[ ${ID:-} != ubuntu || ! ${VERSION_ID:-} =~ ^(22\.04|24\.04)$ ]]; then
    echo 'This distribution is outside the Ubuntu 22.04/24.04 verification matrix.' >&2
  fi
fi
for command in git cmake ninja g++ pkg-config curl tar zip unzip; do
  command -v "$command" >/dev/null || { echo "Missing $command. Use --print-deps." >&2; exit 1; }
done
pkg-config --exists gtk+-3.0 || { echo 'Missing GTK3 development files. Use --print-deps.' >&2; exit 1; }
baseline=$(sed -n 's/.*"builtin-baseline": *"\([a-f0-9]*\)".*/\1/p' "$root/vcpkg.json")
[[ $baseline =~ ^[a-f0-9]{40}$ ]] || { echo 'Invalid vcpkg baseline' >&2; exit 1; }
if [[ -z $vcpkg_root ]]; then
  vcpkg_root="${XDG_CACHE_HOME:-$HOME/.cache}/nexamap/vcpkg-$baseline"
  if [[ ! -d $vcpkg_root/.git ]]; then
    mkdir -p "$vcpkg_root"
    git -C "$vcpkg_root" init
    git -C "$vcpkg_root" remote add origin https://github.com/microsoft/vcpkg.git
    git -C "$vcpkg_root" fetch --depth 1 origin "$baseline"
    git -C "$vcpkg_root" checkout --detach FETCH_HEAD
  fi
fi
vcpkg_root=$(CDPATH= cd -- "$vcpkg_root" && pwd)
dependencies_dir=${dependencies_dir:-$vcpkg_root/nexamap-installed/$triplet}
mkdir -p "$dependencies_dir"
dependencies_dir=$(CDPATH= cd -- "$dependencies_dir" && pwd)
# OpenSSL's configure wrapper splits its install prefix at whitespace, even
# when CMake quotes it. Keep dependencies outside a source/build path with spaces.
if [[ $vcpkg_root =~ [[:space:]] || $dependencies_dir =~ [[:space:]] ]]; then
  echo 'vcpkg and its dependencies directory must have paths without whitespace.' >&2
  echo 'Use --vcpkg-root and --dependencies-dir to select paths without whitespace.' >&2
  exit 1
fi
[[ $(git -C "$vcpkg_root" rev-parse HEAD) == "$baseline" ]] || { echo "vcpkg must be at $baseline" >&2; exit 1; }
if [[ ! -x $vcpkg_root/vcpkg ]]; then
  bash "$vcpkg_root/bootstrap-vcpkg.sh" -disableMetrics
fi
mkdir -p "$build"
build=$(CDPATH= cd -- "$build" && pwd)
prefix=${prefix:-$build/install}
compiler_cache_args=()
compiler_cache=''
for candidate in sccache ccache; do
  if command -v "$candidate" >/dev/null; then
    compiler_cache=$candidate
    compiler_cache_args=("-DCMAKE_C_COMPILER_LAUNCHER=$candidate" "-DCMAKE_CXX_COMPILER_LAUNCHER=$candidate")
    echo "Compiler cache: $candidate"
    break
  fi
done
echo "Source SHA: $(git -C "$root" rev-parse HEAD)"
g++ --version | head -1
cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_TOOLCHAIN_FILE="$vcpkg_root/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_INSTALLED_DIR="$dependencies_dir" -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
  -DVCPKG_OVERLAY_TRIPLETS="$root/cmake/triplets" -DVCPKG_TARGET_TRIPLET="$triplet" \
  -DENABLE_UNITY_BUILD=ON -DBUILD_TESTING="$tests" \
  -DENABLE_MULTIPLAYER_SESSION_TESTS="$tests" -DENABLE_GL_CACHE_TESTS="$tests" \
  "${compiler_cache_args[@]}"
build_start=$SECONDS
cmake --build "$build" --parallel "$jobs"
echo "Unity build completed in $((SECONDS - build_start)) seconds ($jobs parallel jobs)."
if [[ -n $compiler_cache ]]; then
  "$compiler_cache" --show-stats || true
fi
if [[ $tests == ON ]]; then
  command -v xvfb-run >/dev/null || { echo 'Install xvfb and xauth to run GTK tests.' >&2; exit 1; }
  mkdir -p "$build/test-user"/{config,data,state,cache}
  XDG_CONFIG_HOME="$build/test-user/config" XDG_DATA_HOME="$build/test-user/data" \
  XDG_STATE_HOME="$build/test-user/state" XDG_CACHE_HOME="$build/test-user/cache" \
  LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1280x960x24' \
    ctest --test-dir "$build" --output-on-failure --no-tests=error --timeout 180
fi
cmake --install "$build"
desktop-file-validate "$prefix/share/applications/nexamap-editor.desktop"
if ((package)); then
  cpack --config "$build/CPackConfig.cmake" -G DEB -B "$build/packages"
fi
echo "Installed launcher: $prefix/bin/nexamap-editor"
