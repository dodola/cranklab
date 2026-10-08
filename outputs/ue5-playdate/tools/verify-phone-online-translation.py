"""Exercise the actual packaged UE/PDGames translation UI in isolated phone saves."""
import argparse,json,re,subprocess,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--output',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
adb='C:/Users/dodol/AppData/Local/Android/Sdk/platform-tools/adb.exe';pkg='com.cranklab.emulator'
def call(*args):return subprocess.run([adb,'-s',a.serial,*args],capture_output=True,timeout=30,check=True).stdout
policy=call('shell','dumpsys','window','policy').decode('utf-8',errors='replace')
if re.search(r'\bmIsShowing=true\b',policy):raise RuntimeError('Unlock the test device before running the scene checks')
identity=uuid.uuid4().hex[:8];relative=f'files/CrankLab/online-test-{identity}'
library=f'/data/user/0/{pkg}/{relative}'
seed=root/'tools/online-launcher-seed'
destination=relative+'/GameSaves/d00f96e407353d2703fdf487d3993bd8/com.pdgamez.browser'
call('shell','run-as',pkg,'mkdir','-p',destination)
for name in ('game_cache.json','img_index.json','cranklab_launcher_return.json'):
    remote=f'/data/local/tmp/cranklab-online-{identity}-{name}'
    call('push',str(seed/name),remote)
    call('shell','run-as',pkg,'cp',remote,destination+'/'+name)
    call('shell','rm',remote)
results=[]
for name,flags in [('live',[]),('cached-offline',['-CrankTranslationDetailBase=http://127.0.0.1:9','-CrankTranslationApiBase=http://127.0.0.1:9']),('original',['-CrankTranslationOriginalProbe'])]:
    directory=out/name
    args=['python',str(root/'tools/probe-ue-android.py'),'--adb',adb,'--device',a.serial,'--output',str(directory),'--timeout','90',
          '--arg=-CrankTranslationProbe','--arg=-CrankImmersive','--arg=-CrankLibraryRoot='+library]
    args+=['--arg='+f for f in flags]
    with (out/(name+'.log')).open('wb') as log:done=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=120)
    assert done.returncode==0,name
    result=json.loads((directory/'results.json').read_text('utf-8'))
    assert result['translationPassed'] and result['dialogAbsent'],result
    if name=='original':assert result['translationOriginalPassed']
    else:assert result['inlineChineseApplied']
    log=(directory/'logcat.txt').read_text('utf-8',errors='replace')
    metrics=re.findall(r'Crank online translation.*provider=(\w+).*elapsedMs=([\d.]+).*parts=(\d+)',log)
    result=dict(name=name,passed=True,translationPassed=True,dialogAbsent=True,inlineChineseApplied=result['inlineChineseApplied'],
                originalRestored=result['translationOriginalPassed'],requestMetrics=metrics)
    results.append(result);print(json.dumps(result),flush=True)
(out/'results.json').write_text(json.dumps(dict(passed=True,testLibrary=library,runs=results),indent=2),'utf-8')
