import argparse,ctypes,json,os,time,array
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser(); p.add_argument('--library',required=True); p.add_argument('--game',required=True); p.add_argument('--output',required=True); a=p.parse_args()
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
lib=Path(a.library).resolve()
os.environ.update(SDL_AUDIODRIVER='dummy',PD_STUB_LOG='1')
with os.add_dll_directory(str(lib.parent)): core=ctypes.CDLL(str(lib))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]; core.crank_set_buttons.argtypes=[ctypes.c_uint32]
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_add_crank.argtypes=[ctypes.c_float,ctypes.c_int]; core.crank_copy_audio.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))(); audio=(ctypes.c_int16*8192)(); captures=[]; samples=0; peak=0
def error():
    buf=ctypes.create_string_buffer(2048); core.crank_last_error(buf,len(buf)); return buf.value.decode(errors='replace')
def advance(seconds):
    global samples,peak
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        assert core.crank_status()!=3,error(); core.crank_copy_frame(pixels,len(pixels))
        count=core.crank_copy_audio(audio,len(audio)); samples+=count
        peak=max(peak,max((abs(x) for x in audio[:count]),default=0)); time.sleep(.02)
def press(mask): core.crank_set_buttons(mask); advance(.13); core.crank_set_buttons(0); advance(.35)
def capture(name):
    frame=core.crank_copy_frame(pixels,len(pixels)); Image.frombytes('RGBA',(400,240),bytes(pixels),'raw','BGRA').save(out/(name+'.png'))
    captures.append(dict(name=name,frame=frame,error=error()))
save=out/'saves';save.mkdir(exist_ok=True)
game=Path(a.game).resolve()
assert core.crank_start(str(game).encode(),str(save).encode())
try:
    advance(2);capture('library');press(32);advance(.5);capture('menu-play')
    press(8);capture('menu-edit');press(8);capture('menu-copy');press(4);capture('menu-edit-again')
    press(32);advance(.5);capture('editor');press(16);advance(.5);capture('back')
    for _ in range(90): core.crank_add_crank(5,0);advance(.025)
    capture('crank');result=dict(noRuntimeError=not error(),captures=captures,audioSamples=samples,audioPeak=peak,scope='Visual menu selection and resource/audio review pending; scratch saves only')
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
finally:core.crank_stop()

