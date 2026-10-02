#!/usr/bin/env python3
"""Format changed C/C++ files using the repository clang-format rules."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ino"}


def git_lines(*args: str) -> list[str]:
    result = subprocess.run(
        ["git", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def changed_files(base: str | None, head: str) -> list[Path]:
    files: set[str] = set()

    if base:
        files.update(git_lines("diff", "--name-only", "--diff-filter=ACMR", base, head))
    else:
        files.update(git_lines("diff", "--name-only", "--diff-filter=ACMR", "HEAD"))
        files.update(git_lines("diff", "--cached", "--name-only", "--diff-filter=ACMR", "HEAD"))
        files.update(git_lines("ls-files", "--others", "--exclude-standard"))

    return sorted(
        Path(path)
        for path in files
        if Path(path).suffix.lower() in EXTENSIONS and Path(path).is_file()
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Check formatting without modifying files.")
    parser.add_argument("--base", help="Git base ref/SHA to compare against.")
    parser.add_argument("--head", default="HEAD", help="Git head ref/SHA (default: HEAD).")
    parser.add_argument("paths", nargs="*", help="Explicit files to format instead of auto-detecting changed files.")
    args = parser.parse_args()

    clang_format = shutil.which("clang-format")
    if clang_format is None:
        print("error: clang-format is not on PATH (CI uses clang-format 22.1.0).", file=sys.stderr)
        return 2

    if args.paths:
        files = sorted(
            Path(path)
            for path in args.paths
            if Path(path).suffix.lower() in EXTENSIONS and Path(path).is_file()
        )
    else:
        files = changed_files(args.base, args.head)

    if not files:
        print("No changed C/C++ files to format.")
        return 0

    print(("Checking" if args.check else "Formatting") + ":")
    for path in files:
        print(f"  {path}")

    command = [clang_format, "-style=file"]
    if args.check:
        command += ["--dry-run", "--Werror"]
    else:
        command += ["-i"]
    command += [str(path) for path in files]

    return subprocess.run(command).returncode


if __name__ == "__main__":
    raise SystemExit(main())
