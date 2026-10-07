CrankLab / UE5 Android Playdate compatibility prototype

Native core development delivery — 2026-10-02
---------------------------------------------
The current source and runtime binaries execute unencrypted pdex.bin ARM device
code with Unicorn Cortex-M7 emulation and host implementations of SDK APIs.
Pure C and mixed C/Lua entry points, callbacks and shared resources are supported.
This supersedes the earlier source-level limitation on general C extensions.
The stage-two APK remains an unchanged historical build in phase-two-arm64/.
The new package, exact verification scope and reproduction commands are recorded
in native-core-handoff.txt and verification-native-core/delivery.json.
SDK 3.1.2 table generation defines 590 function slots, not 590 implemented APIs.
Unsupported C APIs fail with their full API name. Official firmware is not used.

Stage two development delivery — 2026-10-02
------------------------------------------
New APK: android/phase-two-arm64/CrankLab-arm64.apk (UE 5.8.3, ARM64 Development).
Exact hashes, build scope and development checks: verification-phase-two/summary.json.
Chinese controls and device acceptance steps: phase-two-handoff.txt.
The stage-two APK has been built and its desktop/native logic and rendered scene
checked. It has not been installed or touch-tested on the phone; the user owns
subsequent touch, motion-follow, vibration and sustained-performance acceptance.
The earlier phone-verified APK remains in android/arm64/ without replacement.

Stage two adds a device-screen boot/home/lock/menu shell, real Lua SDK custom
menus, asynchronous game switching, cover/history library, persisted experience
and quality settings, graphite instrument HUD, motion-follow camera, haptic
button/crank detents, and body-only enlarged 3D framing on a display double tap.
Clockwise/counterclockwise HUD buttons are removed; the dial and held Q/E remain.
Android supports direct ZIP download/install and explicit failed-import retry.
Music Box's circled A/B menu symbols have an authored fallback when a selected
font omits them. PDGames list/detail/images/SDK Show QR/back were inspected; its
QR decoded successfully through the Lua fallback. That stage-two package did not
load general C extensions; the native core update above adds device-code execution.

Milestone accepted on 2026-10-02. The user will conduct subsequent touch and crank-drag testing; automated phone testing has ended. This closes the current prototype and phone black-screen repair milestone. Compatibility limits below remain documented.

Previous phone-verified milestone
--------------------------
Windows desktop runtime and UE 5.8.3 plugin compile successfully.
Native Android libraries compile for arm64-v8a and x86_64, with 16 KB ELF segment alignment.
Music Box, Pooh Mini Mahjong and PDGames execute their supplied main.pdz.
Android API 36.1 x86_64 emulator instrumentation executes all three games and produces their real frames.
Music Box and Mahjong produce nonzero 44.1 kHz stereo PCM; a 440 Hz fixture verifies the audio transfer format.
PDGames retrieves real list/detail/image data, including HTTP thumbnails and HTTPS metadata, without loading its C library.
PDGames list thumbnails and detail image were visually checked after adding save-directory image loading.
Unreal CrankVerify commandlet ran Mahjong and produced a 400x240 texture without runtime errors.
The UE 3D scene renders the original device and a live game texture in landscape and portrait.
Projected A-button input and held crank rotation pass scripted scene checks.
Device materials reuse the webpage's prefiltered environment, SH lighting and open-crank AO maps.
Web-style yellow, chrome reflections, grey backdrop and soft shadow are verified in Windows screenshots.
UE's ES3.1 mobile preview passes frame/input checks, with 4x MSAA for smoother model edges.
An earlier LDR build of the cooked UE Android application renders the model and real game frames in the isolated x86_64 emulator. The current HDR x86_64 package is rebuilt but has not been rerun there.
Vulkan software rendering passes the bounded scene checks. The connected HONOR PPG-AN00 (Adreno 830, Android 17, ARM64) also renders the device and live game screen.
The phone's LDR + MSAA path showed a black scene. Mobile HDR with a neutral tone curve retains 4x MSAA and restores rendering in Vulkan and OpenGL.
Adreno8xx uses native display resolution; the tested phone renders at 1264x2800.
The Android emulator profile disables UE 5.8's chunked PSO cache after an assertion with the software driver.
The crank follows the original pivot hierarchy; a 24-angle sweep verifies its attachment and viewport fit.
UE's default virtual joysticks are disabled in Config/DefaultInput.ini.
Bundled fixture files materialize into a physical, app-private PDX directory; this passes the Windows scene check.
The ZIP import/install implementation passes validation, duplicate-import, Unicode and binary-file tests on JVM.
All files in the three supplied games survive import unchanged and their imported copies run through the Windows C bridge.
The Windows C bridge starts and stops eight successive sessions in one process without runtime or termination-callback errors.
Game-worker suspension, relative-clock freeze, PCM pause/resume, pending playdate.wait and save persistence pass Windows C ABI checks.
The cooked UE application passes three actual Android Home/foreground cycles and checkpoint recovery after process death on the isolated emulator and ARM64 phone.
The phone also passes Android touch A down/up, release of a held touch on backgrounding, and ADB-dispatched Z key down/up.
Android-dispatched multipointer A/B presses, either release order and cancellation pass on the phone; verification-phone/touch-before/results.json records the bounded check.
Android Q/E holds rotate continuously for two seconds and stop after release. Previously imported Music Box starts with the default phone renderer and produces live frames after updating the APK.
UE Android native compilation, cooking, embedded game/model content and x86_64 application execution pass.
These are bounded scripted checks; complete gameplay compatibility and a phone release are not established.

