"""Export generated Playdate artwork to Android density resources (Pillow).

Only resizes the original artwork; native drawables handle backgrounds/layout.
Run from any working directory. The master files stay outside Build/Android so
Unreal does not copy the generation prompts or source PNGs into the APK.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Unreal/Build/Branding"
RES = ROOT / "Unreal/Build/Android/res"


def export(image: Image.Image, relative: str, size: int) -> None:
    destination = RES / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.resize((size, size), Image.Resampling.LANCZOS).save(destination, optimize=True)
    print(f"{destination.relative_to(ROOT)}: {size} x {size}")


def main() -> None:
    with Image.open(SOURCE / "playdate-icon.png") as icon:
        for density, size in (
            ("ldpi", 36), ("mdpi", 48), ("hdpi", 72), ("xhdpi", 96),
            ("xxhdpi", 144), ("xxxhdpi", 192),
        ):
            export(icon, f"drawable-{density}/icon.png", size)
        export(icon, "drawable/icon.png", 192)
        export(icon, "drawable-nodpi/playdate_mark.png", 432)

    with Image.open(SOURCE / "playdate-launch.png") as launch:
        if launch.mode != "RGBA" or launch.getextrema()[3][0] != 0:
            raise ValueError("Launch lockup must retain its generated transparency")
        export(launch, "drawable-nodpi/playdate_launch.png", 840)
        # UE requires these resources to exist when bShowLaunchImage is enabled.
        # Our splashscreen_p/l drawables set their bounds instead of stretching.
        export(launch, "drawable/splashscreen_portrait.png", 840)
        export(launch, "drawable/splashscreen_landscape.png", 840)


if __name__ == "__main__":
    main()
