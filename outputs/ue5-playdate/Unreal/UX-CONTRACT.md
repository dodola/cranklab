# CrankLab interaction contract

Visual intent lives in `DESIGN.md`. This is a single native scene with a
temporary non-modal game library, rather than a CRUD application.

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Game selection | SCrankOverlay::Action / ChooseGame | this contract | library row; Windows path | launch, failure retained, focus return |
| Scrollbar | UE SScrollBox / CoreStyle | native Slate | bounded game list | narrow viewport and keyboard |
| Status | SCrankOverlay status text | ACrankDevicePawn::GetStatusText | current game; persistent error | scene screenshot; failed path |
| Direct input | ACrankDevicePawn | runtime button-mask and crank ABI | keyboard, mouse, independent touches | native probe plus scene checks |
| File selection | Windows directory field / Android SAF | FCrankGameFiles / CrankPdxImport | ZIP or PDX directory; platform picker geometry | ZIP validation passes on JVM; Android picker verification pending |
| Utility actions | SCrankAction / CrankTheme | DESIGN.md and this contract | library, settings, framing, docking | focus/pressed states and rendered captures |
| Camera framing | ACrankDevicePawn / CrankView | this contract | full device; enlarged body; manual orbit/zoom | landscape/portrait scene bounds; gesture math |
| Immersive presentation | SCrankImmersive / CrankImmersive::FLayout / ACrankDevicePawn | user request 2026-10-07 and this contract | portrait LCD above gamepad; wide landscape LCD between controls | aspect/bounds automation; rendered scenes; independent pointer and session probe |
| Experience settings | ACrankDevicePawn experience.json | this contract | independent motion/haptic switches, sensitivity, strength, reduced motion | round-trip persistence; user sensor/haptic acceptance |
| Crank instrument | SCrankDial / ACrankDevicePawn | runtime crank angle and docking | drag; keyboard steps; Q/E hold | frame/crank checks; user touch acceptance |
| Device system screens | SCrankSystemScreen / ACrankDevicePawn | ECrankSystemMode and runtime menu snapshot | boot, home, loading, menu, lock | native scene flow; actual screen captures |
| SDK custom menu | pd_sdkmenu / FCrankNative / RuntimeComponent | game-thread Lua registry and bounded command queue | normal, check, options, menu image | callbacks, deletion, pause/resume and termination tests |
| Download/import | CrankPdxImport / FCrankGameFiles | importer state and last source | SAF ZIP/folder; HTTP/HTTPS ZIP; retry | product Java contract tests; device picker pending |
| Default launcher | bundled PDGames build 18 / pd_host scoped adapter | explicit launcher session and catalog ID | original list/detail/images/QR; host detail action bar | real PDGames bridge captures; return-detail flow |
| Launcher download/run | FCrankLauncher / FCrankGameFiles::InstallArchive | host job revision, catalog ID, atomic installed index | public itch package; demo; publisher page; cancel/retry/run | live download commandlet; ZIP failure cases; full UE launcher flow |
| Quality | ACrankDevicePawn / UGameUserSettings | experience.json | 30/60 FPS; 75–100% render scale | setting round trip and ARM64 compilation; sustained device performance pending |
| Description translation | FCrankTranslation / scoped PDGames adapter | public PDGames description, model digest, prompt revision and source hash | Chinese/original; model download; cancel/retry | real GGUF inference, cache invalidation, streamed model validation and native scene captures |

Launching validates pdxinfo and a Lua entry or pdex.bin before stopping the current game.
Save directories are separate for each bundle ID (name/author or path fallback).
Re-importing a version preserves that game's save identity. A launch failure keeps
the selector and entered path visible. Empty lists offer a concrete next step.
Runtime stop/start runs on a worker; Unreal polls its future and creates screen
and audio objects only on the game thread. Loading disables duplicate launches.
Selecting the running game resumes the same session. Recent history is written
only after the first live frame, capped at 12 entries. A history marker means
“previously started”, not complete compatibility. The Android picker still needs
device acceptance.

