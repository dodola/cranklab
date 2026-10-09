"""Keep the normal UE launcher running and check audio/frame progress across Home events."""
import argparse,json,shlex,subprocess,time,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--serial',required=True)
p.add_argument('--output',required=True);p.add_argument('--duration',type=int,default=90)
a=p.parse_args();assert a.duration>=60
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
pkg='com.cranklab.emulator';base=[a.adb,'-s',a.serial]
remote=f'/data/user/0/{pkg}/files/CrankLab/audio-stability-'+uuid.uuid4().hex
def adb(*args,check=True):return subprocess.run(base+list(args),capture_output=True,check=check,timeout=30).stdout
def state():
    raw=adb('exec-out','run-as',pkg,'cat',remote+'/input.json',check=False)
    return json.loads(raw.decode('utf-16' if raw[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))
def launch(initial=False):
    cmd='am start -W -n '+pkg+'/com.epicgames.unreal.GameActivity'
    if initial:cmd+=' --es cmdline '+shlex.quote('-vulkan -CrankInputProbe='+remote)
    assert b'Status: ok' in adb('shell',cmd)
adb('shell','am','force-stop',pkg);launch(True)
started=time.monotonic();pid=None;first=None;last=None;transitions=0;records=[]
try:
    while time.monotonic()-started<a.duration:
        current=adb('shell','pidof',pkg,check=False).strip()
        assert current and (pid is None or current==pid), 'UE process exited or restarted'
        pid=current
        try:last=state()
        except (ValueError,UnicodeDecodeError):time.sleep(.5);continue
        if first is None:first=last
        elapsed=time.monotonic()-started
        if transitions<2 and elapsed>(transitions+1)*a.duration/3:
            before=last;adb('shell','input','keyevent','KEYCODE_HOME');time.sleep(1)
            paused=state();time.sleep(.5);frozen=state()
            # The world may stop ticking before it can publish the paused flag.
            # A stable frame/audio counter proves it actually stopped updating.
            assert frozen['frames']==paused['frames'] and frozen['audioSamples']==paused['audioSamples'],(paused,frozen)
            launch();deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                resumed=state()
                if not resumed['suspended'] and resumed['frames']>paused['frames']+3:break
                time.sleep(.1)
            else:raise AssertionError('Frame updates did not resume')
            records.append(dict(before=before,paused=paused,resumed=resumed));transitions+=1
        time.sleep(.5)
    assert first and last and last['frames']>first['frames']+300 and last['audioSamples']>first['audioSamples']+44100,(first,last)
    assert transitions==2
    (out/'device.png').write_bytes(adb('exec-out','screencap','-p'))
    result=dict(passed=True,durationSeconds=time.monotonic()-started,processUnchanged=True,
                frames=last['frames'],audioSamples=last['audioSamples'],transitions=records)
    (out/'results.json').write_text(json.dumps(result,indent=2),'utf-8');print(json.dumps(result),flush=True)
finally:
    (out/'logcat.txt').write_bytes(adb('logcat','-d','-t','3000',check=False))
    adb('shell','am','force-stop',pkg)
