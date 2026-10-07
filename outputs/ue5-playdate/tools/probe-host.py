"""Exercise the C ABI used by Unreal: input, frame transfer and clean shutdown."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import time
import wave
import array

p = argparse.ArgumentParser()
p.add_argument('--library', required=True)
p.add_argument('--game', required=True)
p.add_argument('--output', required=True)
p.add_argument('--require-input-change', action='store_true',
               help='Assert visual responses, for a fixture that draws button and crank state.')
p.add_argument('--require-audio', action='store_true')
a = p.parse_args()
root = Path(a.output).resolve()
root.mkdir(parents=True, exist_ok=True)
(root / 'saves').mkdir(exist_ok=True)
os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'
library_path = Path(a.library).resolve()
with os.add_dll_directory(str(library_path.parent)):
    core = ctypes.CDLL(str(library_path))
core.crank_start.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
core.crank_copy_frame.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
core.crank_copy_frame.restype = ctypes.c_uint32
core.crank_add_crank.argtypes = [ctypes.c_float, ctypes.c_int]
core.crank_set_buttons.argtypes = [ctypes.c_uint32]
core.crank_last_error.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
core.crank_copy_audio.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
core.crank_copy_audio.restype = ctypes.c_uint32
audio_buffer = (ctypes.c_int16 * 8192)()
pcm = bytearray()
def drain_audio():
    count = core.crank_copy_audio(audio_buffer, len(audio_buffer))
    assert count % 2 == 0
    pcm.extend(bytes(audio_buffer)[:count * 2])
assert core.crank_start(str(Path(a.game).resolve()).encode(), str(root / 'saves').encode()) == 1
pixels = (ctypes.c_uint8 * (400 * 240 * 4))()
deadline = time.monotonic() + 10
serial = 0
while time.monotonic() < deadline:
    if core.crank_status() == 3:
        break
    serial = core.crank_copy_frame(pixels, len(pixels))
    drain_audio()
    if serial >= 30:
        break
    time.sleep(.02)
first = bytes(pixels)
core.crank_set_buttons(32)
time.sleep(.15)
core.crank_copy_frame(pixels, len(pixels))
held = bytes(pixels)
core.crank_set_buttons(0)
# Stop at a different angle so a complete revolution cannot hide a working input.
for _ in range(20):
    core.crank_add_crank(6.0, 0)
    drain_audio()
    time.sleep(.02)
time.sleep(.10)
serial = core.crank_copy_frame(pixels, len(pixels))
after = bytes(pixels)
state = core.crank_status()
drain_audio()
error = ctypes.create_string_buffer(2048)
core.crank_last_error(error, len(error))
core.crank_stop()
result = {'statusBeforeStop': state, 'serial': serial, 'error': error.value.decode(errors='replace'),
          'frameChangedWhileButtonHeld': first != held,
          'frameChangedAfterCrank': first != after, 'statusAfterStop': core.crank_status(),
          'pixelValues': sorted(set(after[::4]))}
audio_samples = array.array('h', pcm)
result['audioSamples'] = len(audio_samples)
result['audioPeak'] = max((abs(s) for s in audio_samples), default=0)
with wave.open(str(root / 'audio.wav'), 'wb') as output:
    output.setparams((2, 2, 44100, 0, 'NONE', 'not compressed'))
    output.writeframes(pcm)
(root / 'host-results.json').write_text(json.dumps(result, indent=2))
(root / 'frame.bgra').write_bytes(after)
from PIL import Image
Image.frombytes('RGBA', (400, 240), after, 'raw', 'BGRA').resize((800, 480)).save(root / 'frame.png')
print(json.dumps(result, indent=2))
assert state == 2 and serial >= 30 and not error.value, result
assert core.crank_status() == 0, result
if a.require_input_change:
    assert first != held and first != after, result
if a.require_audio:
    assert len(audio_samples) >= 44100 and result['audioPeak'] > 100, result
