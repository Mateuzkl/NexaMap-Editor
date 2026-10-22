#!/usr/bin/env python3
"""Keep the pinned wxWidgets port while using Ubuntu's GTK runtime.

GTK and its pixbuf/GIO modules must share the same GLib runtime. Bundling static
GTK from vcpkg breaks discovery of the system theme's SVG loader. Materialize
the baseline port unchanged apart from these two system-provided dependencies.
"""
import json
from pathlib import Path
import shutil
import sys


def main():
    source, destination = map(Path, sys.argv[1:])
    manifest = json.loads((source / "vcpkg.json").read_text(encoding="utf-8"))
    for name in ("gtk3", "cairo"):
        if not any(isinstance(dep, dict) and dep.get("name") == name
                   for dep in manifest["dependencies"]):
            raise RuntimeError(f"The pinned wxWidgets port no longer declares {name}")
    manifest["dependencies"] = [
        dep for dep in manifest["dependencies"]
        if not (isinstance(dep, dict) and dep.get("name") in ("gtk3", "cairo"))
    ]
    shutil.copytree(source, destination, dirs_exist_ok=True)
    (destination / "vcpkg.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("wxWidgets: pinned port with system GTK3/Cairo")


if __name__ == "__main__":
    main()
