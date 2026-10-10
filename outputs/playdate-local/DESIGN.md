# Playdate local model demonstration

Scope: only the original intro-canvas experience. User explicitly requests a central model area only. No logo, heading, copy, controls or footer. Responsive centered canvas on the original gray background is the entire normal page.

Tokens are owned by styles.css :root: background #e8e8e8, ink #292929, muted #666666, yellow #ffc600. Arial/Microsoft YaHei only serve loading/failure copy. Original yellow device, PBR materials and soft shadow carry the identity.

Drag rotates the device; arrows and Home provide a keyboard alternative. Gentle idle rotation stops on interaction. Screen video loops silently. Reduced motion disables automatic rotation and video at startup. Loading reserves model space; failure-only reload button and error message provide recovery. Video failure restores the default model screen.

Interactive update: button meshes split from material groups, retaining original PBR materials. A/B, d-pad, menu and top key depress while held and rebound. Clicking crank unfolds it, dragging/scrolling rotates it, double-click folds it. Body drag is isolated from part interaction. Z/X, WASD, M, C, Q/E offer keyboard equivalents. Announcements use a visually hidden live region; no visible controls or copy are added. Pointer capture, cancellation and focus-loss release prevent stuck input. Screen remains original video, not a game emulator. Canvas dispatches playdate-input events for future game integration.

The web emulator also uses the same button-depression implementation through a visual-only API. This allows its merged keyboard/pointer state to animate A/B and diagonal Dpad input without emitting another playdate-input event. Original pointer and keyboard bindings retain their existing behavior.

Size/motion update: canvas grows to 1500px wide and 1100px tall (bounded by viewport). Camera zoom fits narrow screens and reserves space for an unfolded crank. Desktop device is roughly 2.5 times the prior displayed size. Q/E are held-key inputs with time-based acceleration to 300 degrees/second; release decelerates, blur/hidden tab clears input. Pointer and wheel set a target angle; frame-based damping renders smooth rotation. Frame update shares the original viewer loop.

All assets are local. No analytics, remote runtime requests, external fonts or navigation. Original viewer retained for its custom binary geometry parser and PBR shaders.
