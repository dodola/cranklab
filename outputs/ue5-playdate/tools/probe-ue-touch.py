"""Exercise real Android multipointer events in the owned UE application."""
import argparse
import json
from pathlib import Path
import queue
import re
import shlex
import struct
import subprocess
import threading
import time
import uuid

p=argparse.ArgumentParser()
p.add_argument('--adb',required=True)
p.add_argument('--device',required=True)
p.add_argument('--dex',required=True)
p.add_argument('--output',required=True)
a=p.parse_args()
package='com.cranklab.emulator'
out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
token=uuid.uuid4().hex
remote=f'/data/user/0/{package}/files/CrankLab/input-touch-'+token
dex='/data/local/tmp/CrankLabTouch-'+token+'.dex'
base=[a.adb,'-s',a.device]
def adb(*args,check=True):
    result=subprocess.run(base+list(args),capture_output=True,timeout=30)
    if check: assert result.returncode==0,(args,result.stderr.decode(errors='replace'))
    return result.stdout
def snapshot():
    data=adb('exec-out','run-as',package,'cat',remote+'/input.json',check=False)
    return json.loads(data.decode('utf-16' if data[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig'))
def wait(test,seconds=15):
    end=time.monotonic()+seconds
    last=None
    while time.monotonic()<end:
        try:
            last=snapshot()
            if test(last): return last
        except (ValueError,UnicodeDecodeError): pass
        time.sleep(.1)
    raise AssertionError(('No matching input state',last))

adb('shell','am','force-stop',package)
command='am start -W -n '+package+'/com.epicgames.unreal.GameActivity --es cmdline '+shlex.quote('-CrankInputProbe='+remote)
(out/'launch.txt').write_bytes(adb('shell',command))
first=wait(lambda s:s['frames']>20 and s['buttons']==0)
picture=adb('exec-out','screencap','-p')
(out/'before.png').write_bytes(picture)
width,height=struct.unpack('>II',picture[16:24])
assert (width>height)==(first['width']>first['height']),'Display orientation changed during setup'
scale=(width/first['width'],height/first['height'])
def point(name): return [first['controls'][name][i]*scale[i] for i in range(2)]
uid_response=adb('shell','cmd','package','list','packages','-U',package).decode()
uid=int(re.search(r'uid:(\d+)',uid_response)[1])
adb('push',str(Path(a.dex).resolve()),dex)
stderr=(out/'harness-stderr.txt').open('w',encoding='utf-8')
process=subprocess.Popen(base+['shell','CLASSPATH='+dex+' app_process /system/bin com.cranklab.tools.TouchStream '+str(uid)],
                         stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=stderr,text=True,encoding='utf-8')
lines=queue.Queue()
def receive():
    for line in process.stdout: lines.put(line.strip())
    lines.put('EOF')
threading.Thread(target=receive,daemon=True).start()
def response(expected):
    actual=lines.get(timeout=15)
    assert actual==expected,(expected,actual)
def touch(command):
    process.stdin.write(command+'\n'); process.stdin.flush(); response('ok')
states=[]
def record(label,test):
    state=wait(test); states.append(dict(label=label,**state)); return state
try:
    response('ready')
    A,B=point('A'),point('B')
    touch(f'down 3 {A[0]} {A[1]}')
    record('A alone',lambda s:s['buttons']==32)
    touch(f'down 7 {B[0]} {B[1]}')
    record('A and B',lambda s:s['buttons']==48)
    # Release the first pointer, leaving a non-dense remaining Android ID.
    touch('up 3')
    record('B remains after releasing A',lambda s:s['buttons']==16)
    touch('up 7')
    record('Both released',lambda s:s['buttons']==0)
    touch(f'down 7 {B[0]} {B[1]}')
    record('B alone',lambda s:s['buttons']==16)
    touch(f'down 3 {A[0]} {A[1]}')
    record('B and A',lambda s:s['buttons']==48)
    touch('up 3')
    record('B remains after releasing second pointer A',lambda s:s['buttons']==16)
    touch('cancel')
    record('Cancellation releases all',lambda s:s['buttons']==0)
    result=dict(passed=True,device=a.device,targetUid=uid,states=states,
                scope='Actual Android multipointer down/up and cancel, both A/B orders, non-dense pointer IDs. Uses platform input injection under the existing ADB shell permission; no app input-helper injection or permission changes.')
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result),flush=True)
finally:
    try:
        if process.poll() is None:
            process.stdin.write('quit\n'); process.stdin.flush(); process.stdin.close(); process.wait(timeout=10)
    finally:
        if process.poll() is None: process.terminate()
        stderr.close()
        adb('shell','am','force-stop',package)
        adb('shell','run-as',package,'rm','-rf',remote)
        adb('shell','rm','-f',dex)
