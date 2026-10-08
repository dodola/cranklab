"""Fetch the pinned experiment only; all binaries and weights stay outside Git on D."""
import argparse
import hashlib
import json
import time
import urllib.request
from pathlib import Path


def fetch(root, item):
    destination = (root / item["filename"]).resolve()
    if not destination.is_relative_to(root.resolve()):
        raise ValueError("download path escapes experiment directory")
    destination.parent.mkdir(parents=True, exist_ok=True)

    def valid(file):
        return file.is_file() and file.stat().st_size == item["bytes"] and hashlib.file_digest(file.open("rb"), "sha256").hexdigest() == item["sha256"]

    if destination.exists():
        if not valid(destination):
            raise ValueError("existing experiment file has incorrect hash: " + str(destination))
        print("Verified cached " + destination.name, flush=True)
        return
    partial = destination.with_suffix(destination.suffix + ".part")
    last = time.monotonic()
    total = 0
    with urllib.request.urlopen(item["url"], timeout=120) as response, partial.open("wb") as output:
        while block := response.read(1024 * 1024):
            output.write(block)
            total += len(block)
            if time.monotonic() - last >= 10:
                print(f"{destination.name}: {total}/{item['bytes']} bytes", flush=True)
                last = time.monotonic()
    if not valid(partial):
        raise ValueError("download checksum mismatch: " + destination.name)
    partial.replace(destination)
    print("Verified downloaded " + destination.name, flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", default="D:/PlaydateAI/litert-experiment")
    parser.add_argument("--model", choices=["dynamic-int4", "mixed-int4"], default="dynamic-int4")
    args = parser.parse_args()
    root = Path(args.cache).resolve()
    if root.drive.lower() == "c:":
        parser.error("Use D drive for experimental downloads and build caches")
    source = Path(__file__).resolve().parent / "android-litert-probe"
    for dependency in json.loads((source / "dependencies.json").read_text(encoding="utf-8")):
        fetch(root / "deps", dependency)
    model_file = "model-mixed-int4.json" if args.model == "mixed-int4" else "model.json"
    fetch(root, json.loads((source / model_file).read_text(encoding="utf-8")))
