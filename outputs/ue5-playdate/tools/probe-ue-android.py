"""Run the cooked UE application's own bounded scene probe on a test device.

This checks actual mobile shaders, bundled fixture materialization and the
game/frame/input bridge. It does not establish ARM phone or full gameplay parity.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import struct
import time
import uuid
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('--adb', required=True)
p.add_argument('--device', required=True)
p.add_argument('--apk')
p.add_argument('--output', required=True)
p.add_argument('--package', default='com.cranklab.emulator')
p.add_argument('--game', help='Already installed physical PDX directory on the device')
p.add_argument('--renderer', choices=('opengl', 'vulkan'), default='vulkan')
p.add_argument('--software-rendering', action='store_true', help='Permit the isolated emulator software Vulkan device')
p.add_argument('--cvar', action='append', default=[], help='Test-only device-profile CVar override, e.g. r.Vulkan.UseChunkedPSOCache=0')
p.add_argument('--timeout', type=int, default=180)
p.add_argument('--visual-crank-angle', type=float)
p.add_argument('--orientation', choices=('landscape','portrait'))
p.add_argument('--arg', action='append', default=[], help='Additional development command-line argument')
a = p.parse_args()
out = Path(a.output).resolve()
out.mkdir(parents=True, exist_ok=True)
base = [a.adb, '-s', a.device]

def adb(*args, timeout=20, check=True):
    result = subprocess.run(base + list(args), capture_output=True, timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace') or result.stdout.decode(errors='replace'))
    return result

def decode(data):
    return data.decode('utf-16' if data[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')

if a.apk:
    # Some phones require the owner to confirm the update on the device.
    adb('install', '-r', str(Path(a.apk).resolve()), timeout=300)
adb('shell', 'am', 'force-stop', a.package)
remote = f'/data/user/0/{a.package}/files/CrankLab/probe-{uuid.uuid4().hex}'
command = f'-CrankSceneProbe={remote} -{a.renderer}'
if a.software_rendering:
    command += ' -AllowSoftwareRendering'
if a.cvar:
    assert all('"' not in item and ',' not in item and '=' in item for item in a.cvar), 'Invalid CVar override'
    command += ' -dpcvars="' + ','.join(a.cvar) + '"'
if a.game:
    command += ' -CrankGame="' + a.game.replace('"', '') + '"'
if a.visual_crank_angle is not None:
    command += f' -CrankViewAngle={a.visual_crank_angle}'
for argument in a.arg:
    command += ' ' + argument
launch = adb('shell', 'am start -W -n ' + shlex.quote(a.package + '/com.epicgames.unreal.GameActivity') +
             ' --es cmdline ' + shlex.quote(command), timeout=30)
(out / 'launch.txt').write_bytes(launch.stdout + launch.stderr)
deadline = time.monotonic() + a.timeout
started = time.monotonic()
result = None
try:
    assert b'Status: ok' in launch.stdout, 'Android activity launch failed; inspect launch.txt'
    while time.monotonic() < deadline:
        response = adb('exec-out', 'run-as', a.package, 'cat', remote + '/results.json', check=False)
        candidate = None
        if response.returncode == 0 and response.stdout:
            # adb exec-out can return success even when the remote cat failed.
            # Also tolerate observing the JSON while it is still being written.
            try:
                candidate = json.loads(decode(response.stdout))
            except (UnicodeDecodeError, json.JSONDecodeError):
                pass
        if isinstance(candidate, dict):
            result = candidate
            picture = adb('exec-out', 'run-as', a.package, 'cat', remote + '/device.png').stdout
            assert picture[:8] == b'\x89PNG\r\n\x1a\n', 'Missing or invalid scene screenshot'
            (out / 'device.png').write_bytes(picture)
            width,height=struct.unpack('>II',picture[16:24])
            result['viewportWidth']=width; result['viewportHeight']=height
            # Scene and game assertions can pass while the mobile resolve produces
            # a black image. Check the rendered model, not just live Slate controls.
            pixels = list(Image.open(out / 'device.png').convert('RGB').getdata())
            yellow = sum(r > 150 and g > 85 and b < 150 and r > b * 1.4 and g > b * 1.2
                         for r, g, b in pixels)
            result['modelYellowPixelFraction'] = yellow / len(pixels)
            result['visualPassed'] = result['modelYellowPixelFraction'] > .01
            result['runtimeAssertionsPassed'] = result['passed']
            result['passed'] = result['passed'] and result['visualPassed']
            (out / 'results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
            if a.orientation == 'landscape': assert width>height, 'Expected landscape screenshot'
            if a.orientation == 'portrait': assert height>width, 'Expected portrait screenshot'
            break
        if time.monotonic() - started > 20:
            running = adb('shell', 'pidof', a.package, check=False)
            assert running.stdout.strip(), 'Application exited before writing scene results'
        time.sleep(.5)
finally:
    log = adb('logcat', '-d', '-t', '2500', check=False).stdout
    (out / 'logcat.txt').write_bytes(log)
assert result is not None, 'No fresh scene results; inspect launch.txt and logcat.txt'
assert result['visualPassed'], 'Scene assertions passed but the model is absent from the rendered image; inspect device.png'
assert result['passed'], result
for line in log.decode(errors='replace').splitlines():
    assert 'Failed to compile Material' not in line and 'Fatal error:' not in line, line
print(json.dumps({'platform': 'cooked UE Android application', 'renderer': a.renderer, **result}, ensure_ascii=False), flush=True)
