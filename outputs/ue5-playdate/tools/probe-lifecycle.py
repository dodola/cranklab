"""Windows C ABI suspension, PCM, callback, input and save/restart verification."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import time

p=argparse.ArgumentParser()
p.add_argument('--library',required=True)
p.add_argument('--work',required=True)
p.add_argument('--output',required=True)
a=p.parse_args()
root=Path(a.work).resolve(); root.mkdir(parents=True,exist_ok=True)
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
fixture=root/'lifecycle.pdx'; fixture.mkdir(exist_ok=True)
tools=Path(__file__).resolve().parent
shutil.copyfile(tools/'fixtures/lifecycle.lua',fixture/'main.lua')
shutil.copyfile(tools.parent/'Unreal/Content/Games/fixture.pdx/tone.wav',fixture/'tone.wav')
(fixture/'pdxinfo').write_text('name=Lifecycle fixture\nbundleID=com.cranklab.lifecycle\n')
save=root/'save'; save.mkdir(exist_ok=True)
library=Path(a.library).resolve()
os.environ['SDL_AUDIODRIVER']='dummy'
with os.add_dll_directory(str(library.parent)):
    core=ctypes.CDLL(str(library))
core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
core.crank_set_suspended.argtypes=[ctypes.c_uint32]
core.crank_set_buttons.argtypes=[ctypes.c_uint32]
core.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_copy_frame.restype=ctypes.c_uint32
core.crank_copy_audio.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
core.crank_copy_audio.restype=ctypes.c_uint32
core.crank_add_crank.argtypes=[ctypes.c_float,ctypes.c_int]
core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))()
pcm=(ctypes.c_int16*8192)()
statefile=save/'com.cranklab.lifecycle/lifecycle.txt'
eventfile=statefile.with_name('events.txt')

def error():
    text=ctypes.create_string_buffer(2048); core.crank_last_error(text,len(text))
    return text.value.decode(errors='replace')

def wait_for(test,seconds=8):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        assert core.crank_status()!=3,error()
        try:
            value=test()
            if value: return value
        except (FileNotFoundError,ValueError,IndexError): pass
        time.sleep(.01)
    raise AssertionError('Timed out waiting for runtime transition')

def snapshot():
    columns=statefile.read_text().strip().split(',')
    assert len(columns)==8,columns
    return dict(event=columns[0],frames=int(columns[1]),buttons=int(columns[2]),
                crank=float(columns[3]),milliseconds=int(columns[4]),elapsed=float(columns[5]),
                sampleOffset=float(columns[6]),previousSave=int(columns[7]))

def serial(): return core.crank_copy_frame(pixels,len(pixels))
def audio(): return core.crank_copy_audio(pcm,len(pcm))

assert core.crank_start(str(fixture).encode(),str(save).encode())==1
try:
    wait_for(lambda: serial()>12)
    core.crank_set_buttons(32); core.crank_add_crank(35,0)
    wait_for(lambda: snapshot()['buttons']==32 and abs(snapshot()['crank']-35)<.1)
    cycles=[]
    for reasons in (1,3,2):
        core.crank_set_suspended(reasons)
        wait_for(lambda: core.crank_status()==4)
        before=snapshot(); frozen=serial()
        core.crank_set_suspended(reasons)  # no duplicate callbacks
        core.crank_set_buttons(32); core.crank_add_crank(180,0)
        time.sleep(.65)
        after=snapshot()
        assert after==before and serial()==frozen,(before,after)
        assert audio()==0,'PCM continued while suspended'
        core.crank_set_suspended(0)
        wait_for(lambda: serial()>frozen+3)
        resumed=snapshot()
        assert resumed['buttons']==0 and abs(resumed['crank']-35)<.1,resumed
        assert 0<=resumed['milliseconds']-before['milliseconds']<400,resumed
        assert resumed['sampleOffset']-before['sampleOffset']<.4,resumed
        count=wait_for(audio)
        peak=max(abs(int(pcm[i])) for i in range(count))
        assert peak>0,'PCM did not resume'
        cycles.append(dict(reasons=reasons,frozenSerial=frozen,paused=before,resumed=resumed,pcmSamples=count,pcmPeak=peak))
    # Lock while already paused: adds lock once without a second pause callback.
    core.crank_set_suspended(1); wait_for(lambda: core.crank_status()==4)
    core.crank_set_suspended(3); wait_for(lambda: snapshot()['event']=='lock')
    core.crank_set_suspended(1); wait_for(lambda: snapshot()['event']=='unlock')
    stopped_serial=serial(); stop_started=time.monotonic(); core.crank_stop()
    stop_duration=time.monotonic()-stop_started
    assert stop_duration<2 and core.crank_status()==0,stop_duration
    terminated=snapshot(); assert terminated['event']=='terminate',terminated
    saved=int(statefile.with_name('checkpoint.txt').read_text())
    events=[line.split(',')[0] for line in eventfile.read_text().splitlines()]
    expected=['boot','a-down','a-up','pause','resume','pause','lock','unlock','resume',
              'pause','lock','unlock','resume','pause','lock','unlock','terminate']
    assert events==expected,events
    assert core.crank_start(str(fixture).encode(),str(save).encode())==1
    wait_for(lambda: serial()>3)
    restarted=snapshot()
    assert restarted['previousSave']==saved and restarted['crank']==0 and restarted['buttons']==0,restarted
    core.crank_stop()
    waiting=root/'waiting.pdx'; waiting.mkdir(exist_ok=True)
    (waiting/'pdxinfo').write_text('name=Wait fixture\nbundleID=com.cranklab.wait\n')
    (waiting/'main.lua').write_text('''
local phase=0
function playdate.update()
 phase=phase+1
 local name=phase==1 and "wait-start.txt" or "wait-end.txt"
 local f=assert(playdate.file.open(name,playdate.file.kFileWrite))
 f:write(tostring(playdate.getCurrentTimeMilliseconds())); f:close()
 if phase==1 then playdate.wait(600) end
end
''')
    wait_save=root/'wait-save'; wait_save.mkdir(exist_ok=True)
    assert core.crank_start(str(waiting).encode(),str(wait_save).encode())==1
    startfile=wait_save/'com.cranklab.wait/wait-start.txt'
    endfile=startfile.with_name('wait-end.txt')
    wait_for(lambda: startfile.exists())
    core.crank_set_suspended(3); wait_for(lambda: core.crank_status()==4)
    time.sleep(.7)
    assert not endfile.exists(),'wait finished in the background'
    resumed_at=time.monotonic(); core.crank_set_suspended(0)
    wait_for(lambda: endfile.exists())
    remaining_wait=time.monotonic()-resumed_at
    assert remaining_wait>.35,'background time consumed the pending wait'
    core.crank_stop()
    failing=root/'failing.pdx'; failing.mkdir(exist_ok=True)
    (failing/'pdxinfo').write_text('name=Failing callback fixture\n')
    (failing/'main.lua').write_text('playdate.update=function() end\nplaydate.gameWillPause=function() error("pause-callback-fixture") end\n')
    assert core.crank_start(str(failing).encode(),str(save).encode())==1
    wait_for(lambda: serial()>3)
    core.crank_set_suspended(1)
    deadline=time.monotonic()+5
    while core.crank_status()!=3 and time.monotonic()<deadline: time.sleep(.01)
    callback_error=error()
    assert core.crank_status()==3 and 'pause-callback-fixture' in callback_error,callback_error
    core.crank_stop()
    result=dict(passed=True,scope='Windows C ABI; application OS lifecycle verified separately',
                cycles=cycles,events=events,stopWhileSuspendedSeconds=stop_duration,
                checkpoint=saved,restarted=restarted,waitRemainingAfterResumeSeconds=remaining_wait,
                callbackFailurePropagated=True,intentionalCallbackError=callback_error)
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    shutil.copyfile(eventfile,out/'events.txt')
    print(json.dumps(result,indent=2),flush=True)
finally:
    core.crank_stop()
