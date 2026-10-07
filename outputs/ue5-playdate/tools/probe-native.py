"""Real ARM PDX verification: pixels, C/Lua ownership, callbacks and ABI.

Each case uses a separate process. No phone installation or input is involved.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from PIL import Image, ImageChops


def run_case(args):
    out = Path(args.output).resolve() / args.case
    out.mkdir(parents=True, exist_ok=True)
    save = out / "saves"
    save.mkdir(exist_ok=True)
    os.environ.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", PD_HEADLESS="1")
    os.environ.pop("PD_NATIVE_TRACE", None)
    os.environ.pop("PD_EVAL", None)
    if args.case == "particles":
        os.environ["PD_EVAL"] = """
for i=1,1000 do local p=particlelib.particles.new(30);p:setNumberOfParticles(60);end
collectgarbage('collect');collectgarbage('collect')
local f=assert(playdate.file.open('gc-ok',playdate.file.kFileWrite));f:write('done');f:close()
"""
    elif args.case == "qr":
        os.environ["PD_EVAL"] = """
assert(debug.getinfo(quick_qr.generate).what=='C')
local qr=assert(quick_qr.generate('https://pdgamez.com/game/2523'))
playdate.update=function()
playdate.graphics.clear(playdate.graphics.kColorWhite)
qr:drawScaled(110,30,4)
end
"""
    library = Path(args.library).resolve()
    with os.add_dll_directory(str(library.parent)):
        core = ctypes.CDLL(str(library))
    core.crank_start.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    core.crank_copy_frame.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    core.crank_last_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    for key in ("crank_copy_native_info", "crank_copy_menu"):
        getattr(core, key).argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        getattr(core, key).restype = ctypes.c_uint32
    core.crank_set_buttons.argtypes = [ctypes.c_uint32]
    core.crank_set_suspended.argtypes = [ctypes.c_uint32]
    core.crank_set_menu_open.argtypes = [ctypes.c_int]
    core.crank_menu_select.argtypes = [ctypes.c_uint32, ctypes.c_int]
    core.crank_copy_audio.argtypes = [ctypes.c_void_p,ctypes.c_uint32]
    pixels = (ctypes.c_uint8 * (400 * 240 * 4))()

    def error():
        buf = ctypes.create_string_buffer(2048)
        core.crank_last_error(buf, len(buf))
        return buf.value.decode(errors="replace")

    def snapshot(key="crank_copy_native_info"):
        fn = getattr(core, key)
        needed = fn(None, 0)
        buf = ctypes.create_string_buffer(max(needed, 2))
        fn(buf, len(buf))
        return json.loads(buf.value or b"{}")

    def advance(seconds):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            assert core.crank_status() != 3, error()
            time.sleep(.01)

    def wait(condition, timeout=30):
        until = time.monotonic() + timeout
        while not condition():
            assert time.monotonic() < until, "condition timed out: " + error()
            advance(.02)

    def capture(name):
        assert core.crank_copy_frame(pixels, len(pixels)) > 0
        img = Image.frombytes("RGBA", (400, 240), bytes(pixels), "raw", "BGRA").convert("RGB")
        img.save(out / (name + ".png"))
        return img

    assert core.crank_start(str(Path(args.game).resolve()).encode(), str(save).encode())
    stopped = False
    result = {"case": args.case}
    try:
        if args.case=="watchdog":
            # The first update never returns; stopping must still interrupt
            # Unicorn's execution quantum without waiting for the watchdog.
            advance(.25);start=time.monotonic();core.crank_stop();stopped=True
            result["stopLatencySeconds"]=time.monotonic()-start
            assert result["stopLatencySeconds"]<.75 and not error(),result
            assert core.crank_start(str(Path(args.game).resolve()).encode(),str(save).encode())
            stopped=False;until=time.monotonic()+5
            while core.crank_status()!=3 and time.monotonic()<until:time.sleep(.01)
            assert core.crank_status()==3,"watchdog did not trip"
            assert "watchdog" in error(),error()
            result["watchdogError"]=error();result["passed"]=True
            (out/"results.json").write_text(json.dumps(result,indent=2),encoding="utf-8");print(json.dumps(result));return
        wait(lambda: core.crank_status() == 2)
        advance(.4)
        first = capture("frame")
        if args.case in ("hello","life","sprites"):
            wait(lambda:snapshot().get("armCalls",0)>10)
            advance(.5)
            second = capture("moved")
            assert ImageChops.difference(first, second).getbbox(), "Hello World did not move"
            assert first.getextrema()[0] == (0, 255), "empty C framebuffer"
            assert snapshot()["mode"] == "C" and snapshot()["armCalls"] > 10
            result["movingFramebuffer"] = True
            if args.case=="sprites":
                core.crank_set_buttons(32);advance(1.5);core.crank_set_buttons(0);advance(2.5);capture("after-fire")
                assert "sprite.moveWithCollisions" in snapshot()["usedApis"]
        elif args.case == "particles":
            gc_files=list(save.rglob("gc-ok"))
            assert len(gc_files)==1 and gc_files[0].read_text() == "done", "GC workload did not finish"
            info = snapshot()
            assert info["mode"] == "Lua+C" and info["resources"] <= 8, info
            assert info["heapBytes"] < 20000, info
            assert "lua.getArgObject" in info["usedApis"]
            core.crank_set_buttons(4)
            advance(.1)
            core.crank_set_buttons(0)
            advance(.5)
            capture("after-up")
            result["collectedObjects"] = 1000
            result["boundedGuestHeap"] = info["heapBytes"]
        elif args.case == "qr":
            import cv2
            # OpenCV's full landscape-image locator misses this small code;
            # decode the known, unchanged QR display region with its quiet zone.
            image=cv2.imread(str(out / "frame.png"))
            decoded, points, _ = cv2.QRCodeDetector().detectAndDecode(image[15:210,90:300])
            assert decoded == "https://pdgamez.com/game/2523", (decoded, points)
            info = snapshot()
            assert info["mode"] == "Lua+C" and "graphics.getBitmapData" in info["usedApis"], info
            result["nativeQrDecoded"] = decoded
        elif args.case=="mixed":
            assert next(save.rglob("mixed-ok")).read_text()=="done"
            assert first.getpixel((10,15))==(0,0,0),"C ownership release destroyed the Lua bitmap"
            info=snapshot();assert info["mode"]=="Lua+C" and info["resources"]<5,info
            result["retainReleaseUservaluesNestedCallsAndSharedBitmap"]=True
        elif args.case=="audio":
            audio=(ctypes.c_int16*8192)();samples=[];start=time.monotonic()
            while time.monotonic()-start<2:
                count=core.crank_copy_audio(audio,len(audio));samples.extend(audio[:count]);advance(.02)
            assert max((abs(s) for s in samples),default=0)>100,"Native C audio did not reach PCM output"
            assert next(save.rglob("audio-ok")).read_text()=="done","Native finish callback did not run"
            result["audioPeak"]=max(abs(s) for s in samples)
            result["finishCallback"]=True
        elif args.case == "abi":
            assert first.getpixel((8, 20)) == (0, 0, 0), "native 52-byte screen stride was lost"
            core.crank_set_buttons(32)
            advance(.1)
            core.crank_set_buttons(0)
            advance(.1)
            core.crank_set_suspended(1)
            core.crank_set_menu_open(1)
            wait(lambda: core.crank_status() == 4)
            menu = snapshot("crank_copy_menu")
            assert len(menu["items"]) == 3, menu
            action, check, options = menu["items"]
            assert core.crank_menu_select(action["id"], 0) == 1
            assert core.crank_menu_select(check["id"], 1) == 1
            assert core.crank_menu_select(options["id"], 2) == 1
            advance(.2)
            event_path=next(save.rglob("events.log"))
            events = event_path.read_text()
            assert "MENU_ACTION" in events and "MENU_CHECK" not in events
            core.crank_set_menu_open(0)
            core.crank_set_suspended(0)
            wait(lambda: core.crank_status() == 2)
            core.crank_set_suspended(2)
            wait(lambda: core.crank_status() == 4)
            core.crank_set_suspended(0)
            wait(lambda: core.crank_status() == 2)
            info = snapshot()
            core.crank_stop()
            stopped = True
            events = event_path.read_text().splitlines()
            required = ["SELFTEST_OK", "BUTTON_DOWN", "KEY_DOWN", "BUTTON_UP", "KEY_UP", "PAUSE", "MENU_ACTION", "MENU_CHECK", "MENU_OPTION", "RESUME", "PAUSE", "LOCK", "UNLOCK", "RESUME", "TERMINATE"]
            assert events == required, events
            assert not error(), error()
            result["events"] = events
            result["registerStackFloatingPointAbi"] = True
        result["nativeInfo"] = snapshot()
        result["passed"] = True
        (out / "results.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
    finally:
        if not stopped:
            core.crank_stop()


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--library", required=True)
    p.add_argument("--fixtures", required=True)
    p.add_argument("--pdgames", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--case", choices=["hello", "particles", "qr", "abi","mixed","audio","life","sprites","watchdog"])
    p.add_argument("--game")
    args = p.parse_args()
    if args.case:
        run_case(args)
    else:
        out = Path(args.output).resolve()
        out.mkdir(parents=True, exist_ok=True)
        games = {"hello": Path(args.fixtures) / "Hello-World/Hello-World.pdx",
                 "particles": Path(args.fixtures) / "Particles/Particles.pdx",
                 "qr": Path(args.pdgames), "abi": Path(args.fixtures) / "Core-ABI/Core-ABI.pdx",
                 "mixed":Path(args.fixtures)/"Mixed-ABI/Mixed-ABI.pdx","audio":Path(args.fixtures)/"Audio-C/Audio-C.pdx",
                 "life":Path(args.fixtures)/"Life/Life.pdx","sprites":Path(args.fixtures)/"Sprite-Game/Sprite-Game.pdx",
                 "watchdog":Path(args.fixtures)/"Runaway/Runaway.pdx"}
        reports = []
        for case, game in games.items():
            cmd = [sys.executable, __file__, "--library", args.library, "--fixtures", args.fixtures,
                   "--pdgames", args.pdgames, "--output", str(out), "--case", case, "--game", str(game)]
            run = subprocess.run(cmd, capture_output=True, timeout=50)
            log = (run.stdout + run.stderr).decode("utf-8", errors="replace")
            (out / (case + ".log")).write_text(log, encoding="utf-8")
            reports.append({"case": case, "exitCode": run.returncode, "passed": run.returncode == 0})
        (out / "summary.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
        print(json.dumps(reports, indent=2))
        sys.exit(0 if all(r["passed"] for r in reports) else 1)
