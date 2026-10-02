"""Shared PlatformIO discovery and command-line entry point."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys


def platformio_command(core_dir=None):
    """Return an argv prefix; paths with spaces remain single arguments."""
    for name in ("pio", "platformio"):
        executable = shutil.which(name)
        if executable:
            return [executable]

    # Prefer the build's configured core and the active Python/virtualenv.
    directories = [Path(sys.executable).parent]
    for value in (os.environ.get("PLATFORMIO_CORE_DIR"),
                  os.environ.get("VIRTUAL_ENV"),
                  Path(__file__).resolve().parents[1] / ".venv",
                  Path(__file__).resolve().parents[1] / "venv",
                  Path.home() / ".platformio" / "penv"):
        if value:
            base = Path(value).expanduser()
            directories.extend([base / "penv" / "Scripts", base / "penv" / "bin",
                                base / "Scripts", base / "bin"])
    if core_dir:
        base = Path(core_dir).expanduser()
        directories = [base / "penv" / "Scripts", base / "penv" / "bin",
                       base / "Scripts", base / "bin"] + directories
    for directory in directories:
        for name in ("pio", "platformio"):
            executable = directory / (name + (".exe" if os.name == "nt" else ""))
            if executable.is_file() and os.access(executable, os.X_OK):
                return [str(executable)]

    # Also supports pip/virtualenv installations without console entry points.
    if importlib.util.find_spec("platformio") is not None:
        return [sys.executable, "-m", "platformio"]
    raise RuntimeError(
        "PlatformIO was not found. Install PlatformIO Core or the VS Code "
        "PlatformIO extension, or set PLATFORMIO_CORE_DIR to your existing "
        "PlatformIO core directory (containing penv). No global PATH edit is required."
    )


def main():
    try:
        return subprocess.call(platformio_command() + sys.argv[1:])
    except (RuntimeError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
