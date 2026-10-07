"""Verify continuous Q/E rotation through actual Android key dispatch."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time
import uuid

p=argparse.ArgumentParser()
p.add_argument('--adb',required=True)
p.add_argument('--device',required=True)
p.add_argument('--output',required=True)
a=p.parse_args()
package='com.cranklab.emulator'
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
remote=f'/data/user/0/{package}/files/CrankLab/input-crank-'+uuid.uuid4().hex
base=[a.adb,'-s',a.device]
def adb(*args):
    return subprocess.check_output(base+list(args),timeout=30)
def snapshot():
    data=adb('exec-out','run-as',package,'cat',remote+'/input.json')
    return json.loads(data.decode('utf-16' if data[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))
adb('shell','am','force-stop',package)
adb('shell','am start -W -n '+package+'/com.epicgames.unreal.GameActivity --es cmdline '+
    shlex.quote('-CrankInputProbe='+remote))
try:
    deadline=time.monotonic()+30
    while time.monotonic()<deadline:
        try:
            initial=snapshot()
            if initial['frames']>20: break
        except (ValueError,subprocess.CalledProcessError): pass
        time.sleep(.2)
    else: raise AssertionError('No live default-game input telemetry')
    records=[]
    for key,label,direction in ((33,'E',1),(45,'Q',-1)):
        before=snapshot()
        adb('shell','input','keyevent','--duration','2000',str(key))
        time.sleep(.7)
        released=snapshot()
        delta=released['crankDegrees']-before['crankDegrees']
        assert delta*direction>400,(label,before,released)
        assert released['frames']>before['frames']+30,(label,before,released)
        time.sleep(.7)
        idle=snapshot()
        assert abs(idle['crankDegrees']-released['crankDegrees'])<1,(label,released,idle)
        records.append(dict(key=label,heldMilliseconds=2000,rotationDegrees=delta,before=before,released=released,idle=idle))
    result=dict(passed=True,device=a.device,records=records,
                scope='Default application startup, continuous crank from Android Q/E key events, release without stuck rotation. External physical keyboard and crank dragging excluded.')
    (out/'device.png').write_bytes(adb('exec-out','screencap','-p'))
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result),flush=True)
finally:
    adb('shell','am','force-stop',package)
    adb('shell','run-as',package,'rm','-rf',remote)
