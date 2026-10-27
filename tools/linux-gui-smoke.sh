#!/usr/bin/env bash
set -euo pipefail
[[ $# == 3 ]] || { echo 'Usage: linux-gui-smoke.sh EXECUTABLE OUTPUT CAPTURE_TOOL' >&2; exit 2; }
executable=$(realpath -- "$1")
mkdir -p "$2"
output=$(realpath -- "$2")
capture=$(realpath -- "$3")
if nm --dynamic --defined-only "$executable" | grep -E '[[:space:]](png_|jpeg_|TIFF)[[:alnum:]_]*(@@[^[:space:]]+)?$' >/dev/null; then
  echo 'Bundled image codec symbols leak into the GTK plugin namespace' >&2
  exit 1
fi
user_dir=$(mktemp -d -t nexamap-startup.XXXXXX)
app_pid=''
cleanup() {
  if [[ -n $app_pid ]]; then kill "$app_pid" 2>/dev/null || true; wait "$app_pid" 2>/dev/null || true; fi
  rm -rf -- "$user_dir"
}
trap cleanup EXIT
export XDG_CONFIG_HOME="$user_dir/config" XDG_DATA_HOME="$user_dir/data"
export XDG_STATE_HOME="$user_dir/state" XDG_CACHE_HOME="$user_dir/cache"
export NEXAMAP_DIAGNOSTICS=1 G_DEBUG=fatal-criticals GDK_BACKEND=x11
mkdir -p "$XDG_CONFIG_HOME/nexamap"
printf 'WELCOME_DIALOG=1\n' > "$XDG_CONFIG_HOME/nexamap/editor.cfg"
cd "$user_dir"
"$executable" > "$output/installed-startup.log" 2>&1 &
app_pid=$!
window=''
for attempt in {1..200}; do
  kill -0 "$app_pid" 2>/dev/null || { cat "$output/installed-startup.log"; exit 1; }
  if grep -q 'Application event loop started' "$output/installed-startup.log"; then
    window=$(xdotool search --onlyvisible --pid "$app_pid" --name NexaMap | head -n 1) || true
    [[ -n $window ]] && break
  fi
  sleep 0.2
done
[[ -n $window ]] || { cat "$output/installed-startup.log"; echo 'No mapped editor window' >&2; exit 1; }
sleep 1
"$capture" "$window" "$output/installed-startup.png"
if grep -Eiq 'assertion|\[critical\]|Could not find data|Unable to load|Failed to load image' "$output/installed-startup.log"; then
  cat "$output/installed-startup.log"
  exit 1
fi
cp "$XDG_STATE_HOME/nexamap/logs/nexamap.log" "$output/xdg-diagnostics.log"
echo 'PASS installed GUI startup, resource discovery, GTK codecs and XDG diagnostics'
