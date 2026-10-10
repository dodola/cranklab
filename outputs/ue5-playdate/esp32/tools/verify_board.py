"""Run real PDX games on the attached board and check frames and runtime input."""
import argparse
import json
import time
import zlib
from pathlib import Path
import serial

class Board:
    def __init__(self, port, output):
        self.output = output
        self.log = []
        self.serial = serial.Serial()
        self.serial.port = port
        self.serial.baudrate = 115200
        self.serial.timeout = 0.3
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.open()

    def command(self, command):
        self.serial.write((command + "\n").encode())
        self.serial.flush()

    def read_until(self, prefix, timeout=15):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            line = self.serial.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            self.log.append(line if not line.startswith("FRAME ") else f"FRAME {len(line)-6} hex characters")
            if line.startswith("ERROR"):
                raise RuntimeError(line)
            if line.startswith(prefix):
                return line[len(prefix):]
        raise TimeoutError(f"No {prefix!r} response")

    def state(self):
        self.serial.reset_input_buffer()
        self.command("status")
        return json.loads(self.read_until("STATE "))

    def wait_state(self, predicate, timeout=5):
        # Periodic telemetry can already be in flight when a command arrives.
        # Wait for the transition instead of treating that older frame as its reply.
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            state = self.state()
            if predicate(state):
                return state
            time.sleep(0.05)
        raise TimeoutError(f"Board did not reach the expected state: {state}")

    def frame(self, name):
        self.serial.reset_input_buffer()
        self.command("frame")
        bits = bytes.fromhex(self.read_until("FRAME "))
        assert len(bits) == 12000
        (self.output / (name + ".pbm")).write_bytes(b"P4\n400 240\n" + bits)
        return bits

    def close(self):
        self.serial.close()
        (self.output / "serial.log").write_text("\n".join(self.log), encoding="utf-8")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", default="COM9")
    p.add_argument("--output", type=Path, default=Path("D:/PlaydateAI/esp32-verification"))
    p.add_argument("--music", action="store_true", help="Also probe Music Box compatibility")
    p.add_argument("--asteroids", action="store_true", help="Run the official SDK's complete Asheteroids game")
    p.add_argument("--asteroids-seconds", type=float, default=30,
                   help="Sustained Asheteroids run duration, including repeated thrust/shoot input")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    board = Board(args.port, args.output)
    report = {}
    try:
        board.command("load sdk-crank")
        board.read_until("RUN ")
        time.sleep(1)
        first = board.state()
        before = board.frame("sdk-crank-before")
        assert first["playing"] and first["frames"] > 0
        assert any(b != 0 for b in before) and any(b != 255 for b in before)
        board.command("crank 90")
        time.sleep(0.25)
        after_state = board.state()
        after = board.frame("sdk-crank-after")
        assert after_state["crank"] == 90, after_state
        assert before != after, "Crank input must change the real SDK game's drawing"
        assert after_state["frames"] > first["frames"]
        report["sdkCrank"] = {"before": first, "after": after_state, "framesChanged": True}
        board.command("load crank-paddle")
        board.read_until("RUN ")
        time.sleep(0.5)
        board.command("buttons 32")
        time.sleep(0.1)
        board.command("buttons 0")
        time.sleep(0.1)
        paused = board.frame("paddle-paused")
        time.sleep(0.3)
        assert paused == board.frame("paddle-still-paused"), "A must pause the game's simulation"
        board.command("buttons 2")
        time.sleep(0.3)
        board.command("buttons 0")
        moved = board.frame("paddle-right")
        assert moved != paused, "Direction input must move the game's paddle while paused"
        board.command("crank -60")
        time.sleep(0.1)
        assert board.frame("paddle-crank") != moved, "Cranked callback must move the paddle"
        report["paddle"] = board.state()
        if args.asteroids:
            board.command("load sdk-asheteroids")
            board.read_until("RUN ")
            time.sleep(0.5)
            first = board.frame("asheteroids-before")
            board.command("buttons 20")  # Up + B: thrust and shoot.
            time.sleep(0.2)
            board.command("buttons 0")
            time.sleep(0.4)
            assert first != board.frame("asheteroids-after"), "The real SDK game must advance"
            report["asheteroids"] = board.state()
            assert report["asheteroids"]["playing"] and report["asheteroids"]["frames"] > 10
            samples = []
            until = time.monotonic() + args.asteroids_seconds
            while time.monotonic() < until:
                board.command("buttons 20")
                time.sleep(0.2)
                board.command("buttons 0")
                time.sleep(0.8)
                state = board.state()
                assert state["playing"], "Asheteroids stopped during sustained input testing"
                assert state["frames"] > report["asheteroids"]["frames"], "Game frames stopped advancing"
                samples.append(state)
            report["asheteroidsSustained"] = {"seconds": args.asteroids_seconds, "samples": samples}
        if args.music:
            try:
                board.command("load music-box")
                board.read_until("RUN ")
                time.sleep(1)
                report["musicBox"] = board.state()
                board.frame("music-box")
            except (RuntimeError, TimeoutError) as error:
                report["musicBox"] = {"supported": False, "error": str(error)}
        idle_heaps = []
        for cycle in range(10):
            board.command("load sdk-crank")
            board.read_until("RUN ")
            board.command("menu")
            idle = board.wait_state(lambda state: not state["playing"] and state["luaBytes"] == 0)
            idle_heaps.append(idle["heapFree"])
        assert max(idle_heaps) - min(idle_heaps) < 4096, "Repeated game switches leak heap memory"
        report["gameSwitches"] = {"cycles": 10, "idleHeapBytes": idle_heaps}
        board.command("load sdk-crank")
        board.read_until("RUN ")
        report["recovery"] = board.state()
        assert report["recovery"]["playing"]
        report["result"] = "passed"
        print(json.dumps(report, indent=2))
    finally:
        board.close()
        (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")

if __name__ == "__main__":
    main()
