"""Exercise SDK menu state and callback ordering through the real C bridge."""
import argparse, ctypes, json, os, shutil, time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--library',required=True); p.add_argument('--work',required=True); p.add_argument('--output',required=True)
a=p.parse_args(); root=Path(a.work).resolve(); root.mkdir(parents=True,exist_ok=True)
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
fixture=root/'sdk-menu.pdx'; fixture.mkdir(exist_ok=True)
shutil.copyfile(Path(__file__).parent/'fixtures/sdk-menu.lua',fixture/'main.lua')
(fixture/'pdxinfo').write_text('name=SDK system menu fixture\nbundleID=com.cranklab.sdkmenu\n')
save=root/'save'; save.mkdir(exist_ok=True); log=save/'com.cranklab.sdkmenu/events.txt'
library=Path(a.library).resolve(); os.environ['SDL_AUDIODRIVER']='dummy'
with os.add_dll_directory(str(library.parent)): core=ctypes.CDLL(str(library))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
core.crank_set_suspended.argtypes=[ctypes.c_uint32]
core.crank_set_menu_open.argtypes=[ctypes.c_int]
core.crank_menu_select.argtypes=[ctypes.c_uint32,ctypes.c_int]
core.crank_copy_menu.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_copy_menu.restype=ctypes.c_uint32
core.crank_copy_menu_image.argtypes=[ctypes.c_void_p,ctypes.c_uint32]; core.crank_copy_menu_image.restype=ctypes.c_uint32
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))()
def error():
    buf=ctypes.create_string_buffer(2048); core.crank_last_error(buf,len(buf)); return buf.value.decode()
def wait(test,timeout=6):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        assert core.crank_status()!=3,error()
        try:
            value=test()
            if value: return value
        except (FileNotFoundError,KeyError,IndexError,json.JSONDecodeError): pass
        time.sleep(.01)
    raise AssertionError('Menu bridge timed out')
def snapshot():
    for _ in range(4):
        needed=core.crank_copy_menu(None,0); buf=ctypes.create_string_buffer(max(needed,2))
        if core.crank_copy_menu(buf,len(buf))<=len(buf) and buf.value: return json.loads(buf.value)
    raise AssertionError('Unstable snapshot')
def events(): return log.read_text().splitlines()
def frame(): return core.crank_copy_frame(pixels,len(pixels))
def open_menu():
    core.crank_set_suspended(1); core.crank_set_menu_open(1); wait(lambda:core.crank_status()==4)
def close_menu():
    core.crank_set_menu_open(0); core.crank_set_suspended(0); wait(lambda:core.crank_status()==2)
assert core.crank_start(str(fixture).encode(),str(save).encode())==1
try:
    wait(lambda:frame()>3); initial=snapshot(); ids=[x['id'] for x in initial['items']]
    assert initial['items'][0]['title']=='重置' and initial['items'][2]['value']==2,initial
    assert initial['hasImage'] and initial['imageOffset']==25,initial
    image_serial=core.crank_copy_menu_image(pixels,len(pixels)); assert image_serial>0
    assert list(pixels[:4])==[0,0,0,255] and list(pixels[200*4:200*4+4])==[255,255,255,255]
    tiny=ctypes.create_string_buffer(2); required=core.crank_copy_menu(tiny,len(tiny))
    assert required>len(tiny) and tiny.value==b''
    assert core.crank_menu_select(ids[0],0)==0,'Closed menu accepted input'
    open_menu(); frozen=frame()
    assert core.crank_menu_select(ids[1],1)==1 and core.crank_menu_select(ids[2],3)==1
    wait(lambda:snapshot()['items'][1]['value']==1 and snapshot()['items'][2]['value']==3)
    time.sleep(.1); assert events()==['boot','pause'] and frame()==frozen,events()
    close_menu(); wait(lambda:events()[-1]=='resume')
    assert events()==['boot','pause','check:true','option:C','resume'],events()
    open_menu(); assert core.crank_menu_select(ids[0],0)==1
    close_menu(); wait(lambda:len(snapshot()['items'])==1)
    assert events()[-3:]==['pause','action','resume'],events()
    replacement=snapshot()['items'][0]; assert replacement['title']=='Replacement'
    open_menu(); core.crank_menu_select(ids[0],0); close_menu()
    assert events()[-2:]==['pause','resume'],'Removed item callback ran'
    before_restart=list(events()); core.crank_stop()
    assert events()[-1]=='terminate'
    assert core.crank_start(str(fixture).encode(),str(save).encode())==1
    wait(lambda:frame()>3); assert all(x['id'] not in ids for x in snapshot()['items'])
    open_menu(); core.crank_menu_select(ids[0],0); close_menu()
    assert events()==['boot','pause','resume'],'Stale session ID executed a callback'
    current=snapshot()['items']; open_menu()
    assert core.crank_menu_select(current[1]['id'],1) and core.crank_menu_select(current[2]['id'],1)
    core.crank_stop()
    assert events()[-4:]==['pause','check:true','option:A','terminate'],events()
    failing=root/'failure.pdx'; failing.mkdir(exist_ok=True)
    (failing/'pdxinfo').write_text('name=Menu callback failure\n')
    (failing/'main.lua').write_text('playdate.getSystemMenu():addMenuItem("Fail",function() error("menu-callback-fixture") end)\nplaydate.update=function() end\n')
    assert core.crank_start(str(failing).encode(),str(save).encode())==1
    wait(lambda:frame()>3); fail_id=snapshot()['items'][0]['id']; open_menu()
    core.crank_menu_select(fail_id,0); core.crank_set_menu_open(0); core.crank_set_suspended(0)
    deadline=time.monotonic()+6
    while core.crank_status()!=3 and time.monotonic()<deadline: time.sleep(.01)
    assert core.crank_status()==3 and 'menu-callback-fixture' in error(),error()
    result=dict(passed=True,scope='Windows native C ABI, real Lua callbacks; no OS touch simulation',
                initial=initial,events=before_restart,menuImagePixelsVerified=True,
                deferredValueCallbacks=True,removedAndStaleIdsIgnored=True,callbackFailurePropagated=True,queuedValuesFlushedBeforeTerminate=True)
    (out/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
finally: core.crank_stop()
