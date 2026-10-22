#!/usr/bin/env python3
"""Exercise an installed editor from an unrelated cwd with isolated preferences.

Run under xvfb-run for CI, or the current X11/XWayland display. This proves
startup, resource discovery and a mapped window; it does not certify editing.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    exports = subprocess.check_output(
        ["nm", "--dynamic", "--defined-only", str(executable)], text=True
    )
    if re.search(r"\b(?:png_|jpeg_|TIFF)\w+(?:@@\S+)?$", exports, re.M):
        raise RuntimeError("Bundled image codec symbols leak into the GTK plugin namespace")
    with tempfile.TemporaryDirectory(prefix="nexamap-startup-") as temporary:
        user = Path(temporary)
        env = os.environ.copy()
        env["HOME"] = str(user)
        for kind in ("CONFIG", "DATA", "STATE", "CACHE"):
            env[f"XDG_{kind}_HOME"] = str(user / kind.lower())
        env["NEXAMAP_DIAGNOSTICS"] = "1"
        env["G_DEBUG"] = ",".join(filter(None, (env.get("G_DEBUG"), "fatal-criticals")))
        # The visibility probe and screenshot use X11. Do not let an inherited
        # Wayland socket send the application outside the test's Xvfb display.
        env["GDK_BACKEND"] = "x11"
        log = output / "installed-startup.log"
        with log.open("w") as console:
            process = subprocess.Popen([str(executable)], cwd=user, env=env,
                                       stdout=console, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 40
                while time.monotonic() < deadline:
                    text = log.read_text(errors="replace")
                    if process.poll() is not None:
                        raise RuntimeError(f"Editor exited during startup ({process.returncode}):\n{text}")
                    if "Application event loop started" in text:
                        break
                    time.sleep(0.2)
                else:
                    raise RuntimeError(f"Editor did not reach its event loop:\n{text}")
                time.sleep(1)
                windows = subprocess.check_output(
                    ["xdotool", "search", "--onlyvisible", "--name", "NexaMap"], text=True
                ).strip().splitlines()
                if not windows:
                    raise RuntimeError("No visible NexaMap window")
                from PIL import ImageGrab
                ImageGrab.grab().save(output / "installed-startup.png")
                text = log.read_text(errors="replace")
                if re.search(r"assertion|\[critical\]|Could not find data|Unable to load|Failed to load image", text, re.I):
                    raise RuntimeError(f"Startup diagnostics contain an error:\n{text}")
                diagnostic = user / "state/nexamap/logs/nexamap.log"
                if not diagnostic.is_file():
                    raise RuntimeError("Diagnostic log was not created under XDG_STATE_HOME")
                (output / "xdg-diagnostics.log").write_bytes(diagnostic.read_bytes())
                print(f"PASS installed GUI startup from {user}; {len(windows)} visible window(s)")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