Files
-----
Unreal/CrankLab.uproject: UE5 C++ project, with CrankRuntime plugin.
runtime/: C runtime and patched Lua VM, with Windows build dependencies.
bin/Win64/: desktop executable and native UE library.
bin/Android/: native libraries and command-line probes for each Android ABI.
tools/Build-Runtime.ps1: offline rebuild (CMake and VS 2022 C++ tools required).
tools/Build-AndroidRuntime.ps1: native Android rebuild with NDK r27c.
tools/Build-Unreal.ps1: UE Windows editor or Android game compilation.
tools/Cook-Unreal.ps1: build, cook and archive an Android APK with packaged game/model content.
tools/Build-SceneAssets.ps1: convert the original model and generate UE materials.
tools/Run-SceneProbe.ps1: render the device and verify projected input.
tools/probe-sessions.py: repeated game sessions in the same native library.
tools/Run-Desktop.ps1: launch a supplied PDX directory with separate save data.
tools/probe-games.py: bounded headless game tests, logs and framebuffer evidence.
tools/probe-host.py: test the C ABI used by Unreal.
tools/Build-ProbeApk.ps1: diagnostic instrumentation APK; assembles local test games into work/ only.
tools/probe-android.py: Android execution, frame, PCM and HTTP evidence collection.
tools/probe-ue-android.py: run the cooked Android application's bounded 3D scene probe.
tools/probe-lifecycle.py: C ABI suspension, clocks, PCM and checkpoint verification.
tools/probe-sdk-menu.py: custom menu values, callbacks, deletion and termination ordering.
tools/build-native-fixtures.py: official SDK/GCC device PDX compilation.
tools/generate-native-api.py: SDK C table layout generation.
tools/probe-native.py: real ARM C and mixed C/Lua execution, ABI and ownership checks.
tools/probe-native-loader.py: damaged binaries and same-process recovery checks.
tools/probe-pdgames-flow.py: list, detail, images, SDK Show QR and return.
tools/probe-ue-lifecycle.py: actual Android Home/foreground, touch/key dispatch and restart persistence checks.
tools/probe-ue-crank.py: actual Android Q/E key holds and release without stuck rotation.
verification-phone/: black-screen reproduction, mobile renderer evidence and phone captures.
verification-lifecycle/: Windows and Android lifecycle evidence.
verification-android/: Android emulator test results, captured frames and audio.
verification-ue.log: UE commandlet evidence.
third-party.json: provenance. No official simulator or SDK CoreLibs bundled.

Desktop run (PowerShell from this directory)
-------------------------------------------
./tools/Run-Desktop.ps1 -GamePath 'C:/path/to/game.pdx'
Use arrow keys for directions, Z/X for A/B, Q/E for continuous crank.
SDL host key bindings are defined in runtime/src/pd_main.c.

UE bridge
---------
Add CrankRuntimeComponent to an actor. StartGame accepts PDX and save directories.
ScreenTexture exposes the live 400x240 BGRA texture for a material or widget.
SetButtons uses Left=1, Right=2, Up=4, Down=8, B=16, A=32.
AddCrankDelta supplies signed degrees and crank dock status.
Runtime ticks independently of Unreal rendering. C ABI resides in runtime/include/crank_host.h.
The hosted core opens no independent window. Audio is captured from its mixer and queued into UE's procedural sound wave.
crank_copy_audio returns signed 16-bit stereo samples at 44100 Hz; Android HTTP uses a Java HttpURLConnection helper.

Rebuild and verification
------------------------
./tools/Build-Runtime.ps1
./tools/Build-AndroidRuntime.ps1
./tools/Build-Unreal.ps1 -Platform Android
./tools/Cook-Unreal.ps1 -Architecture arm64
Build CrankLabEditor Win64 Development with UE 5.8 Build.bat.
UnrealEditor-Cmd.exe Unreal/CrankLab.uproject -run=CrankVerify -Game='C:/path/game.pdx' -unattended -nullrhi
Python probe scripts require Python; framebuffer PNG conversion uses Pillow.

Outstanding work
----------------
Android SAF picker/callback, external physical keyboards and whole-game input coverage remain pending verification. Further manual touch and direct crank-drag testing is handed to the user.
Packaged fixture materialization and the cooked UE application pass on the test emulator.
The ARM64 UE APK executes on the connected phone. Rendering, bounded input/lifecycle and checkpoint recovery are verified; complete gameplay and sustained performance remain unverified.
UE procedural audio integration compiles, but actual speaker playback in the UE application has not been verified.
Repeated Windows sessions pass bounded checks; long playthroughs and memory stability still need verification.
Earlier Music Box tests exposed an intermittent menu error. The bounded stage-two menu/edit/return check ran without a runtime fault and now displays its selection symbol. The supplied package still requests an absent res/images/aButton asset; complete gameplay and the CoreLibs click sound remain unverified.
PDGames list/detail/images and its QR fallback are verified in the native bridge. Accounts and complete store use remain unverified. Its supplied package has no observed game-download/install path; CrankLab provides its own direct ZIP download/install implementation, tested on JVM and pending Android UI acceptance.
Waveform synthesis, advanced native audio/graphics, video and all-game API coverage remain incomplete. SDK custom system menus are implemented and covered by callback/lifecycle checks. Native C execution supports a verified API subset; see native-core-handoff.txt for its boundaries.
Headless probes capture PCM from a dummy output; the saved WAV files permit listening checks.
Open Unreal/CrankLab.uproject and play the Device map for the 3D scene. The Games panel accepts a local PDX directory on Windows.
For the isolated software emulator, tools/probe-ue-android.py requires --software-rendering.
The x86_64 APK is for emulator testing; the ARM64 APK is the phone build. Package hashes and exact execution scope are recorded in verification-build/android-cooked.json.