Android import copies the selected document or directory into app-private
storage in a worker thread. ZIP paths and game metadata are validated before
an atomic install. A repeated identical package reuses the existing copy.
Selection/copy progress disables duplicate imports and game switching; returning
to the current game is still available. Completion refreshes the library and
does not switch the running game. Cancel preserves the running game and saves.
Errors remain visible with a retry action. URL downloads use the same staged
extract/install path, bounded transfer size and connection/read timeouts.
Partial, oversized or invalid archives never publish a game. Retry keeps the
last URL/URI while the application is alive. Lua, C and mixed packages are
accepted. The ARM guest core executes a subset of SDK C APIs; package acceptance
and a first frame do not establish complete game compatibility.

Each held key or touch is independently represented. Releasing one pointer
does not release another pointer's button. Dpad touches may slide between
directions. Backgrounding, focus loss or opening the library releases all
game input. Q/E rotate continuously at a time-based speed; the two former
direction buttons are removed. Crank input goes to menu/home selection while a
system screen is active. Closing a system menu suppresses the activating held
A/B until release, so it cannot leak into gameplay.
Opening the library also suspends the independent game worker and audio.
Application deactivation pauses it; Android backgrounding sends deviceWillLock,
and returning sends deviceDidUnlock before gameWillResume. Each transition runs
on the Lua thread once. Relative game clocks stop during suspension, while epoch
time remains wall time. Queued audio is discarded across transitions. Normal
game exit calls gameWillTerminate before closing Lua, including a paused game.
Background/foreground and save-after-process-death checks pass on the isolated
Android emulator and the connected ARM64 phone. The phone also passes actual
Android touch down/up, release of a held touch across backgrounding, and
ADB-dispatched Android key down/up. External physical keyboards are unverified.
Android Q/E key holds also pass continuous rotation and released-key idle checks.

The library is non-modal and does not claim modal focus trapping. Escape and
Return close it. Native Slate buttons own keyboard activation, navigation,
focus and accessible text labels. Physical mesh controls need additional
assistive-technology coverage; complete Android accessibility is unverified.

The Android target accepts imported games. The owned fixture and the user's
supplied PDGames build 18 are staged as content. Normal startup shows boot then
launches PDGames. Explicit scene probes retain their fixture startup unless the
launcher probe is selected. The local home/library remains the fallback if
PDGames fails. Whole-SDK parity remains outside verified capability.

On 2026-10-07 the user selected direct download/run in the PDGames game detail.
An owned Lua adapter adds a fixed monochrome action bar to that detail only;
the original PDX bytecode/resources are unchanged. A activates the selected
host action; Left/Up select original detail controls, Right selects the host
action. Screenshot fullscreen keeps the original controls. A delayed tap on
the projected action bar also activates it, while double tap remains framing.
The original app uses English, so its embedded bar uses English; host feedback
and recovery use Simplified Chinese and the existing Slate status/action owners.

Only an explicitly started bundled launcher receives the bridge. Requests carry
a session token; old replies cannot affect a new session or a normal game.
Downloads use a fresh publisher-exposed itch URL, transfer to a bounded physical
file, validate ZIP paths/CRC/size/one PDX/native header and atomically install.
The optional source snapshot contains catalog IDs and public upload IDs, not
signed CDN URLs, credentials or the D-drive game archives. New itch sources can
be resolved live. Demo status is explicit; ambiguous/missing/interactive source
responses retain the detail with a publisher-page/manual-import path.

Duplicate activation shares one job. Cancel invalidates network callbacks,
stops extraction and removes partial staging; callbacks close their own file
even after a replacement job starts. Failures remain visible with Retry. Local
installed IDs persist through an atomic index, and per-bundle saves remain
separate. Installed games launch without a new network lookup. A successful
detail download automatically runs only if that detail/session is still active;
leaving it or opening a panel leaves the installed game in the local library.
Menu → Return PDGames switches back and restores the last selected detail.
From PDGames, that menu action opens the local library without restarting it.

