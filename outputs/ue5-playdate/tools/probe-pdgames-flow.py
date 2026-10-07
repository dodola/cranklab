"""Browse/detail/QR through the provided PDGames package; no account mutation."""
import argparse,ctypes,json,os,time
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser(); p.add_argument('--library',required=True); p.add_argument('--game',required=True); p.add_argument('--output',required=True)
a=p.parse_args(); out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True); save=out/'saves'; save.mkdir(exist_ok=True)
lib=Path(a.library).resolve(); os.environ.update(SDL_AUDIODRIVER='dummy',PD_STUB_LOG='1')
with os.add_dll_directory(str(lib.parent)): core=ctypes.CDLL(str(lib))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]; core.crank_set_buttons.argtypes=[ctypes.c_uint32]
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_copy_menu.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_copy_menu.restype=ctypes.c_uint32
core.crank_set_suspended.argtypes=[ctypes.c_uint32]; core.crank_set_menu_open.argtypes=[ctypes.c_int]; core.crank_menu_select.argtypes=[ctypes.c_uint32,ctypes.c_int]
pixels=(ctypes.c_uint8*(400*240*4))(); captures=[]
def error():
    buf=ctypes.create_string_buffer(2048); core.crank_last_error(buf,len(buf)); return buf.value.decode(errors='replace')
def advance(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        assert core.crank_status()!=3,error(); core.crank_copy_frame(pixels,len(pixels)); time.sleep(.02)
def press(mask): core.crank_set_buttons(mask); advance(.13); core.crank_set_buttons(0); advance(.2)
def capture(name):
    frame=core.crank_copy_frame(pixels,len(pixels)); Image.frombytes('RGBA',(400,240),bytes(pixels),'raw','BGRA').save(out/(name+'.png'))
    captures.append(dict(name=name,frame=frame,error=error()))
def menu():
    needed=core.crank_copy_menu(None,0); buf=ctypes.create_string_buffer(max(needed,2)); core.crank_copy_menu(buf,len(buf)); return json.loads(buf.value)
assert core.crank_start(str(Path(a.game).resolve()).encode(),str(save).encode())
try:
    advance(9); capture('list')
    press(32); advance(6); capture('detail')
    press(32); advance(3); capture('images')
    press(16); advance(.5)
    core.crank_set_suspended(1); core.crank_set_menu_open(1); advance(.2)
    sdk_menu=menu(); qr=next(x for x in sdk_menu['items'] if 'qr' in x['title'].lower())
    assert core.crank_menu_select(qr['id'],0)==1
    core.crank_set_menu_open(0); core.crank_set_suspended(0); advance(8); capture('qr')
    press(16); advance(1); capture('back')
    result=dict(noRuntimeError=True,sdkMenu=sdk_menu,captures=captures,scope='Visual evidence still requires review; no login, purchase or wishlist mutation')
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8'); print(json.dumps(result,indent=2))
finally: core.crank_stop()
