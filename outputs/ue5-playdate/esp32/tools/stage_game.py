"""Copy an ordinary Lua .pdx directory into the MCU's LittleFS game library."""
import argparse
import re
import shutil
from pathlib import Path

def stage_game(source: Path, destination: Path):
    if not (source / "pdxinfo").is_file():
        raise ValueError("A PDX directory with pdxinfo is required")
    if not any((source / name).is_file() for name in ("main.pdz", "main.luac", "main.lua")):
        raise ValueError("No Lua entry point found")
    native = source / "pdex.bin"
    if native.exists() and native.stat().st_size:
        raise ValueError("Native ARM/C games cannot run on this ESP32 port")
    if destination.exists():
        raise ValueError(f"Destination already exists: {destination}; choose a new game id")
    destination.mkdir(parents=True)
    for file in source.rglob("*"):
        if file.is_symlink():
            raise ValueError(f"Symbolic links are not accepted: {file}")
        if not file.is_file() or file.suffix.lower() in (".dll", ".dylib", ".so", ".exe"):
            continue
        target = destination / file.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
    return sum(f.stat().st_size for f in destination.rglob("*") if f.is_file())

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("pdx", type=Path)
    p.add_argument("--id", required=True)
    p.add_argument("--data", type=Path, default=Path(__file__).resolve().parents[1] / "data")
    args = p.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,50}", args.id):
        p.error("Use a short ASCII game id with letters, digits, - or _")
    size = stage_game(args.pdx.resolve(), args.data.resolve() / "games" / args.id)
    print(f"Staged {args.id}: {size} bytes. Build and upload the LittleFS image next.")
