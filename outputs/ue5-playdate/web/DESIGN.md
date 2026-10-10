---
name: Playdate Web
colors:
  background: "#e8e8e8"
  ink: "#292929"
  muted: "#666666"
  primary: "#ffc600"
  surface: "#f7f7f7"
  device-control: "#ffdc72"
  control-shadow: "#b59020"
  control-edge: "#c59e2e"
  control-pressed: "#ffe697"
  control-highlight: "#fff0b0"
  lcd: "#d5d4c9"
  lcd-ink: "#303029"
  error: "#a42e20"
  line: "#c8c8c8"
  scroll-thumb: "#929292"
typography:
  body:
    fontFamily: "Segoe UI, Microsoft YaHei, Arial, sans-serif"
  utility:
    fontFamily: "Consolas, Microsoft YaHei, monospace"
rounded:
  panel: "18px"
  control: "10px"
omitted:
  - section: spacing
    reason: "Responsive handheld geometry determines controls; no separate spacing token scale."
components:
  action:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.ink}"
  action-border:
    backgroundColor: "{colors.line}"
  selected-action:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.primary}"
  device-control:
    backgroundColor: "{colors.device-control}"
    textColor: "{colors.ink}"
  device-control-pressed:
    backgroundColor: "{colors.control-pressed}"
    textColor: "{colors.ink}"
  device-control-base:
    backgroundColor: "{colors.control-shadow}"
  device-control-edge:
    backgroundColor: "{colors.control-edge}"
  device-control-inset:
    backgroundColor: "{colors.control-highlight}"
  game-display:
    backgroundColor: "{colors.lcd}"
    textColor: "{colors.lcd-ink}"
  status:
    textColor: "{colors.muted}"
  scrollbar:
    backgroundColor: "{colors.scroll-thumb}"
  error:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.error}"
---

# Playdate Web design

## 1. Overview

Product register: a working browser emulator for desktop and touch users, in Simplified Chinese.
Current task: real Pooh Mini Mahjong and Music Box execution, original 3D handheld, and immersive gameplay.
The user explicitly chose the original webpage's appearance and large yellow immersive controls.
No Japan-market business flows, accounts, payments or remote uploads are in this version.

The signature is the original yellow PBR handheld with a live game on its LCD. Utility chrome stays quiet.
This is an application, with no marketing hero, pricing cards, fabricated screenshots or decorative metrics.
Reference: `../../playdate-local/DESIGN.md`, existing Android immersive controls and the user's emulator screenshot.

## 2. Colors

Ownership model B: `public/styles.css :root` owns all UI palette values and fonts; this frontmatter mirrors them.
Runtime mapping: each color name maps to the same-named CSS variable (`colors.device-control` → `--device-control`).
`--yellow` is the semantic primary color's alias used by the device styling.
`colors.lcd` and `colors.lcd-ink` also define the live canvas palette. Game pixels remain monochrome.
The renderer retains the original device textures, materials, environment and PBR shader.
Gray surrounds the model. Immersive mode uses yellow, black LCD framing and warm yellow tactile buttons.
Yellow on charcoal identifies active toolbar actions; failures include explicit text and a retry action.

## 3. Typography

Body labels use Segoe UI with Microsoft YaHei fallback. Utility/key hints use Consolas with the same Chinese fallback.
Game text comes from the real game and core's existing bitmap-font renderer. Host menus use browser fonts.
Brand is restrained at 25px; toolbar labels are 13px and single-line. Touch labels remain readable at narrow widths.

## 4. Layout

One viewport stage, one toolbar and one stable session footer. The viewport is intentional for a game tool, not a table sizing workaround.
The game library and SDK menu use one non-modal panel recipe, one active inner scroller, and never overlap each other.
Panel opening pauses gameplay and releases held input. Returning restores the game session.
3D mode dedicates the stage to the original model. Immersive mode expands a 5:3 game display with large controls.
Portrait places LCD above Dpad and A/B/crank. Wide and short landscape places controls beside the LCD.
Loading/progress stays within the library; status and error messages do not move game controls.

## 5. Shapes

Panel radius is 18px and toolbar control radius is 10px. The display shell is thick charcoal with rounded corners.
A/B remain circular, the Dpad cross-shaped, and the crank a charcoal rotary instrument.
The Dpad is one rounded SVG cross with a shared face and base. Its four native hit targets stay transparent,
so idle controls never draw four separate tiles or squared corners. Pressing a direction tilts the shared face.
Desktop Dpad/A-B sizes are 248px/112px; portrait scales to about 191px/82px at 390px width,
and 844px short landscape uses 158px/78px. Short narrow layouts adapt; very short portrait may scroll rather than overlap controls.
Touch controls resemble the original handheld rather than generic floating mobile controls.

## 6. Motion

Button press feedback, smooth crank rotation and user-controlled orbit only. No idle orbit while playing.
Reduced motion disables UI transitions and the original interaction's animated button/crank transitions.
Mode switches change presentation, not the running game. The hidden 3D renderer pauses in immersive mode.

## 7. Components

Canonical actions, state messages and panels are shared between library, menu and both view modes.
Hold ownership belongs to `public/input.mjs`; release, focus loss and pointer cancellation are shared.
Physical model input remains owned by `../../playdate-local/interactions.js` and bridges into the same runtime.
Its visual-only button API receives the merged held state, without dispatching duplicate game input.
Keyboard and pointer owners therefore share depression/rebound, including held A/B and diagonal Dpad tilt.
Native buttons own keyboard activation and focus. The crank slider adds keyboard steps and Q/E alternatives.
Global scrollbar baseline uses standards-based properties and WebKit fallback, with forced-colors reset.

## 8. Do's and Don'ts

Keep real gameplay and the original device appearance central. Use names users recognize; hide implementation diagnostics from normal UI.
Do not show a fake download percentage, replace real games with reimplementations, autoplay audio without interaction,
add visible clockwise/counterclockwise buttons, or restart a game when changing view modes.
Do not copy original model assets into an independently maintained duplicate; development and packaging reuse the maintained source.

## Reconcile drift

The old local viewer contract prohibited visible controls because it was a model-only demo. This separate application
intentionally adds a toolbar and game library under the user's emulator scope; the original demo remains unchanged.
The original gray/yellow identity and material renderer are retained. New chrome uses its documented palette and shared recipes.
