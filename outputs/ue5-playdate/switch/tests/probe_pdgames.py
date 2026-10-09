"""Exercise the supplied launcher's Lua browse/detail/image/QR routes through its ABI."""
import argparse, ctypes, json, os, shutil, time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--launcher',required=True);p.add_argument('--seed',required=True);p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output).resolve();save=out/'saves';data=save/'com.pdgamez.browser';data.mkdir(parents=True,exist_ok=True)
for f in Path(a.seed).glob('*.json'):shutil.copyfile(f,data/f.name)
os.environ.update(SDL_AUDIODRIVER='dummy',SDL_VIDEODRIVER='dummy',PD_STUB_LOG='1')
fixture=json.loads((Path(a.seed)/'cranklab_launcher_return.json').read_text())['game']
encoded=json.dumps(json.dumps(fixture,ensure_ascii=True))
# A desktop-only test hook navigates the real detail even when the remote list
# is unavailable. This does not replace its screen, bytecode, or QR generator.
os.environ['PD_EVAL']=f'''local fixture=json.decode({encoded})
local once=true;local update=playdate.update
playdate.update=function(...) local value=update(...)
if once and not screenManager.isTransitioning() then once=false;screenManager.switch('gameDetail',{{game=fixture}},'left') end
return value end'''
core=ctypes.CDLL(a.library)
core.crank_start_launcher.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_launcher_reply.argtypes=[ctypes.c_uint32,ctypes.c_char_p]
core.crank_launcher_request.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_copy_menu.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_set_suspended.argtypes=[ctypes.c_uint32];core.crank_set_menu_open.argtypes=[ctypes.c_int]
core.crank_menu_select.argtypes=[ctypes.c_uint32,ctypes.c_int];core.crank_launcher_detail_action.argtypes=[ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))();requests=[];captures=[]
def error():
    buf=ctypes.create_string_buffer(2048);core.crank_last_error(buf,len(buf));return buf.value.decode(errors='replace')
def advance(seconds):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        assert core.crank_status()!=3,error()
        req=ctypes.create_string_buffer(256)
        while core.crank_launcher_request(req,len(req)):
            v=json.loads(req.value);requests.append(v)
            if v['action']=='inspect':assert core.crank_launcher_reply(v['session'],json.dumps(dict(id=v['id'],state='ready',label='Download & Play',message='Publisher package available',busy=False)).encode())
        core.crank_copy_frame(pixels,len(pixels));time.sleep(.02)
def capture(name):
    frame=core.crank_copy_frame(pixels,len(pixels));assert frame>0
    (out/(name+'.bgra')).write_bytes(bytes(pixels));captures.append(dict(name=name,frame=frame,error=error()))
def press(mask):core.crank_set_buttons(mask);advance(.15);core.crank_set_buttons(0);advance(.4)
assert core.crank_start_launcher(a.launcher.encode(),str(save).encode())
try:
    # Seed restores a public game detail. The real network list may be offline.
    for i in range(40):
        advance(1)
        if any(v['action']=='inspect' for v in requests):break
    assert any(v['action']=='inspect' for v in requests),'PDGames never reached detail'
    advance(2);capture('detail')
    assert core.crank_launcher_activate()==1;advance(.5)
    assert any(v['action']=='play' for v in requests),'Download button did not request play'
    core.crank_launcher_detail_action(3);advance(1);capture('images');press(16)
    core.crank_set_suspended(1);core.crank_set_menu_open(1);advance(.3)
    buf=ctypes.create_string_buffer(16384);core.crank_copy_menu(buf,len(buf));menu=json.loads(buf.value)
    qr=next(x for x in menu['items'] if 'qr' in x['title'].lower())
    assert core.crank_menu_select(qr['id'],0)==1
    core.crank_set_menu_open(0);core.crank_set_suspended(0);advance(8);capture('qr')
    press(16);advance(1);capture('back')
    result=dict(passed=True,luaOnly=True,requests=requests,menu=menu,captures=captures)
    (out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
finally:core.crank_stop()
