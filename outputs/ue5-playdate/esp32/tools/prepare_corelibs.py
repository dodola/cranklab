"""Compile the locally installed SDK's shared CoreLibs for the MCU.
The SDK and generated archive are local dependencies, not repository assets.
"""
import argparse
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--sdk", required=True, type=Path)
p.add_argument("--work", type=Path, default=Path("D:/PlaydateAI/esp32-corelibs"))
p.add_argument("--data", type=Path, default=Path(__file__).resolve().parents[1] / "data")
args = p.parse_args()
args.work.mkdir(parents=True, exist_ok=True)
source = args.work / "source"
source.mkdir(exist_ok=True)
modules = sorted(f.stem for f in (args.sdk / "CoreLibs").glob("*.lua") if not f.stem.startswith("__"))
(source / "main.lua").write_text("\n".join(f"import 'CoreLibs/{name}'" for name in modules), encoding="utf-8")
(source / "pdxinfo").write_text("name=Shared CoreLibs\nbundleID=dev.cranklab.esp32.corelibs\n", encoding="utf-8")
pdx = args.work / "corelibs.pdx"
subprocess.run([str(args.sdk / "bin/pdc.exe"), "-sdkpath", str(args.sdk), str(source), str(pdx)], check=True)
args.data.mkdir(parents=True, exist_ok=True)
shutil.copyfile(pdx / "main.pdz", args.data / "CoreLibs.pdz")
print(f"Compiled {len(modules)} CoreLibs: {(pdx / 'main.pdz').stat().st_size} bytes")
