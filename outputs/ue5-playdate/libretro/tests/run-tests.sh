#!/bin/bash
# Regression tests for the libretro core, using the bundled test frontend.
#   tests/run-tests.sh path/to/cranklab_libretro.so
set -u
core="$(readlink -f "${1:?usage: run-tests.sh CORE.so}")"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project="$(cd "$here/../.." && pwd)"
work="$(mktemp -d /tmp/cranklab-tests.XXXXXX)"
harness="$work/harness"
games="$project/native-test-games"
failures=0
trap 'rm -rf "$work"' EXIT

if [ -n "${CRANKLAB_TEST_HARNESS:-}" ]; then
    harness="$(readlink -f "$CRANKLAB_TEST_HARNESS")"
    [ -x "$harness" ] || exit 2
else
    gcc -O1 -Wall -I"$here/../include" "$here/harness.c" -o "$harness" -ldl -lm || exit 2
fi
lua_only=$(python3 -I - "$core" <<'PY'
import ctypes, sys
class Info(ctypes.Structure):
    _fields_ = [('name', ctypes.c_char_p), ('version', ctypes.c_char_p), ('extensions', ctypes.c_char_p), ('fullpath', ctypes.c_bool), ('block_extract', ctypes.c_bool)]
lib = ctypes.CDLL(sys.argv[1]); info = Info()
lib.retro_get_system_info(ctypes.byref(info))
print(1 if info.version.endswith(b'-lua') else 0)
PY
) || exit 2

field() { sed -n "s/.*$1=\([^ ]*\).*/\1/p" <<<"$2" | head -1; }
run() { # stdout and stderr both land in $work/<name>.log
    local out="$1"; shift
    timeout 120 "$harness" "$core" "$@" --out "$work/$out" >"$work/$out.stdout" 2>"$work/$out.log"
    cat "$work/$out.stdout" >>"$work/$out.log"
    grep -a -o 'RESULT.*' "$work/$out.stdout" | head -1 || true
}
check() { # name condition-result
    if [ "$2" = 1 ]; then echo "PASS  $1"; else echo "FAIL  $1"; failures=$((failures + 1)); fi
}
ge() { [ -n "$1" ] && [ "$1" -ge "$2" ] && echo 1 || echo 0; }
eq() { [ "$1" = "$2" ] && echo 1 || echo 0; }

# Fixtures
python3 -I "$project/tools/make-fixture.py" --output "$work/basic.pdx" >/dev/null
mkdir -p "$work/life.pdx"
printf 'name=Lifecycle\nbundleID=local.crank.life\nversion=1.0\n' >"$work/life.pdx/pdxinfo"
cat >"$work/life.pdx/main.lua" <<'LUA'
local gfx = playdate.graphics
local n = 0
function playdate.gameWillPause() print("LIFE pause") end
function playdate.gameWillResume() print("LIFE resume") end
function playdate.gameWillTerminate()
  local f = playdate.file.open("saved.txt", playdate.file.kFileWrite)
  f:write("saved=" .. n)
  f:close()
end
function playdate.update() n = n + 1 gfx.clear(gfx.kColorWhite) gfx.drawText(tostring(n), 10, 10) end
LUA

echo "== Lua game, video, audio, input"
r=$(run lua "$work/basic.pdx/main.lua" --frames 60 --realtime --hold 10:40:a)
check "lua loads" "$(eq "$(field load "$r")" ok)"
check "one video frame per run" "$(eq "$(field video "$r")" 60)"
check "1470 audio frames per run" "$(eq "$(field audio_frames "$r")" 88200)"
check "audio is audible" "$(ge "$(field audio_peak "$r")" 1000)"
check "A button changes the picture" "$(ge "$(field changed "$r")" 3)"
r=$(run crank "$work/basic.pdx/main.lua" --frames 60 --realtime --hold 5:40:r)
check "crank input animates" "$(ge "$(field changed "$r")" 30)"
r=$(run palette "$work/basic.pdx/main.lua" --frames 20 --opt cranklab_palette=black_white)
check "palette option accepted" "$(eq "$(field load "$r")" ok)"

