---
version: alpha
name: Playdate Switch
description: A maximum-size Playdate LCD with graphite controller-driven system panels.
colors:
  primary: "#FFC600"
  background: "#252C31"
  text: "#F4F2EA"
  muted: "#BCC3C8"
  raised: "#353E45"
  frame: "#5C6971"
  danger: "#FFA391"
typography:
  body:
    fontFamily: "Nintendo shared Simplified Chinese font"
    fontSize: "26px"
  title:
    fontFamily: "Nintendo shared Simplified Chinese font"
    fontSize: "38px"
spacing:
  edge: "24px"
  listRow: "78px"
  settingRow: "82px"
rounded:
  default: "0px"
components:
  selection:
    backgroundColor: "{colors.raised}"
    textColor: "{colors.text}"
  status:
    textColor: "{colors.danger}"
---

# Playdate Switch design system

## Overview

This is a controller-first product for the existing Chinese-speaking user on
an original Switch, Lite or OLED capable of homebrew. The first version runs
Lua games, has no UE frontend and no translation feature. Original game names
and PDGames content retain their original language. There is no Japan-market
scope. The accepted Playdate yellow/LCD reference and graphite Android panels
remain the visual identity; Switch gameplay fills the display without a 3D model.

Model B: `../Unreal/Source/CrankLab/CrankTheme.h` owns the shared palette.
`tools/generate.py` exports it to `build-*/generated/theme.h`; native SDL panels
consume that generated adapter. This document mirrors the relevant roles.
No UE runtime is linked by this adaptation. Native text geometry belongs to
the Switch frontend and uses the console's shared font.

## Colors

Yellow letterboxing surrounds the uncropped 5:3 game image. Graphite panels use
pale text; yellow selection rails and an A marker make focus clear without
relying on color alone. Errors stay in the owning panel until dismissed.

## Typography

The console provides a Simplified Chinese font through libnx. SDL_ttf renders
native system labels at 26 px and titles at 38 px. Desktop verification uses
Noto Sans CJK through PLAYDATE_UI_FONT. Game text comes from the game's own pixels.

## Layout

The display preserves 400:240 at every size. Optional integer scaling uses
3x at 1280x720 and 4x at 1920x1080. Panels have an explicit selected row and
bounded list window; navigation keeps the selected row visible. Bottom controller
instructions stay outside list scrolling. Native system panels do not add
virtual A/B buttons over the game; physical controllers provide those controls.

## Elevation & Depth

Opaque native panels separate library/settings/errors from the paused game.
The SDK custom menu remains a monochrome overlay in the simulated LCD.

## Shapes

Rectangular list rows and a narrow yellow rail carry focus. Do not add dashboard
cards, decorative charts, or unrelated visual chrome around the game.

## Motion

Games own their animation. No ambient frontend animation is required. Crank
input is angular and frame-time based; the dead zone prevents center jitter.

## Components

`App::draw` owns native library/settings/error rows and footer labels.
`cranklab_menu_draw` owns the reused SDK custom menu. `CrankInput` owns stick/trigger
rotation. `inspectGame` owns package classification and stable save identity.

## Accessibility

Every frontend action has a physical button route. Focus has a rail and activation
marker. C/mixed games remain identifiable in the library and explain their
unsupported status; selecting one does not stop the running game. Chinese font,
handheld legibility, vibration, and docking still require Switch hardware review.

## Verification

Host core checks cover Lua frames, C/mixed rejection, game switches preserving
the SDL host, suspension, save identity, crank continuity and display geometry.
Cross-compilation checks the actual devkitA64/libnx ABI. Hardware performance
remain a separate verification gate. The first deliverable is NRO only.
