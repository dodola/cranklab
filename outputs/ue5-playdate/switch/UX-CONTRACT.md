# Playdate Switch interaction contract

Scope: original Switch/Lite/OLED homebrew, Lua games only, no UE and no translation.
The goal includes PDGames browsing/download/run, local games, audio, saves,
settings, controller input, suspend/resume, and a self-contained NRO.
The user narrowed the deliverable to NRO on 2026-10-10; NSP/keyset work is deferred.

| Capability | Canonical owner | Source of truth | Allowed variants | Verification |
|---|---|---|---|---|
| Game selection | inspectGame / App::launch | Lua entry, pdxinfo, native-code guard | local folder; installed download; bundled launcher | failure retains game; C/mixed rejected |
| Status | App::fail and owning launcher state | host job/session | persistent error; progress; retry | injected errors; retained selection |
| Direct input | App::controls / CrankInput | Playdate button and crank ABI | Joy-Con/Pro; desktop probe | masks, wrap, held input, releases |
| Settings | App::settingsSave | settings.txt | integer scale; haptics; volume; crank speed | persistence and clamping |
| Library selection | App::draw / scanLibrary | current SD library | bounded selected row | empty, long list, unavailable game |
| Default launcher | Switch launcher Lua adapter | supplied PDGames build 18 | Details; Download/Run/Cancel; original text | scoped requests, restore detail, no translation action |
| SDK menu | cranklab_menu_draw / SDK menu ABI | game-owned menu snapshot | action/check/options/image; host navigation | callbacks, pause, input suppression |
| Suspend | App::pause / runtime lifecycle | focus/background plus active panel | Home/sleep; panels; controller disconnect | no stuck input; audio/time freeze; resume |
| Installation | Switch download/import service | validated PDX plus published index | public ZIP; manually copied PDX | traversal, corrupt/partial data, C guard, cancel |

Switch storage lives below `sdmc:/switch/playdate`: games, per-bundle saves and
settings are separate. Internal bundle ID owns save identity across upgrades.
The bundled launcher is read-only in RomFS. Its optional ARM QR acceleration
module is omitted only for the pinned supplied build; documented Lua QR fallback
must pass a real launcher-flow test. Arbitrary C or mixed games are never stripped
or run. No model downloader, translator, or translation UI is included.

Opening a native panel releases held game input and pauses the game. Closing
suppresses the activating A/B until released. Leaving the process stops the
game before SDL/curl teardown. Host SDL window/controller ownership survives
game restarts. A completed download only launches while its original detail
and launcher session are active; otherwise it remains in the library.

There are no purchase/account automation flows. Publisher downloads use currently
exposed public package buttons and preserve demo/full labels. Failed or unsupported
downloads retain a retry/manual-import route. Cancellation invalidates old callbacks
and removes only the job's own staged files. Installed games work offline.

The final artifact gate checks the AArch64 ELF, NRO header and asset sections,
bundled launcher, source exclusions, and SHA256. Actual Switch launch and hardware
behavior must be distinguished from host tests and cross-compilation.
