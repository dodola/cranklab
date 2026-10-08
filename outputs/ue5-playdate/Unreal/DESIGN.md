---
version: alpha
name: CrankLab
description: A playable yellow handheld with a precision instrument HUD on a grey stage.
colors:
  primary: "#FFC600"
  background: "#E8E8E8"
  text: "#232323"
  danger: "#A1261E"
  hudSurface: "#252C31"
  hudRaised: "#353E45"
  hudText: "#F4F2EA"
  hudMuted: "#BCC3C8"
  hudFrame: "#5C6971"
  hudError: "#FFA391"
  keyCap: "#FFDA68"
  keyPressed: "#ECB100"
  keyEdge: "#AB7B00"
  lcdFrame: "#302E29"
  lcdBackground: "#D4D2C9"
typography:
  body:
    fontFamily: "Roboto, Droid Sans Fallback"
    fontSize: "16px"
  label:
    fontFamily: "Roboto, Droid Sans Fallback"
    fontSize: "16px"
  title:
    fontFamily: "Roboto, Droid Sans Fallback"
    fontSize: "22px"
  utility:
    fontFamily: "Roboto, Droid Sans Fallback"
    fontSize: "12px"
  gameKey:
    fontFamily: "Roboto, Droid Sans Fallback"
    fontSize: "40px"
spacing:
  target: "48px"
  gap: "12px"
  edge: "20px"
rounded:
  control: "9px"
  panel: "18px"
components:
  game-library:
    backgroundColor: "{colors.hudSurface}"
    textColor: "{colors.hudText}"
    typography: "{typography.body}"
  crank-action:
    backgroundColor: "{colors.hudSurface}"
    textColor: "{colors.hudText}"
    typography: "{typography.body}"
    height: "48px"
    rounded: "{rounded.control}"
  instrument-track:
    backgroundColor: "{colors.hudFrame}"
  crank-action-hover:
    backgroundColor: "{colors.hudRaised}"
    textColor: "{colors.hudText}"
  crank-action-pressed:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.text}"
  instrument-caption:
    backgroundColor: "{colors.hudSurface}"
    textColor: "{colors.hudMuted}"
    typography: "{typography.utility}"
  stage-label:
    backgroundColor: "{colors.background}"
    textColor: "{colors.text}"
    typography: "{typography.label}"
  error:
    backgroundColor: "{colors.hudSurface}"
    textColor: "{colors.hudError}"
    typography: "{typography.body}"
  stage-error:
    backgroundColor: "{colors.background}"
    textColor: "{colors.danger}"
    typography: "{typography.body}"
  virtual-key:
    backgroundColor: "{colors.keyCap}"
    textColor: "{colors.text}"
    typography: "{typography.gameKey}"
  virtual-key-pressed:
    backgroundColor: "{colors.keyPressed}"
    textColor: "{colors.text}"
  virtual-key-edge:
    backgroundColor: "{colors.keyEdge}"
  immersive-lcd:
    backgroundColor: "{colors.lcdBackground}"
  immersive-lcd-frame:
    backgroundColor: "{colors.lcdFrame}"
---

# CrankLab design system

## Overview

The Android application name is **Playdate**. Its launcher icon is a miniature
yellow handheld with a silver side crank and a friendly pixel screen. Startup
uses a matching ivory Playdate lockup centered on graphite `#252C2C`; the
horizontal and vertical layouts preserve the artwork's aspect ratio. Android
system launch windows use the same icon/background. Branding masters and exact
image_gen prompts live in `Build/Branding`; exported Android resources live in
`Build/Android/res`. The internal CrankLab project name remains a build detail.

The user's original brief keeps only the Playdate model, makes it larger, and
allows pressing the buttons and continuously turning the crank. The signature
is that physical yellow device playing a real game. It must not become a
dashboard, promotional page, or collection of cards.

This is a native UE tool for a Chinese-speaking user on Android, with Windows
as the development preview. UI labels are Simplified Chinese; game names retain
their original language. No Japan-market scope is inferred. The 3D view
keeps the physical device dominant. On 2026-10-02 the user approved a visually
expressive instrument HUD: graphite surfaces, yellow edges, authored icons and
coordinated motion. The circular crank dial is its signature element.

Runtime ownership (Model B): `Source/CrankLab/CrankTheme.h` is canonical for UI
colours, font roles and sizes. This document mirrors it. Slate consumes that
header; `tools/create-ue-assets.py` adapts the backdrop to a linear material.
The supplied mesh textures remain the device's physical-material source.

## Colors

