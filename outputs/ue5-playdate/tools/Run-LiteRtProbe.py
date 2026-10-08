"""Run one explicit backend on one explicitly selected device; never alter Playdate data."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial", required=True)
    parser.add_argument("--cache", default="D:/PlaydateAI/litert-experiment")
    parser.add_argument("--adb", default="C:/Users/dodol/AppData/Local/Android/Sdk/platform-tools/adb.exe")
    parser.add_argument("--model", choices=["dynamic-int4", "mixed-int4"], default="dynamic-int4")
    parser.add_argument("--backend", choices=["CPU", "GPU"], required=True)
    parser.add_argument("--case", choices=["short", "controls", "long"], default="short")
    parser.add_argument("--prompt", choices=["baseline", "strict", "strict-cn"], default="baseline")
    parser.add_argument("--placement", choices=["system", "user"], default="user")
    parser.add_argument("--repeats", type=int, choices=[1, 2, 3], default=2)
    parser.add_argument("--report", required=True)
    args = parser.parse_args()
    if not args.report or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for c in args.report):
        parser.error("report must contain only letters, numbers, underscore or hyphen")
    root = Path(args.cache).resolve()
    if root.drive.lower() == "c:":
        parser.error("Use D drive for experimental downloads and build caches")
    source = Path(__file__).resolve().parent / "android-litert-probe"
    lock = json.loads((source / ("model-mixed-int4.json" if args.model == "mixed-int4" else "model.json")).read_text(encoding="utf-8"))
    model = root / lock["filename"]
    if model.stat().st_size != lock["bytes"] or hashlib.file_digest(model.open("rb"),"sha256").hexdigest() != lock["sha256"]:
        raise ValueError("model checksum mismatch")
    adb = [args.adb, "-s", args.serial]

    def command(*words, timeout=120):
        return subprocess.run(adb + list(words), check=True, capture_output=True, timeout=timeout).stdout

    package = "com.cranklab.litertprobe"
    storage = f"/sdcard/Android/data/{package}/files"
    command("install", "-r", str(root / "build/playdate-litert-probe.apk"))
    command("shell", "am", "start", "-n", package + "/" + package + ".ProbeActivity")
    command("shell", "mkdir", "-p", storage + "/models")
    remote = storage + "/models/" + model.name
    try:
        already_copied = command("shell", "sha256sum", remote).decode().split()[0] == lock["sha256"]
    except subprocess.CalledProcessError:
        already_copied = False
    if not already_copied:
        command("push", str(model), remote)
    if command("shell", "sha256sum", remote).decode().split()[0] != lock["sha256"]:
        raise ValueError("device model checksum mismatch")
    if args.case == "long":
        description = json.loads(command("shell", "run-as", "com.cranklab.emulator", "cat", "files/CrankLab/Translation/Descriptions/2524.json"))
        fixture = root / "fixtures.json"
        fixture.write_text(json.dumps({"long": description["description"]},ensure_ascii=False),encoding="utf-8")
        command("push", str(fixture), storage + "/fixtures.json")
    # Remove only this probe-owned report, so a native crash cannot reuse old success.
    command("shell", "rm", "-f", storage + "/" + args.report + ".json")
    print(f"Running {args.backend} {args.case} {args.model}", flush=True)
    (root / (args.report + "-thermal-before.txt")).write_bytes(command("shell","dumpsys","thermalservice"))
    try:
        output = command("shell", "am", "instrument", "-w", "-r", "-e", "backend", args.backend,
                         "-e", "case", args.case, "-e", "prompt", args.prompt, "-e", "placement", args.placement,
                         "-e", "template", "qwen3", "-e", "model", model.name, "-e", "repeats", str(args.repeats),
                         "-e", "report", args.report, package + "/" + package + ".LiteRtProbe", timeout=600)
        (root / (args.report + ".log")).write_bytes(output)
    except subprocess.TimeoutExpired:
        command("shell", "am", "force-stop", package)
        raise
    finally:
        (root / (args.report + "-thermal-after.txt")).write_bytes(command("shell","dumpsys","thermalservice"))
        try:
            command("pull", storage + "/" + args.report + ".json", str(root / (args.report + ".json")))
        except subprocess.CalledProcessError:
            print("No report returned: inspect logcat for a native crash", flush=True)
    report = json.loads((root / (args.report + ".json")).read_text(encoding="utf-8"))
    if b"INSTRUMENTATION_CODE: -1" not in output:
        report["success"] = False
    if "pid" in report:
        (root / (args.report + "-native.log")).write_bytes(command("logcat", "-d", "--pid=" + str(report["pid"])))
    print(json.dumps({"runtimeSuccess": report.get("success",False), "initializeSeconds":report.get("initializeSeconds"),
                      "runs":[{"seconds":r.get("seconds"),"firstVisibleSeconds":r.get("firstVisibleSeconds")} for r in report.get("runs",[])],
                      "error":report.get("error"), "report": str(root / (args.report + ".json"))},ensure_ascii=True),flush=True)
    if not report.get("success", False):
        raise SystemExit(1)
