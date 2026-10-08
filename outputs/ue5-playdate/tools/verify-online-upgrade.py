"""Snapshot/verify app-private upgrade data without changing saves."""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('mode',choices=['snapshot','verify']);p.add_argument('--serial',required=True);p.add_argument('--output',required=True);a=p.parse_args()
adb='C:/Users/dodol/AppData/Local/Android/Sdk/platform-tools/adb.exe';pkg='com.cranklab.emulator'
def call(*args):return subprocess.run([adb,'-s',a.serial,*args],capture_output=True,encoding='utf-8',errors='replace',timeout=30,check=True).stdout
script='find files/CrankLab/GameSaves files/CrankLab/Translation/Cache files/CrankLab/Translation/Descriptions -type f -exec sha256sum {} \\;'
raw=call('shell','run-as '+pkg+' sh -c '+shlex.quote(script));files={}
for line in raw.splitlines():
    if len(line)>66 and all(c in '0123456789abcdef' for c in line[:64]):files[line[66:]]=line[:64]
models=call('shell','run-as',pkg,'ls','-l','files/CrankLab/Translation/Models')
path=Path(a.output)
if a.mode=='snapshot':
    assert files,'No app data found'
    result=dict(files=files,modelsBefore=models);path.write_text(json.dumps(result,indent=2),'utf-8')
    print(json.dumps(dict(snapshotFiles=len(files),modelPresent='Hy-MT2-' in models)))
else:
    before=json.loads(path.read_text());stable={k:v for k,v in before['files'].items() if '/com.pdgamez.browser/' not in k}
    changed=[k for k,v in stable.items() if files.get(k)!=v]
    assert not changed,changed
    assert 'Hy-MT2-' not in models,models
    result=dict(passed=True,oldModelsRemoved=True,preservedFiles=len(stable),launcherCacheExcluded=True)
    path.with_name('upgrade-result.json').write_text(json.dumps(result,indent=2),'utf-8');print(json.dumps(result))
