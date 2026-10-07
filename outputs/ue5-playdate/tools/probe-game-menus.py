"""Real user PDX packages: bounded C ABI play/pause/menu smoke test."""
import argparse, ctypes, json, os, time
from pathlib import Path
p=argparse.ArgumentParser(); p.add_argument('--library',required=True); p.add_argument('--inventory',required=True); p.add_argument('--output',required=True)
a=p.parse_args(); out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
lib=Path(a.library).resolve(); os.environ['SDL_AUDIODRIVER']='dummy'
with os.add_dll_directory(str(lib.parent)): core=ctypes.CDLL(str(lib))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
core.crank_set_suspended.argtypes=[ctypes.c_uint32]; core.crank_set_menu_open.argtypes=[ctypes.c_int]
core.crank_set_buttons.argtypes=[ctypes.c_uint32]; core.crank_add_crank.argtypes=[ctypes.c_float,ctypes.c_int]
core.crank_copy_menu.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_copy_menu.restype=ctypes.c_uint32
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))(); reports=[]
def snapshot():
    for _ in range(3):
        needed=core.crank_copy_menu(None,0); buf=ctypes.create_string_buffer(max(needed,2))
        if core.crank_copy_menu(buf,len(buf))<=len(buf) and buf.value: return json.loads(buf.value)
    return {}
for game in json.loads(Path(a.inventory).read_text(encoding='utf-8-sig'))['games']:
    folder=out/game['metadata']['bundleID']; folder.mkdir(exist_ok=True); save=folder/'saves'; save.mkdir(exist_ok=True)
    frames=0; menus=[]; buf=ctypes.create_string_buffer(2048)
    try:
        assert core.crank_start(game['root'].encode(),str(save).encode())
        started=time.monotonic(); end=started+8
        while time.monotonic()<end and core.crank_status()!=3:
            elapsed=time.monotonic()-started
            core.crank_set_buttons(32 if 2<elapsed<2.12 else 0)
            core.crank_add_crank(5,0); frames=core.crank_copy_frame(pixels,len(pixels)); time.sleep(.03)
        for _ in range(3):
            if core.crank_status()==3: break
            core.crank_set_suspended(1); core.crank_set_menu_open(1)
            end=time.monotonic()+3
            while core.crank_status() not in (3,4) and time.monotonic()<end: time.sleep(.01)
            menus.append(snapshot()); core.crank_set_menu_open(0); core.crank_set_suspended(0); time.sleep(.3)
        core.crank_last_error(buf,len(buf)); err=buf.value.decode(errors='replace')
        report=dict(game=game['metadata']['name'],frames=frames,menus=menus,error=err,
                    passed=frames>30 and not err and len(menus)==3,
                    scope='Bounded Windows native bridge run and three menu cycles; complete gameplay not established')
        if frames:
            (folder/'frame.pbm').write_bytes(b'P4\n400 240\n'+bytes(sum((int(pixels[(y*400+x+b)*4]<128)<<(7-b) for b in range(8))) for y in range(240) for x in range(0,400,8)))
        reports.append(report)
    finally: core.crank_stop()
(out/'results.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(reports,ensure_ascii=False,indent=2))