The grey stage and original device materials remain unchanged. Graphite HUD
surfaces use pale text and muted secondary labels. Yellow denotes interaction,
focus and the rotating instrument trace. Dark buttons keep a yellow outline
for selection/focus; their pressed surface uses dark text on yellow. Errors
on graphite use hudError, while errors on the grey stage keep danger.
CrankTheme.h owns each palette value; rounded brushes and vector-drawn controls
consume these tokens. hudSurface is rendered at 97% opacity.

## Typography

Slate's Roboto body and bold label roles use its Droid Sans fallback for
Chinese. Labels use 16 Slate units; font fallback must be checked in the actual
packaged application. Screen game text comes from the runtime's own pixels.
Panel headings use 22 units and instrument captions use 12 units.

## Layout

Normal framing fits the unfolded device. Double-tapping the actual projected
display switches to body framing at 88% occupancy; the crank is excluded from
that fit and may leave the viewport. The existing mesh and buttons remain
interactive. Manual orbit/zoom is preserved when entering and leaving.
Tools stay in the safe-area upper right. A compact brand/game label occupies
the upper left; the collapsible crank instrument sits at the lower right.
Library/settings panels open centrally at up to 380 units wide and a viewport-bounded
height. Each panel owns its own SScrollBox; the return action stays outside it.

On 2026-10-07 the user additionally requested an immersive view based on the
Playdate Emulator reference: no 3D model, a maximum-size uncropped 5:3 game
screen, and large virtual controls. This is an independent presentation mode,
alongside the original body-framed 3D zoom. A yellow surface, dark LCD bezel,
pale circular A/B keycaps and a cross-shaped direction pad carry the reference.
Portrait puts the LCD above the controls; wide landscape puts controls beside
it. The shared crank dial remains usable. The layout uses safe-area bounds,
reserves the utility toolbar, preserves native display pixels with nearest
sampling, and never changes the game aspect ratio. A/B targets are 72–112 units
in portrait (64 in very narrow windows) and at least 64 units in short landscape. Utility controls compact
to labeled/tooltip icons on narrow viewports, while panel actions keep text.
Compact utility buttons use a centered 22-unit icon inside a 48-unit square,
independent of the text-button layout. Following the 2026-10-08 device feedback,
the immersive crank surface grows to 128–180 units in portrait and up to 160
in landscape, constrained by the available height. Its visible dial and drag
target grow together; space is reserved below A/B without overlap.
Full-screen HUD and immersive layout containers are transparent to hit testing;
their interactive children retain native input. In immersive mode, an upward
LCD swipe turns the crank clockwise and a downward swipe reverses it. One
display-height of travel maps to a full turn at any DPI/orientation. A vertical
gesture threshold separates swipes from taps; double-tap remains available.

The immersive palette extends CrankTheme.h: keyCap/keyPressed/keyEdge own
raised/depressed key surfaces and their edge; lcdFrame/lcdBackground own the
bezel and empty LCD. Existing primary owns the yellow canvas, Ink owns A/B and
direction glyphs, and GameKey is the 40-unit bold A/B role. The graphite HUD,
shared actions, focus appearance and circular crank remain the same system.

## Elevation & Depth

Depth comes from the real mesh, texture maps and lighting. The backdrop is
matte. The HUD uses rounded graphite panels, yellow focus edges and instrument
traces. Native Slate button semantics share authored appearance, focus borders
and a small animated press offset. Panel entrances coordinate translation and
opacity with the camera transition.

The reference is the existing `playdate-local` webpage. The device material
ports its SH diffuse lighting, prefiltered LogLuv panorama and specular BRDF
to `tools/device-ibl.hlsl`; it uses the source yellow replacement #F7B529.
Packed maps use R=metallic, G=AO and B=gloss, with the original open-crank AO
variants. The webpage's ground-shadow alpha supplies the soft device shadow.
Camera tone-curve and gamut overrides avoid applying a second color transform.
These physical materials are separate from the Slate accent palette.
The mobile renderer uses HDR output with 4x MSAA and neutral camera tone-curve
settings. LDR with MSAA produced a black 3D scene on the tested Adreno 830 phone;
the HDR path preserves the reference colors and resolves correctly in Vulkan
and OpenGL. Adreno8xx renders at native display resolution; Android's DPI curve
keeps utility controls and model touch targets at their previous displayed size. Physical materials
use full floating-point precision to preserve the panorama atlas sampling and
glossy reflections on mobile.

## Shapes

Original device geometry owns all physical edges. Slate owns control shapes
and scrollbar geometry; platform differences are accepted. Important utility
targets are at least 48 Slate units high.

## Components