echo "== Archives and device (ARM) code"
if [ "$lua_only" = 1 ]; then
    for g in Life Mixed-ABI; do
        r=$(run "reject-$g" "$games/$g.zip" --frames 5)
        check "$g.zip is rejected in Lua-only mode" "$(eq "$(field load "$r")" failed)"
        check "$g rejection explains C/Lua+C limitation" "$(grep -q 'Lua-only core' "$work/reject-$g.log" && echo 1 || echo 0)"
    done
    # A bundle containing Lua and C must not silently skip its native extension.
    mkdir -p "$work/mixed.pdx" "$work/empty-native.pdx"
    printf 'native' > "$work/mixed.pdx/pdex.bin"
    printf 'error("Lua entry must not run")\n' > "$work/mixed.pdx/main.lua"
    r=$(run reject-mixed "$work/mixed.pdx/main.lua" --frames 5)
    check "Lua entry alongside a C extension is rejected before execution" "$(eq "$(field load "$r")" failed)"
    check "mixed-game rejection names the limitation" "$(grep -q 'Lua-only core' "$work/reject-mixed.log" && echo 1 || echo 0)"
    cp "$work/life.pdx/main.lua" "$work/empty-native.pdx/main.lua"
    : > "$work/empty-native.pdx/pdex.bin"
    r=$(run empty-native "$work/empty-native.pdx/main.lua" --frames 10)
    check "empty pdex.bin placeholder does not block Lua" "$(eq "$(field load "$r")" ok)"
else
for g in Life Sprite-Game Particles Mixed-ABI Audio-C Hello-World Core-ABI; do
    r=$(run "zip-$g" "$games/$g.zip" --frames 40 --realtime)
    check "$g.zip runs" "$(eq "$(field load "$r")" ok)"
done
r=$(run audioc "$games/Audio-C.zip" --frames 40 --realtime)
check "C audio reaches the frontend" "$(ge "$(field audio_peak "$r")" 1000)"
r=$(run runaway "$games/Runaway.zip" --frames 10 --realtime)
check "runaway C loop fails cleanly" "$(eq "$(field load "$r")" failed)"
r=$(run unsupported "$games/Unsupported-C.zip" --frames 10 --realtime)
check "unimplemented C API fails cleanly" "$(eq "$(field load "$r")" failed)"
check "failure names the API" "$(grep -q 'graphics.copyBitmap' "$work/unsupported.log" && echo 1 || echo 0)"
fi

echo "== Lifecycle and saves"
r=$(run life "$work/life.pdx/pdxinfo" --frames 60 --realtime --stall 30:800)
check "frontend stall pauses and resumes the game" "$(grep -q 'LIFE pause' "$work/life.log" && grep -q 'LIFE resume' "$work/life.log" && echo 1 || echo 0)"
check "gameWillTerminate wrote a save file" "$([ -f "$work/life/saves/cranklab/local.crank.life/saved.txt" ] && echo 1 || echo 0)"
r=$(run cycles "$work/life.pdx/pdxinfo" --frames 20 --cycles 5 --reset-at 8)
check "five load/unload cycles with reset" "$(eq "$(field video "$r")" 100)"

