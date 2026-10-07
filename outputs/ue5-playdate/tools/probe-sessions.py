"""Verify multiple games and a stopped yielding coroutine in the same library."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import time

p=argparse.ArgumentParser()
p.add_argument('--library',required=True)
p.add_argument('--fixture',required=True)
p.add_argument('--game',action='append',default=[])
p.add_argument('--output',required=True)
a=p.parse_args()
root=Path(a.output).resolve(); root.mkdir(parents=True,exist_ok=True)
library=Path(a.library).resolve()
os.environ['SDL_AUDIODRIVER']='dummy'
with os.add_dll_directory(str(library.parent)):
    core=ctypes.CDLL(str(library))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_copy_frame.restype=ctypes.c_uint32
core.crank_copy_audio.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_add_crank.argtypes=[ctypes.c_float,ctypes.c_int]
core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))()
samples=(ctypes.c_int16*8192)()
yielding=root/'yielding.pdx'; yielding.mkdir(exist_ok=True)
(yielding/'pdxinfo').write_text('name=Session cleanup fixture\nbundleID=com.cranklab.sessiontest\n')
(yielding/'main.lua').write_text('''
assert(playdate.getCrankPosition()==0)
assert(playdate.getCrankTicks(12)==0)
playdate.update=function()
 playdate.graphics.clear(playdate.graphics.kColorWhite)
 while true do coroutine.yield() end
end
''')
games=[Path(a.fixture).resolve()]+[Path(g).resolve() for g in a.game]
games += [yielding,Path(a.fixture).resolve(),yielding,Path(a.fixture).resolve()]
results=[]
for i,game in enumerate(games):
    save=root/f'save-{i}'; save.mkdir(exist_ok=True)
    assert core.crank_start(str(game).encode(),str(save).encode())==1, game
    assert core.crank_start(str(game).encode(),str(save).encode())==0, 'concurrent start accepted'
    deadline=time.monotonic()+15; serial=0
    while time.monotonic()<deadline:
        serial=core.crank_copy_frame(pixels,len(pixels))
        core.crank_copy_audio(samples,len(samples))
        if serial>=20 or core.crank_status()==3: break
        time.sleep(.015)
    error=ctypes.create_string_buffer(2048); core.crank_last_error(error,len(error))
    result=dict(session=i,game=game.name,status=core.crank_status(),frames=serial,error=error.value.decode(errors='replace'))
    results.append(result)
    core.crank_set_buttons(32); core.crank_add_crank(120,0)
    time.sleep(.05)
    core.crank_stop(); core.crank_stop()
    core.crank_last_error(error,len(error))
    result['stopError']=error.value.decode(errors='replace')
    assert result['status']==2 and serial>=20 and not error.value, result
    assert core.crank_status()==0
    assert not result['stopError'],result
(root/'results.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
