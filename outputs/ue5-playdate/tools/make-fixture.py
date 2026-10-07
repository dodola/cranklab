"""Generate an owned Lua fixture and a 440 Hz sample for repeatable bridge checks."""
import argparse, math, struct, wave
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('--output',required=True); a=p.parse_args()
root=Path(a.output).resolve(); root.mkdir(parents=True,exist_ok=True)
(root/'pdxinfo').write_text('name=Crank input and file verification\nbundleID=local.crank.fixture\nversion=1.0\n')
(root/'main.lua').write_bytes((Path(__file__).parent/'fixtures/basic.lua').read_bytes())
with wave.open(str(root/'tone.wav'),'wb') as wav:
    wav.setparams((1,2,44100,0,'NONE','not compressed'))
    wav.writeframes(b''.join(struct.pack('<h',int(10000*math.sin(2*math.pi*440*i/44100))) for i in range(44100)))
print(root)