echo "== System menu overlay"
mkdir -p "$work/sdk.pdx"
cp "$project/tools/fixtures/sdk-menu.lua" "$work/sdk.pdx/main.lua"
printf 'name=SDK menu\nbundleID=com.cranklab.sdkmenu\n' >"$work/sdk.pdx/pdxinfo"
sdk_events() { tr '\n' ',' <"$work/$1/saves/cranklab/com.cranklab.sdkmenu/events.txt" 2>/dev/null; }
install_font() { mkdir -p "$work/$1/system/cranklab/fonts"; cp "$project/Unreal/Content/Games/launcher.pdx/fonts/cranklab-zh.pft" "$work/$1/system/cranklab/fonts/"; }
install_font menu1
r=$(run menu1 "$work/sdk.pdx/pdxinfo" --frames 40 --realtime --hold 8:2:start --hold 12:2:down --hold 16:2:a --hold 20:2:down --hold 24:2:right --hold 28:2:b)
check "menu: checkmark and option changes apply, in order, around pause/resume" "$(eq "$(sdk_events menu1)" "boot,pause,check:true,option:C,resume,terminate")"
install_font menu2
r=$(run menu2 "$work/sdk.pdx/pdxinfo" --frames 30 --realtime --hold 8:2:start --hold 12:2:a)
check "menu: action item runs its callback then resumes" "$(eq "$(sdk_events menu2)" "boot,pause,action,resume,terminate")"
install_font menu3
r=$(run menu3 "$work/sdk.pdx/pdxinfo" --frames 30 --realtime --hold 8:2:start --hold 20:2:b --stall 14:600)
check "menu: a frontend stall while open does not pause twice" "$(eq "$(sdk_events menu3)" "boot,pause,resume,terminate")"
r=$(run menu4 "$work/sdk.pdx/pdxinfo" --frames 40 --realtime --hold 8:2:start --hold 12:2:down --hold 12:2:a --hold 16:2:start --ppm "$work/menu4.ppm")
check "menu: Start closes the menu" "$(grep -q resume "$work/menu4/saves/cranklab/com.cranklab.sdkmenu/events.txt" && echo 1 || echo 0)"
r=$(run menu5 "$work/sdk.pdx/pdxinfo" --frames 20 --realtime --hold 8:20:start --ppm "$work/menu5.ppm")
check "menu: overlay is drawn while open" "$(python3 -I - "$work/menu5.ppm" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
dark = sum(1 for y in range(h) if px[(y * w + 200) * 3] < 0x80)
print(1 if dark == h else 0)
PY
)"
r=$(run menu6 "$work/life.pdx/pdxinfo" --frames 30 --realtime --hold 8:2:start --hold 12:2:a)
check "menu: game without items still opens, Resume closes" "$(grep -q 'LIFE pause' "$work/menu6.log" && grep -q 'LIFE resume' "$work/menu6.log" && echo 1 || echo 0)"
r=$(run menu7 "$work/life.pdx/pdxinfo" --frames 30 --realtime --hold 8:20:start --reset-at 14)
check "menu: reset while open recovers" "$(eq "$(field load "$r")" ok)"
r=$(run menu8 "$work/basic.pdx/main.lua" --frames 60 --realtime --hold 8:2:start --hold 14:2:b --hold 20:30:right)
check "menu: game input works again after closing" "$(ge "$(field changed "$r")" 5)"
r=$(run menu9 "$work/sdk.pdx/pdxinfo" --frames 30 --realtime --hold 8:2:select --hold 14:2:b --opt cranklab_menu_button=select)
check "menu: Select can be the menu button" "$(grep -q pause "$work/menu9/saves/cranklab/com.cranklab.sdkmenu/events.txt" && echo 1 || echo 0)"

echo "== Frame lock (fast-forward, pause, clock, refresh rate)"
mkdir -p "$work/clock.pdx" "$work/rate.pdx"
for name in clock rate; do
    printf 'name=%s\nbundleID=local.crank.%s\n' "$name" "$name" >"$work/$name.pdx/pdxinfo"
    {
        [ "$name" = rate ] && echo 'playdate.display.setRefreshRate(20)'
        cat <<'LUA'
local n = 0
function playdate.update() n = n + 1 end
function playdate.gameWillTerminate()
  local f = playdate.file.open("t.txt", playdate.file.kFileWrite)
  f:write(string.format("%d %d", n, playdate.getCurrentTimeMilliseconds()))
  f:close()
end
LUA
    } >"$work/$name.pdx/main.lua"
