"""Build real device PDX files with the official SDK and an ARM GCC toolchain."""
import argparse
import shutil
import subprocess
from pathlib import Path


def build(sdk, gcc, output, example, source_root=None):
    source = (source_root or sdk / "C_API/Examples") / example
    target = output / example.replace(" ", "-")
    target.mkdir(parents=True, exist_ok=True)
    assets = target / "Source"
    shutil.copytree(source / "Source", assets, dirs_exist_ok=True)
    files = sorted(source.glob("*.c")) + sorted((source / "src").glob("*.c"))
    if not files:
        raise ValueError(f"No C sources: {example}")
    command = [str(gcc), "-mcpu=cortex-m7", "-mthumb", "-mfloat-abi=hard", "-mfpu=fpv5-sp-d16",
               "-O2", "-fno-common", "-mword-relocations", "-ffunction-sections", "-fdata-sections",
               "-DTARGET_PLAYDATE=1", "-DTARGET_EXTENSION=1", "-I" + str(sdk / "C_API"),
               "-I" + str(source), "-I" + str(source / "src"), "-nostartfiles",
               "-T" + str(sdk / "C_API/buildsupport/link_map.ld"),
               "-Wl,--gc-sections,--emit-relocs", *map(str, files),
               str(sdk / "C_API/buildsupport/setup.c"), "-o", str(assets / "pdex.elf")]
    subprocess.run(command, check=True)
    game = target / (example.replace(" ", "-") + ".pdx")
    subprocess.run([str(sdk / "bin/pdc.exe"), str(assets), str(game)], check=True)
    # System fonts are already compiled SDK resources and are needed by the
    # official Hello World demo's absolute /System/Fonts path.
    fonts = game / "System/Fonts"
    fonts.mkdir(parents=True, exist_ok=True)
    for name in ("Asheville-Sans-14-Bold.pft", "Asheville-Sans-14-Light.pft"):
        shutil.copy2(sdk / "Disk/System/Fonts" / name, fonts / name)
    print(game.resolve())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--gcc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--example", action="append")
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    for example in args.example or ["Hello World", "Particles"]:
        build(args.sdk.resolve(), args.gcc.resolve(), args.output.resolve(), example, args.source_root)
