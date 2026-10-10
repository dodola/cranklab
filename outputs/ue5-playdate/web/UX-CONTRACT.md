# Playdate Web behavior contract

Sources: current user objective (WebAssembly, Mahjong, Music Box, original 3D model, immersive mode),
`../Unreal/UX-CONTRACT.md` for input/lifecycle semantics, and `../../playdate-local/DESIGN.md` for the original model.

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Form | Native game action buttons | current task, validated local manifests | Mahjong / Music Box | browser launch/retry |
| Scrollbar | public/styles.css | DESIGN.md | panel geometry | computed style and short viewport |
| Toast | error-banner / session-status | worker acknowledgments | persistent error / local saved status | browser failure and storage |
| Game session | runtime-worker.mjs | crank_host.h state and session | Lua only | real frames/input/audio |
| Input | input.mjs HeldButtons | crank_host.h button mask | keys / independent pointers / physical meshes | ownership tests and browser gestures |
| Mode | app.mjs setMode | presentation state | 3D / immersive | unchanged session/frame serial |
| Panel | app.mjs setLibrary/toggleMenu | native lifecycle and menu snapshot | library / SDK menu | pause/resume and focus |

Game content stays on the local server/browser; no remote upload. Manifest file paths, sizes and SHA-256 are validated before
the currently running game is stopped. Only the two staged Lua packages are available in this version.
Failure preserves the library and shows an explicit retry action. Duplicate launch is disabled.
Non-empty pdex.bin is rejected; WebAssembly compilation of the host is not ARM guest execution.

The core runs in an isolated worker with pthreads. All rendering/audio nodes/DOM live in the browser host.
Stop requests are asynchronous: the host continues servicing filesystem proxies until the game thread exits,
then joins and persists termination saves before restoring another game's save data.
Unexpected worker failures surface as persistent errors rather than a silent black screen.

Game writes are debounced into IndexedDB; termination flushes are awaited. Saves are scoped by game bundle.
An exclusive browser Web Lock prevents two tabs in supported target browsers from simultaneously writing saves.
Site-data clearing removes local saves. The page does not claim cloud backup or emulator save states.

Opening a panel, losing focus, backgrounding or cancelling a pointer releases held input.
Key/touch owners are independent: releasing one owner must not release another owner's same button.
Merged held state drives both immersive controls and original 3D button transforms. Keyboard Z/X, WASD and arrows
depress the corresponding meshes; release rebounds. Releasing a physical mesh while a key still holds it keeps it depressed.
Visual updates never emit synthetic input that would add a second owner or leave a key stuck.
Q/E rotate continuously at an elapsed-time speed with acceleration/deceleration; dial and LCD swipe are independent alternatives.
Any turn undocks the crank. Mode changes never start or stop the runtime, reset game time or discard a save.
Screen double taps exclude drags/long presses/cancelled gestures. Fullscreen is distinct from immersive presentation.

Panels are explicitly non-modal. They have real close buttons and Escape/focus restoration, without claiming a focus trap.
The library and SDK menu cannot be open together. SDK custom items use live IDs and native queued callbacks;
check/option changes are committed through the core and resume callbacks occur after closing the menu.
Menus paint on an opaque panel above the stage; underlying game content cannot show through labels.

Host UI is zh-CN; game text keeps the package's language. No translation model or online launcher is part of this milestone.
Audio unlock occurs on an explicit launch/input gesture. Core 44.1 kHz stereo PCM is resampled if the browser's output rate differs.
Pauses clear queued PCM to avoid stale audio; mute changes browser gain without altering the game.
Reduced motion preserves input functionality while suppressing decorative transitions.

Target verification: current desktop Chrome/Edge, narrow and landscape touch browser emulation.
Other browsers and physical-phone performance require separate acceptance; static audit is not compatibility proof.
