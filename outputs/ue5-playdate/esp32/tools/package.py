"""Package M5Dial firmware and a small, project-owned example filesystem.

Only creates local artifacts. Does not flash a board or include SDK/game caches.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
from stage_game import stage_game

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def package(args):
    project = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    archive = output.with_suffix(".zip")
    if output.exists():
        raise ValueError(f"Choose a new output directory: {output}")
    if archive.exists():
        raise ValueError(f"Archive already exists: {archive}")
    needed = {name: args.build / name for name in ("bootloader.bin", "partitions.bin", "firmware.bin")}
    needed["boot_app0.bin"] = args.pio_core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    for path in needed.values():
        if not path.is_file():
            raise FileNotFoundError(path)
    mklittlefs = args.pio_core / "packages/tool-mklittlefs" / ("mklittlefs.exe" if sys.platform == "win32" else "mklittlefs")
    if not mklittlefs.is_file():
        raise FileNotFoundError(mklittlefs)
    report = None
    if args.verification:
        report = json.loads((args.verification / "report.json").read_text(encoding="utf-8"))
        if report.get("result") != "passed":
            raise ValueError("The supplied device verification did not pass")
    subprocess.run([sys.executable, "-m", "esptool", "version"], check=True, capture_output=True)
    output.mkdir(parents=True)
    for name, path in needed.items():
        shutil.copyfile(path, output / name)
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin",
                    "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
                    "-o", str(output / "initial-firmware.bin"),
                    "0x0", str(output / "bootloader.bin"), "0x8000", str(output / "partitions.bin"),
                    "0xe000", str(output / "boot_app0.bin"), "0x10000", str(output / "firmware.bin")], check=True)
    # A separate seed image keeps downloaded games and SDK CoreLibs out of the package.
    # The TemporaryDirectory is always rooted under the explicitly selected output.
    with tempfile.TemporaryDirectory(prefix="example-data-", dir=output) as temporary:
        seed = Path(temporary).resolve()
        seed.relative_to(output)
        stage_game(args.example.resolve(), seed / "games/crank-paddle")
        subprocess.run([str(mklittlefs), "-c", str(seed), "-b", "4096", "-p", "256",
                        "-s", str(0x4F0000), str(output / "example-littlefs.bin")], check=True)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=project, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=project, text=True).strip())
    source_files = [project / "platformio.ini", project / "partitions.csv", project / "boards/m5dial.json"]
    for directory in (project / "src", project / "include", project.parent / "runtime/src",
                      project.parent / "runtime/vendor/lua", project.parent / "runtime/deps/zlib"):
        source_files.extend(p for p in directory.rglob("*") if p.suffix in (".c", ".cpp", ".h", ".in"))
    sources = {p.relative_to(project.parent).as_posix(): sha256(p) for p in sorted(set(source_files))}
    (output / "source-hashes.json").write_text(json.dumps(sources, indent=2), encoding="utf-8")
    if report:
        shutil.copyfile(args.verification / "report.json", output / "verification.json")
    manifest = {"board": "m5dial", "chip": "esp32s3", "flashBytes": 8*1024*1024,
                "sourceRevision": revision, "sourceDirty": dirty,
                "createdAt": datetime.now(timezone.utc).isoformat(),
                "serialIntegrationVerified": report is not None, "manualHardwareReview": "pending",
                "scope": "Lua PDX, single-tone synth; no native ARM/C or PCM",
                "files": {p.name: {"bytes": p.stat().st_size, "sha256": sha256(p)}
                          for p in output.iterdir() if p.is_file()}}
    readme = """# Playdate M5Dial 固件

适用于 M5Stack M5Dial / StampS3（ESP32-S3，8 MiB Flash，无 PSRAM）。
`firmware.bin` 是应用程序；`initial-firmware.bin` 包含引导程序、分区表和应用。
`example-littlefs.bin` 仅包含本项目的 Crank Paddle 游戏，不包含下载游戏或 SDK CoreLibs。

## 烧录

安装 Python 与 esptool 5.5：`python -m pip install esptool==5.5.0`。
在此目录使用串口 COM9（可按实际设备替换）：

首次安装：下面两条命令将替换原有固件、NVS 设置，以及文件系统中的游戏和存档。
请先保留设备备份。M5Dial 开发设备的原始完整固件备份另存于本机验证目录。

```powershell
python -m esptool --chip esp32s3 --port COM9 --before usb-reset write-flash 0x0 initial-firmware.bin
python -m esptool --chip esp32s3 --port COM9 --before usb-reset write-flash 0x310000 example-littlefs.bin
```

已经安装本移植时，只更新应用可保留游戏和存档：

```powershell
python -m esptool --chip esp32s3 --port COM9 --before usb-reset write-flash 0x10000 firmware.bin
```

## 操作

旋钮选择游戏，短按实体按钮启动。旋钮在游戏中对应摇柄；下方触控区提供方向键和 A/B。
长按实体按钮超过 600 毫秒后松开，返回游戏库。按键按住时会有反色反馈。
完整 400×240 游戏画面缩放为 200×120，不裁切。

这是 Lua PDX 的早期移植，原生 ARM/C 游戏、PCM 音频及网络尚未支持。
内部 RAM 限制了可运行的游戏；Music Box 当前会在加载 UI 时内存不足。
更多游戏可在源码项目用本地 Playdate SDK 编译/导入。

## 验证信息

`manifest.json` 提供构建来源与文件 SHA-256，`source-hashes.json` 提供源码快照指纹。
传入通过的真机验证记录时，附带 `verification.json`；其检查范围以各字段为准。
串口注入检查不替代实体 LCD、触控、旋钮和扬声器的人工验收。
"""
    (output / "README.md").write_text(readme, encoding="utf-8")
    manifest["files"]["README.md"]={"bytes": (output / "README.md").stat().st_size,
                                     "sha256": sha256(output / "README.md")}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    with ZipFile(archive, "w", ZIP_DEFLATED) as z:
        for path in sorted(output.iterdir()):
            if path.is_file():
                z.write(path, path.name)
    print(json.dumps({"directory": str(output), "archive": str(archive), "sha256": sha256(archive)}, indent=2))

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=Path("D:/PlaydateAI/build/esp32/m5dial"))
    p.add_argument("--pio-core", type=Path, default=Path("D:/PlaydateAI/toolchains/platformio-core"))
    p.add_argument("--example", type=Path, default=Path("D:/PlaydateAI/esp32-games/crank-paddle.pdx"))
    p.add_argument("--verification", type=Path)
    p.add_argument("--output", type=Path, required=True)
    package(p.parse_args())
