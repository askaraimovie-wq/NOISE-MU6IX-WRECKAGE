#!/usr/bin/env python3
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
