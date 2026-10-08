# Playdate application branding

The Android display name is **Playdate**. The package stays
`com.cranklab.emulator`, preserving the existing installation and private data.
The Unreal project/module names remain CrankLab for build compatibility.

`playdate-icon.png` is the original generated launcher artwork;
`playdate-launch.png` is the transparent launch lockup. Both were drawn with the
built-in image_gen tool. The exact generation prompts are in [PROMPTS.md](PROMPTS.md).

Run `python tools/generate-android-branding.py` from the ue5-playdate directory
after replacing either master. This exports legacy density icons and keeps the
launch image alpha channel. Resource exports are checked into Git; building an
APK does not require Pillow or image generation.

Android 8+ uses `res/drawable-anydpi-v26/icon.xml` with safe margins for circle and
squircle launcher masks. `splashscreen_p.xml` and `splashscreen_l.xml` center the
lockup at 280dp and 240dp on graphite, without stretching it to the device aspect
ratio. The UE splash dialog dismisses through the engine's existing first-frame
callback; branding adds no timers or blocking work.

Android 12+ has its own system launch window before the UE dialog. The
`values-port-v31` and `values-land-v31` themes give this window the same new icon
and graphite background. Both orientation qualifiers are needed because
Android selects orientation before API version when matching resources.

References: [Android splash screens](https://developer.android.com/develop/ui/views/launch/splash-screen),
[Android adaptive icons](https://developer.android.com/develop/ui/compose/system/icon_design_adaptive).