`SCrankAction` owns utility appearance and focus/press motion;
`SCrankOverlay::Action` constructs its business variants. Game selection preserves the panel
and path after failure, closes after accepting a launch, and restores focus to
Games. A screen loading state covers asynchronous runtime switching. The panel
is deliberately non-modal. See `UX-CONTRACT.md` for ownership.

Android offers ZIP import and PDX directory selection through the system
picker, plus direct HTTP/HTTPS ZIP downloads. Import progress, transferred bytes
and an explicit failure retry stay inside the library. Rows use metadata covers,
titles and separate running/history/capability captions; long content wraps.
Returning to play stays available during copying.
Installing a game refreshes the list without interrupting the running game.
The default PDGames launcher is the user-approved exception: its detail action
downloads, installs and starts the chosen game while that detail remains active.
The supplied app retains its original pixel design and English content. Its
owned bottom action bar has a fixed 32-pixel footprint, black selected button,
white text, and explicit Download/Run/Cancel/Retry labels. It does not replace
the surrounding 3D model or graphite HUD. Chinese progress and failures reuse
the existing Slate status slot and library actions; no palette/token changes.

Game-introduction translation belongs to the PDGames detail LCD. The footer
contains Details, Chinese/original and Download/Run, with the selected action
inverted in the launcher's monochrome style. Its width stays 400 pixels and
requires no external panel or contextual HUD row. Both physical A/left/right
and single LCD footer taps activate the same Lua adapter. Translation replaces
only the cached description bitmap, preserving screenshots, metadata, scrolling
and the download/run action. Original restores the source text and layout.
Chinese uses pocket-reader's Source Han Sans 20px atlas and glyph-width-based
layout, with UTF-8 boundaries, Latin word grouping and Chinese punctuation rules.
The model's one-time download, progress and retry controls live in settings and
use CrankTheme's existing graphite, yellow accents and font roles. The detail
language button also exposes explicit download/cancel/loading/retry states.
The immersive screen retains its full area; there is no new action above it.
Model button depression approaches its target immediately, then returns when
released. Crank motion uses elapsed time, acceleration and interpolation;
continuous rotation represents user input. There is no decorative idle motion.
Crank rotation follows the axle and handle pivots from the webpage's hierarchy.
The offset handle mesh stays attached throughout a full turn. The default UE
virtual joysticks are disabled; the model itself supplies directional controls.
The circular `SCrankDial` follows the actual crank angle and accepts dragging
or keyboard Left/Right steps. Q/E remain continuous keyboard alternatives;
clockwise/counterclockwise HUD buttons are removed. Focus loss,
backgrounding, overlay opening and scene exit clear held game input.
The independent game and mixer pause when the library opens or the application
loses activity. Returning continues the same session without replaying buffered
audio. Background callbacks let games save before Android ends their process.

Experience settings persist independent motion-follow and haptic switches,
sensitivity, strength, reduced UI motion, crank docking, 30/60 FPS and 75–100%
render resolution. These settings retain the verified mobile HDR/MSAA path.
Gyroscope input is
fused with the UE tilt sample, calibrated to the current pose and damped with
a dead zone and angle limit (10 degrees normally, 5 when enlarged). Physical
button presses and angular crank detents send frequency-limited Android click/tick
feedback. Unsupported primitives fall back to finite pulses sized for the motor's
amplitude capability, with strength retained and Android touch preferences honored.
Sensor/haptic feel is reserved for user device acceptance.
Reduced UI motion removes transition/press interpolation; real crank and
sensor-driven movement remain responsive to input.

The 400×240 device display also owns the monochrome boot, home, loading,
SDK menu and lock screens. `SCrankSystemScreen` draws into a UE render target
on the same physical screen mesh. Home uses game metadata and covers; the SDK
menu shows a black selected row with white text and supports the game's menu
image on the left. Dpad/A/B, crank and delayed screen taps navigate these
states. This is a compatibility shell, with no original firmware bundled.

Translation stays within PDGames' original monochrome detail view: the middle
footer action switches Chinese/original without a second panel. First-use model
size and progress appear in that same action. Native model settings use two
equal canonical action buttons, with the active profile visibly selected:
lightweight 440 MiB by default and Q4 1.06 GiB as an alternative. Settings never
remove another profile's downloaded file or mix its cached translations.

## Do's and Don'ts

- Keep the live model dominant in 3D; immersive mode prioritizes the LCD.
- Keep errors actionable and preserve the selected path for retry.
- Do not insert promotional copy or decorative panels around the device.
- Do not claim C extensions are compatible merely because a game's Lua entry runs.
