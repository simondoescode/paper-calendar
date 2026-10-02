#!/usr/bin/env bash
set -e
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for candidate in "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/penv/bin/python" "${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/penv/Scripts/python.exe" "${VIRTUAL_ENV:-$SCRIPT_DIR/../.venv}/bin/python" "${VIRTUAL_ENV:-$SCRIPT_DIR/../.venv}/Scripts/python.exe" python3 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
        exec "$candidate" "$SCRIPT_DIR/platformio_cli.py" "$@"
    fi
done
echo "Python was not found. Install PlatformIO Core or the VS Code PlatformIO extension, or set PLATFORMIO_CORE_DIR to its core directory." >&2
exit 1
