"""Run an instrumentation APK on Android, collect native frames, PCM and network evidence."""
import argparse, json, subprocess, wave, array
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser()
p.add_argument('--adb',required=True); p.add_argument('--device',required=True)
p.add_argument('--apk',required=True); p.add_argument('--output',required=True)
p.add_argument('--games',nargs='+',choices=('fixture','music-box','mahjong','pdgames','native-hello','native-life','native-sprites','native-abi','native-mixed','native-audio'),default=('fixture','music-box','mahjong','pdgames'))
a=p.parse_args(); out=Path(a.output).resolve(); out.mkdir(parents=True,exist_ok=True)
base=[a.adb,'-s',a.device]
def adb(*args,timeout=45):
    r=subprocess.run(base+list(args),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=timeout)
    if r.returncode: raise RuntimeError(r.stderr.decode(errors='replace'))
    return r.stdout
adb('install','-r',str(Path(a.apk).resolve()),timeout=90)
results=[]
for key in a.games:
    root=out/key; root.mkdir(exist_ok=True)
    adb('shell','am','force-stop','com.cranklab.probe')
    response=adb('shell','am','instrument','-w','-e','game',key,'com.cranklab.probe/com.cranklab.probe.Probe').decode(errors='replace')
    (root/'instrumentation.txt').write_text(response)
    game_result={'game':key,'passed':'nativeExitCode=0' in response and 'INSTRUMENTATION_CODE: -1' in response}
    for filename in ('frame.log','frame.bgra','frame.before.bgra','frame.pcm','frame.native.json'):
        try: data=adb('exec-out','run-as','com.cranklab.probe','cat',f'files/verification/{key}/{filename}')
        except RuntimeError: continue
        (root/filename).write_bytes(data)
        if filename.endswith('.bgra') and len(data)==400*240*4:
            Image.frombytes('RGBA',(400,240),data,'raw','BGRA').resize((800,480),Image.Resampling.NEAREST).save(root/filename.replace('.bgra','.png'))
        if filename=='frame.pcm':
            pcm=array.array('h',data)
            game_result['audioSamples']=len(pcm); game_result['audioPeak']=max((abs(s) for s in pcm),default=0)
            with wave.open(str(root/'audio.wav'),'wb') as wav:
                wav.setparams((2,2,44100,0,'NONE','not compressed')); wav.writeframes(data)
    if (root/'frame.log').exists():
        log=(root/'frame.log').read_text(errors='replace')
        game_result['http200Responses']=sum('[http response]' in line and 'status=200' in line for line in log.splitlines())
    if key=='fixture': game_result['passed'] &= game_result.get('audioPeak',0)>100
    if key.startswith('native-'):
        info=json.loads((root/'frame.native.json').read_text())
        game_result['nativeInfo']=info
        game_result['passed'] &= info.get('armCalls',0)>10 and info.get('apiCalls',0)>10
        if key=='native-audio':game_result['passed'] &= game_result.get('audioPeak',0)>100
    if key=='pdgames':
        game_result['cdn200Responses']=sum('[http response] cdn.pdgamez.com status=200' in line for line in log.splitlines())
        game_result['passed'] &= game_result.get('http200Responses',0)>0 and game_result['cdn200Responses']>0
    (root/'result.json').write_text(json.dumps(game_result,indent=2))
    results.append(game_result); print(json.dumps(game_result),flush=True)
(out/'results.json').write_text(json.dumps(results,indent=2))
assert all(r['passed'] for r in results),results