The second-stage scope is recorded in ../phase-two-plan.txt. It supersedes the
earlier plain utility appearance and adds a body-framed enlarged mode. A short
double tap must land within the actual transformed screen plane; dragging,
long presses, multiple pointers and background/overlay cancellation invalidate
the gesture. It changes only framing, preserving the running game and manual
view. Background touches own orbit/pinch. Model controls and the captured
instrument dial own their pointers independently. The two old crank direction
buttons are removed; Q/E and dial arrow-key steps remain alternatives.
Single screen taps wait for the double-tap window before activating system UI;
multi-touch, a control/background press or a cancelled gesture discards that
pending activation. Screen coordinates use the transformed LCD mesh plane.

The device Menu button (M on desktop) opens a paused SDK system menu. Physical
Dpad/A/B, crank detents and screen taps select items. Normal items close, invoke
their callback on the Lua thread, then resume; check/options callbacks run when
the menu closes, before resume. Removal and callback-driven replacement cannot
activate stale item IDs. A pending value change is flushed before termination.
The system screen combines up to three custom entries with continue, restart,
home and settings. The game menu image is retained on the left of the 400×240
screen. The selected row is black with white text.
The game preview is clipped to the left half. The opaque right menu panel,
divider and selected-row background paint above game textures; menu text paints
above those surfaces in both 3D and immersive modes.

Home supports selection/launch and links to library/settings. Returning to the
launcher stops the game asynchronously and invokes termination/save behavior. The model
Sleep control (P on desktop) enters a clock lock screen, freezes game time/audio
and sends lock callbacks; unlocking restores the saved system mode and session.
This shell simulates exposed system behavior, with no original firmware or CPU
image bundled.

Library rows distinguish focus from the currently running game, with both
an outline and an explicit running label. Up/Down moves keyboard focus and
activation remains a native button action. Library and settings share one
non-modal panel shell, one active inner scroller and a reachable return action.
Opening either suspends game input/audio; closing restores focus to its trigger.

Experience switches save independently to app-private storage through a
temporary file and atomic replacement. Disabling haptics cancels vibration.
Android uses hardware-supported click/tick primitives or a finite amplitude-aware
one-shot fallback, classified as touch feedback. Non-amplitude motors use default
amplitude and vary pulse duration with strength. Cancellation invalidates queued
UI-thread pulses; inactive windows never start feedback. System touch preferences
remain authoritative. Button/crank frequency limits are shared by 3D and immersive
input, and neither mode uses UE gamepad force feedback for the phone motor.
Disabling follow stops motion acquisition and resets its offset. Foreground
reactivation recalibrates the motion origin. Missing motion samples preserve
manual camera controls. User acceptance covers device-specific sensor/haptic
feel and physical touch operations; development captures do not prove it.
FPS and resolution settings are saved with the experience settings and use UE
quality controls without altering the mobile HDR/MSAA black-screen fix.

Stage-two development evidence lives in ../verification-phase-two and its
summary.json. The new APK is not automatically installed or touch-tested on the
connected phone: the user owns subsequent device interaction acceptance.

