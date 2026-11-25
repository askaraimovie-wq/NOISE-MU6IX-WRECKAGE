#!/usr/bin/env python3
"""Strict Base-10 Rollover Version Bumper for NOISE MU6IX — WRECKAGE.
Rule: Each minor/patch digit stays in 0..9 (never 10).
Example: 1.0.8 -> 1.0.9 -> 1.1.0 ... 1.9.9 -> 2.0.0
"""
import pathlib, sys

VERSION_FILE = pathlib.Path(__file__).resolve().parent.parent / "VERSION"

def bump(version_str: str) -> str:
    parts = [int(x) for x in version_str.strip().lstrip("v").split(".")]
    if len(parts) != 3:
        raise ValueError(f"Expected MAJOR.MINOR.PATCH, got: {version_str}")
    major, minor, patch = parts
    patch += 1
    if patch >= 10:
        patch = 0
        minor += 1
    if minor >= 10:
        minor = 0
        major += 1
    return f"{major}.{minor}.{patch}"

if __name__ == "__main__":
    current = VERSION_FILE.read_text(encoding="utf-8").strip()
    nxt = bump(current)
    if "--dry-run" in sys.argv:
        print(f"Current: v{current} -> Next: v{nxt}")
    else:
        VERSION_FILE.write_text(nxt + "\n", encoding="utf-8")
        print(f"Bumped version: v{current} -> v{nxt}")
