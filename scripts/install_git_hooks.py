#!/usr/bin/env python3
"""Install repository-managed Git hooks for Paper Calendar."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    hook = root / ".githooks" / "pre-commit"

    if not (root / ".git").exists():
        print("error: run this from a Git clone of Paper Calendar.", file=sys.stderr)
        return 1

    if not hook.exists():
        print("error: .githooks/pre-commit is missing.", file=sys.stderr)
        return 1

    subprocess.run(
        ["git", "config", "--local", "core.hooksPath", ".githooks"],
        cwd=root,
        check=True,
    )

    try:
        hook.chmod(hook.stat().st_mode | 0o111)
    except OSError:
        # Git for Windows executes hooks through its shell even when chmod is not meaningful.
        pass

    configured = subprocess.run(
        ["git", "config", "--local", "--get", "core.hooksPath"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()

    print(f"Installed Paper Calendar Git hooks: core.hooksPath={configured}")
    print("C/C++ files staged for commit will now be auto-formatted and re-staged.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
