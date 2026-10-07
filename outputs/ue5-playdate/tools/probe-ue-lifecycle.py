"""Exercise actual Android Home/foreground events in the cooked UE app.

Uses an owned Lua fixture staged via adb, independently of the SAF importer.
Game-thread records prove updates, input, clock and saves around OS transitions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import struct
import time
import uuid

p=argparse.ArgumentParser()
p.add_argument('--adb',required=True)
p.add_argument('--device',required=True)
p.add_argument('--apk')
p.add_argument('--work',required=True)
p.add_argument('--output',required=True)
p.add_argument('--package',default='com.cranklab.emulator')
p.add_argument('--software-rendering',action='store_true')
p.add_argument('--skip-keyboard',action='store_true',help='Isolate OS lifecycle; leaves Android key dispatch unverified')
p.add_argument('--touch-input',action='store_true',help='Verify real OS touch and a held A button across Home/foreground')
a=p.parse_args()
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
work=Path(a.work).resolve(); work.mkdir(parents=True,exist_ok=True)
token=uuid.uuid4().hex
bundle='com.cranklab.lifecycle.'+token
fixture=work/(token+'.pdx'); fixture.mkdir()
tools=Path(__file__).resolve().parent
shutil.copyfile(tools/'fixtures/lifecycle.lua',fixture/'main.lua')
shutil.copyfile(tools.parent/'Unreal/Content/Games/fixture.pdx/tone.wav',fixture/'tone.wav')
(fixture/'pdxinfo').write_text('name=Lifecycle fixture\nbundleID='+bundle+'\n',encoding='utf-8')
base=[a.adb,'-s',a.device]
activity=a.package+'/com.epicgames.unreal.GameActivity'
app_root=f'/data/user/0/{a.package}/files/CrankLab'
game=app_root+'/Diagnostics/lifecycle-'+token+'.pdx'
save=app_root+'/GameSaves/'+hashlib.md5(bundle.encode()).hexdigest()+'/'+bundle
input_probe=app_root+'/input-'+token

def adb(*args,check=True,timeout=30):
    result=subprocess.run(base+list(args),capture_output=True,timeout=timeout)
    if check and result.returncode: raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
    return result

def read(name):
    return adb('exec-out','run-as',a.package,'cat',save+'/'+name,check=False).stdout.decode(errors='replace')

def snapshot():
    columns=read('lifecycle.txt').strip().split(',')
    if len(columns)!=8: raise ValueError('Snapshot incomplete')
    return dict(event=columns[0],frames=int(columns[1]),buttons=int(columns[2]),crank=float(columns[3]),
                milliseconds=int(columns[4]),elapsed=float(columns[5]),sampleOffset=float(columns[6]),previousSave=int(columns[7]))

def events():
    return [line.split(',')[0] for line in read('events.txt').splitlines() if len(line.split(','))==8]

def input_snapshot():
    data=adb('exec-out','run-as',a.package,'cat',input_probe+'/input.json',check=False).stdout
    return json.loads(data.decode('utf-16' if data[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))

def touch(action,point):
    adb('shell','input','touchscreen','motionevent',action,str(round(point[0])),str(round(point[1])))

def wait(test,seconds=90):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        try:
            result=test()
            if result: return result
        except (ValueError,IndexError): pass
        assert adb('shell','pidof',a.package,check=False).stdout.strip(),'UE process exited'
        time.sleep(.1)
    raise AssertionError('Timed out waiting for game-thread lifecycle evidence')

def launch(initial=False):
    command='am start -W -n '+shlex.quote(activity)
    if initial:
        flags='-vulkan -CrankGame="'+game+'"'
        if a.touch_input: flags+=' -CrankInputProbe="'+input_probe+'"'
        if a.software_rendering: flags+=' -AllowSoftwareRendering'
        command+=' --es cmdline '+shlex.quote(flags)
    result=adb('shell',command)
    assert b'Status: ok' in result.stdout,result.stdout.decode(errors='replace')
    return result.stdout.decode(errors='replace')

if a.apk: adb('install','-r',str(Path(a.apk).resolve()),timeout=300)
adb('shell','am','force-stop',a.package)
temporary='/data/local/tmp/CrankLifecycle-'+token
adb('push',str(fixture),temporary)
adb('shell','run-as '+shlex.quote(a.package)+' mkdir -p '+shlex.quote(game))
adb('shell','run-as '+shlex.quote(a.package)+' cp -R '+shlex.quote(temporary+'/.')+' '+shlex.quote(game))
result={}
try:
    result['launch']=launch(True)
    wait(lambda: snapshot()['frames']>15)
    pid=adb('shell','pidof',a.package).stdout.decode().strip()
    # Android key dispatch, rather than calling the pawn's input helpers.
    if not a.skip_keyboard:
        # The software GPU may render below 3 FPS. Keep the OS key down
        # across a rendered tick rather than coalescing its down/up messages.
        adb('shell','input','keyevent','--duration','1500','54')  # Z = A
        wait(lambda: 'a-down' in events() and 'a-up' in events(),seconds=10)
    point=None
    if a.touch_input:
        initial=wait(lambda: input_snapshot())
        (out/'input-initial.json').write_text(json.dumps(initial,indent=2),encoding='utf-8')
        picture=adb('exec-out','screencap','-p').stdout
        (out/'before-touch.png').write_bytes(picture)
        width,height=struct.unpack('>II',picture[16:24])
        # Projected points are in the UE render viewport. Android input uses
        # physical display pixels, which can be larger due to content scaling.
        point=[initial['controls']['A'][0]*width/initial['width'],
               initial['controls']['A'][1]*height/initial['height']]
        touch('DOWN',point); wait(lambda: snapshot()['buttons']==32,seconds=15)
        touch('UP',point); wait(lambda: snapshot()['buttons']==0,seconds=15)
        assert 'a-down' in events() and 'a-up' in events(),events()
    cycles=[]
    for index in range(3):
        if point and index==0:
            touch('DOWN',point); wait(lambda: snapshot()['buttons']==32,seconds=15)
        before=snapshot(); previous_events=events()
        adb('shell','input','keyevent','KEYCODE_HOME')
        wait(lambda: len(events())>len(previous_events) and events()[-1]=='lock',seconds=15)
        locked=snapshot()
        assert locked['event']=='lock' and locked['buttons']==0,locked
        time.sleep(1)
        frozen=snapshot()
        assert frozen==locked,(locked,frozen)
        foreground=launch()
        wait(lambda: snapshot()['event']=='frame' and snapshot()['frames']>locked['frames']+3,seconds=30)
        resumed=snapshot()
        assert resumed['buttons']==0 and abs(resumed['crank']-locked['crank'])<.01,resumed
        assert resumed['milliseconds']-locked['milliseconds']<1000,(locked,resumed)
        assert resumed['sampleOffset']-locked['sampleOffset']<1,(locked,resumed)
        transition=events()[len(previous_events):]
        expected=['a-up','pause','lock','unlock','resume'] if point and index==0 else ['pause','lock','unlock','resume']
        assert transition==expected,transition
        if point and index==0: touch('UP',point)
        assert adb('shell','pidof',a.package).stdout.decode().strip()==pid,'Process restarted on foreground'
        cycles.append(dict(index=index,before=before,locked=locked,resumed=resumed,events=transition,foreground=foreground))
    # Saving must occur at backgrounding, before Android is allowed to kill us.
    old_events=events()
    adb('shell','input','keyevent','KEYCODE_HOME')
    wait(lambda: len(events())>len(old_events) and events()[-1]=='lock',seconds=15)
    checkpoint=int(read('checkpoint.txt').strip())
    assert checkpoint>0
    prior_events=read('events.txt')
    adb('shell','am','force-stop',a.package)
    result['restartLaunch']=launch(True)
    wait(lambda: snapshot()['event']=='frame' and snapshot()['previousSave']==checkpoint)
    restarted=snapshot()
    assert restarted['buttons']==0 and restarted['crank']==0,restarted
    result.update(passed=True,platform='cooked UE Android app',device=a.device,cycles=cycles,
                  androidKeyDownUp=not a.skip_keyboard,androidTouchDownUp=bool(point),
                  heldTouchReleasedOnBackground=bool(point),checkpoint=checkpoint,restarted=restarted,
                  scope='Actual Android Home/foreground callbacks, game-thread freeze, input reset and save after forced process death. PCM freeze/resume verified separately at C ABI; OS speaker playback and SAF excluded.')
    (out/'events-before-restart.txt').write_text(prior_events,encoding='utf-8')
    (out/'events-after-restart.txt').write_text(read('events.txt'),encoding='utf-8')
    (out/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False),flush=True)
finally:
    if a.touch_input:
        try: (out/'input-latest.json').write_text(json.dumps(input_snapshot(),indent=2),encoding='utf-8')
        except (ValueError,UnicodeDecodeError): pass
    log=adb('logcat','-d','-t','3000',check=False).stdout
    (out/'logcat.txt').write_bytes(log)
    (out/'events-latest.txt').write_text(read('events.txt'),encoding='utf-8')
    (out/'lifecycle-latest.txt').write_text(read('lifecycle.txt'),encoding='utf-8')
    adb('shell','am','force-stop',a.package)
    # Only this run's UUID-tagged diagnostic fixture, save and telemetry paths.
    # Imported games and their save directories never appear in this list.
    adb('shell','run-as',a.package,'rm','-rf',game,save.rsplit('/',1)[0],input_probe)
    adb('shell','rm','-rf',temporary)
