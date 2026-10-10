---
version: alpha
name: Playdate M5Dial
description: A complete Playdate LCD inside a round yellow console with a physical crank.
colors:
  primary: "#FFC600"
  ink: "#292D2C"
  button: "#FFDE70"
  lcdPaper: "#D6D7CE"
typography:
  console:
    fontFamily: "M5GFX Font2"
    fontSize: "16px"
spacing:
  displayWidth: "240px"
  displayHeight: "240px"
  gameWidth: "200px"
  gameHeight: "120px"
rounded:
  button: "18px"
  direction: "5px"
components:
  game:
    backgroundColor: "{colors.lcdPaper}"
    textColor: "{colors.ink}"
  action:
    backgroundColor: "{colors.button}"
    textColor: "{colors.ink}"
  pressed:
    backgroundColor: "{colors.ink}"
    textColor: "{colors.primary}"
---

# M5Dial console design

## Overview

This embedded console serves the existing project user on a connected M5Dial.
The product job is to select and play ordinary Lua PDX games on the board.
The established Playdate yellow and monochrome LCD remain the reference;
the physical rotary dial is the signature crank interaction. Device labels
use the driver's ASCII font; this port does not introduce CJK or Japan-market UI.
Documentation remains Chinese. This is a product console, with no web forms,
remote account flows, editable data tables or browser overlays.

Model B: `include/boards/m5dial.h` owns the board palette, screen dimensions,
control centers and touch targets. `src/board_m5dial.cpp` consumes those values
for drawing and input; this file mirrors them. LCD colors are stored as RGB565
(`0x2965` ink, `0xD6B9` paper); frontmatter paper shows the expanded RGB color.

## Colors

Yellow surrounds the game. Pale yellow fills touch buttons; dark ink labels
their function. Pressed controls reverse foreground and background without
moving or resizing. Library and error screens use yellow text on dark ink.
The 1-bit game retains its own black/white pixel data, mapped to LCD ink/paper.

## Typography

The console uses M5GFX Font2 at its native size. A/B remain explicit labels;
direction triangles retain familiar controller meanings. Game fonts belong
to the emulated game and are unaffected by the console font. Serial logs
retain complete technical error messages.

## Layout

The 400:240 game image is reduced exactly 2:1 to 200:120 at (20,60).
All game corners and visible controls must fit inside the 120 px radius.
D-pad controls occupy the lower left, A/B the lower right. Touch coordinates
outside the circular panel do not activate a button; the D-pad center has a
dead zone. Drawing and hit testing use the same board configuration.

## Elevation & Depth

The small display uses flat fills and clear press contrast. Avoid shadows,
decorative animation, or overlays covering the game. Library/errors own the
whole console screen while gameplay is stopped.

## Shapes

A/B use circular buttons. The D-pad is a compact cross with rounded ends;
each held direction receives a contrasting circular indicator. Round-panel
geometry takes precedence over a rectangular touchscreen layout.

## Components

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Select/Listbox | `main.cpp` library + `board_message` | README operation rules | rotary selection and physical A activation | serial switch tests + physical review |

`board_poll` owns physical input. `draw_controls` owns normal/held touch visuals;
the main loop owns library selection, launch gating and serial input overrides.
Rotary input selects in the library and becomes angular crank motion in a game.
Short physical press is A; a hold over 600 ms followed by release returns to
the library. A held launch press must not immediately press A inside a game.
Game errors remain visible until the user returns or starts another game.
Loading failure releases game resources. File-system mounting never formats
the device automatically; game saves remain separate from game packages.

No hover, browser focus, DOM semantics, or reduced-motion animation apply to
this hardware frontend. Physical rotary/button operation is the alternative
to touchscreen input. Manual LCD/touch/speaker review remains distinct from
host geometry tests and serial-injected game input.

## Do's and Don'ts

- Preserve the complete 5:3 game image and round-panel safe area.
- Use one geometry source for labels and hit testing.
- Keep held feedback inside the existing control footprint.
- Do not infer physical touch/audio success from serial-injected input.