Immersive mode hides all device/stage primitives and draws the same live runtime
or system-screen texture in Slate, preserving 400×240 pixels and a 5:3 display.
The toolbar's screen icon and settings switch enter/leave it. Double-tapping
its LCD returns to 3D; the 3D LCD retains the existing body-zoom double tap.
Switching presentation neither restarts nor pauses the game and preserves the
3D manual orbit, zoom and body-framing choice. The mode persists in experience.json.
Held input is cleared on mode changes, layout resizing, overlays, background
and focus loss. Virtual A/B and Dpad use the canonical independent pointer mask;
sliding the Dpad into neutral or off its cross releases its direction. Releasing
one touch leaves other touches held. Keyboard X/Z, arrows and Q/E remain the
shared game/crank alternatives; M/P retain menu/lock access. The Slate toolbar
also exposes menu and lock in immersive mode. The shared crank dial replaces
physical crank picking; docking remains adjustable in settings. Motion-follow
does not move this flat screen and resumes in 3D. Haptic preferences still apply.
The utility overlay's full-screen containers pass hits through to the dial and
viewport. A vertical swipe starting inside the immersive LCD is captured by the
canonical pointer state: upward is clockwise, downward counterclockwise, with
one screen-height equal to 360 degrees. Jitter and horizontal movement do not
start the gesture. Once swiping begins it cancels pending taps/double-taps, can
continue outside the LCD, and works while other fingers hold A/B or Dpad.
Docking and opening an overlay block crank input; release, resize, mode changes,
focus loss and backgrounding clear gesture state. Light taps and double-taps
retain their existing actions. Scene probes now route real Slate touch events
through the hit-test grid and controller polling, rather than validating only
direct calls to the gamepad update method.

Utility actions keep native Slate button semantics, focus/hover/press states,
labels and tooltips when compact. Compact toolbar glyphs remain centered inside
fixed square native buttons. The immersive crank's 128–180-unit portrait surface
is both its rendered dial and captured drag area; short landscape constrains it
to fit with A/B. The virtual gamepad is a direct-input surface like the existing
physical mesh, with authored held feedback and keyboard alternatives.
Full assistive-technology coverage remains unverified. New Android
touch, sensor and haptic feel remain user-owned acceptance; Windows scene probes
establish layout/input routing and session continuity only.

The PDGames detail footer owns three actions: Details, Chinese/original and
Download/Run. Left/right select the action; A activates it, and a single LCD
footer tap uses the same scoped Lua action. Chinese replaces only the description
inside the original cached content bitmap. There is no introduction dialog or
extra HUD row, and translation does not suspend the game. Screenshots, original
headers, metadata, scrolling, B navigation and downloading/running are retained.
The language action shows loading, translating, cancel, empty and retry states.
Original stays readable until a complete translation is available. Original
restores the original cached text and height. Closed details, switched games and
backgrounding cancel outstanding HTTP requests and ignore late completions.
Replies are scoped to launcher session, catalog ID and request revision; ordinary
games have no launcher/translation bridge. Partial or malformed responses never
replace the original or enter the cache.

Chinese uses pocket-reader's Source Han Sans 20px monochrome glyph atlas,
compiled into a host-owned PFT. UTF-8 glyph widths wrap within 384 pixels; Latin
words remain together and closing Chinese punctuation avoids leading a line.
Unsupported glyphs use a visible replacement. Font lookup uses a sorted codepoint
index, and parsed fonts are released on game exit. Long descriptions scroll
through PDGames' original content bitmap; translation settings use the canonical HUD.
Translation is online; no local inference backend or model download is packaged.
Settings provides automatic, Microsoft and Tencent choices through SCrankAction.
Selection persists, cancels outstanding work and scopes cache identities by the
actual provider. Automatic tries Microsoft first and Tencent at most once after
failure. Each request has an 8-second timeout and a 1-MiB response bound. Source
text is capped at 24,000 characters and batched without splitting UTF-16 pairs.
All batch items and protected tokens must return before a result can be cached.
URLs, PDX filenames, Playdate, direction-key terms, docking phrases and button
symbols survive via typed placeholders. Vendor-inserted spacing or case changes
inside placeholders are accepted; missing/duplicated markers reject the result.

First-use translation sends only the public description to the online service.
No API key, game save or account data is involved. Cached translations appear
immediately while a description refresh runs, and remain readable offline.
Source changes invalidate the old cache and cancel its request. Fresh-cache reads
never depend on the old HY-MT2 profile. Upgrade cleanup deletes only the two known
app-private HY-MT2 files, their partials and the obsolete model preference file.
Other user-selected files, public description caches and game saves are retained.
The UPL receipt stages only the game runtime. Packaging removes obsolete
inference-library and downloader-class copies from generated Android staging.