done
clock_value() { awk -v i="$2" '{print $i}' "$work/$1/saves/cranklab/local.crank.${3:-$1}/t.txt" 2>/dev/null; }
start=$(date +%s.%N)
r=$(run clock "$work/clock.pdx/pdxinfo" --frames 600)
elapsed=$(awk -v a="$start" -v b="$(date +%s.%N)" 'BEGIN{printf "%d", (b-a)*1000}')
n=$(clock_value clock 1); ms=$(clock_value clock 2)
check "unthrottled run is much faster than real time (600 frames in ${elapsed} ms)" "$([ "$elapsed" -lt 8000 ] && echo 1 || echo 0)"
check "game runs exactly one update per retro_run (got ${n:-?} for 600)" "$([ -n "$n" ] && [ "$n" -ge 599 ] && [ "$n" -le 602 ] && echo 1 || echo 0)"
check "game clock follows frames, not the wall clock (got ${ms:-?} ms, want ~20000)" "$([ -n "$ms" ] && [ "$ms" -ge 19800 ] && [ "$ms" -le 20200 ] && echo 1 || echo 0)"
r=$(run clockstall "$work/clock.pdx/pdxinfo" --frames 120 --realtime --stall 60:1500)
n=$(clock_value clockstall 1 clock)
check "a 1.5 s frontend stall adds no game frames (got ${n:-?} for 120)" "$([ -n "$n" ] && [ "$n" -ge 119 ] && [ "$n" -le 123 ] && echo 1 || echo 0)"
r=$(run rate "$work/rate.pdx/pdxinfo" --frames 100)
check "refresh-rate change is reported to the frontend" "$(eq "$(field final_fps "$r")" 20.00)"
ms=$(clock_value rate 2)
check "game clock uses the new rate (got ${ms:-?} ms, want ~5000)" "$([ -n "$ms" ] && [ "$ms" -ge 4900 ] && [ "$ms" -le 5200 ] && echo 1 || echo 0)"
r=$(run steady "$work/basic.pdx/main.lua" --frames 150 --realtime)
check "steady tone has no underruns at normal speed (silent runs: $(field silent "$r"))" "$(eq "$(field silent "$r")" 0)"
r=$(run ffaudio "$work/basic.pdx/main.lua" --frames 300)
check "audio never stalls an unthrottled run" "$(eq "$(field load "$r")" ok)"

echo "== Crank: buttons, right stick, dock toggle"
mkdir -p "$work/crank.pdx"
printf 'name=crank\nbundleID=local.crank.crankfx\n' >"$work/crank.pdx/pdxinfo"
cat >"$work/crank.pdx/main.lua" <<'LUA'
local total, calls = 0, 0
function playdate.cranked(change) total = total + change calls = calls + 1 end
function playdate.update() end
function playdate.gameWillTerminate()
  local f = playdate.file.open("c.txt", playdate.file.kFileWrite)
  f:write(string.format("%.1f %d %s %.1f", total, calls, tostring(playdate.isCrankDocked()), playdate.getCrankPosition()))
  f:close()
end
LUA
# crank NAME harness-args... -> "total calls docked position"
crank() { local n="crank-$1"; shift; run "$n" "$work/crank.pdx/pdxinfo" "$@" >/dev/null; cat "$work/$n/saves/cranklab/local.crank.crankfx/c.txt" 2>/dev/null; }
near() { awk -v v="$1" -v want="$2" -v tol="${3:-0.6}" 'BEGIN{d=v-want; if(d<0)d=-d; print (v!="" && d<=tol) ? 1 : 0}'; }
expect() { # name "result" total calls docked [position]
    local got=($2)
    check "$1 (total=${got[0]:-?} calls=${got[1]:-?} docked=${got[2]:-?} pos=${got[3]:-?})" \
        "$([ "$(near "${got[0]:-}" "$3")" = 1 ] && [ "${got[1]:-x}" = "$4" ] && [ "${got[2]:-x}" = "$5" ] && { [ -z "${6:-}" ] || [ "$(near "${got[3]:-}" "$6")" = 1 ]; } && echo 1 || echo 0)"
}
expect "crank starts docked and still" "$(crank idle --frames 30)" 0 0 true 0
expect "R turns clockwise 6 deg/frame and undocks" "$(crank r10 --frames 30 --hold 5:10:r)" 60 10 false 60
expect "L turns counter-clockwise, position wraps to 300" "$(crank l10 --frames 30 --hold 5:10:l)" -60 10 false 300
expect "crank speed option applies" "$(crank speed --frames 30 --hold 5:10:r --opt cranklab_crank_speed=12)" 120 10 false 120
expect "L3 alone undocks without turning" "$(crank l3 --frames 30 --hold 5:2:l3)" 0 0 false 0
expect "L3 after turning docks it again" "$(crank l3dock --frames 40 --hold 5:5:r --hold 20:2:l3)" 30 5 true 30
expect "L3 pressed twice returns to docked" "$(crank l3twice --frames 40 --hold 5:2:l3 --hold 15:2:l3)" 0 0 true 0
expect "stick clockwise: first frame sets the start, then 19 x 10 deg" "$(crank cw --frames 40 --spin 5:20:30000:10)" 190 19 false 190
expect "stick counter-clockwise" "$(crank ccw --frames 40 --spin 5:20:30000:-10)" -190 19 false 170
expect "stick crossing +-180 deg does not glitch" "$(crank wrap --frames 60 --spin 5:30:30000:10:150)" 290 29 false 290
expect "stick inside the dead zone is ignored" "$(crank dead --frames 40 --spin 5:20:8000:10)" 0 0 true 0
expect "stick crank can be turned off" "$(crank off --frames 40 --spin 5:20:30000:10 --opt cranklab_analog_crank=disabled)" 0 0 true 0
expect "releasing and regrabbing the stick does not jump" "$(crank regrab --frames 60 --spin 5:10:30000:10 --spin 25:10:30000:10:200)" 180 18 false 180
expect "stick and L/R add together" "$(crank both --frames 40 --spin 5:10:30000:10 --hold 5:10:r)" 150 10 false 150
expect "crank is ignored while the menu is open" "$(crank menu --frames 40 --hold 3:2:start --hold 6:10:r --spin 6:10:30000:10)" 0 0 true 0

echo "== Archive safety"
python3 -I - "$work" <<'PY'
import sys, zipfile
work = sys.argv[1]
with zipfile.ZipFile(work + "/evil.zip", "w") as z:
    z.writestr(zipfile.ZipInfo("../../escape.txt"), "x")
    z.writestr(zipfile.ZipInfo("/tmp/cranklab-abs-escape.txt"), "x")
    z.writestr(zipfile.ZipInfo("g.pdx/pdxinfo"), "name=ok\nbundleID=local.evil\n")
    z.writestr(zipfile.ZipInfo("g.pdx/main.lua"), "function playdate.update() end")
open(work + "/notzip.zip", "wb").write(b"not an archive")
PY
r=$(run evil "$work/evil.zip" --frames 5)
check "traversal entries are skipped, game still loads" "$(eq "$(field load "$r")" ok)"
check "no file escaped the cache" "$([ ! -e "$work/evil/system/escape.txt" ] && [ ! -e "$work/evil/escape.txt" ] && [ ! -e /tmp/cranklab-abs-escape.txt ] && echo 1 || echo 0)"
r=$(run notzip "$work/notzip.zip" --frames 5)
check "non-archive is rejected" "$(eq "$(field load "$r")" failed)"
r=$(run missing "$work/does-not-exist.zip" --frames 5)
check "missing content is rejected" "$(eq "$(field load "$r")" failed)"

echo
[ "$failures" = 0 ] && echo "ALL PASSED" || echo "$failures FAILED"
exit "$failures"
