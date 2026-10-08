import argparse,json,re,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--provider',choices=['microsoft','tencent'],default='microsoft');p.add_argument('--repeats',type=int,default=2);p.add_argument('--cache',default='D:/PlaydateAI/online-translation');a=p.parse_args()
root=Path(a.cache);adb='C:/Users/dodol/AppData/Local/Android/Sdk/platform-tools/adb.exe'
root.mkdir(parents=True,exist_ok=True)
if not (root/'cases.json').exists():shutil.copyfile(Path(__file__).with_name('online-translation-cases.json'),root/'cases.json')
def call(*args):return subprocess.run([adb,'-s',a.serial,*args],capture_output=True,encoding='utf-8',errors='replace',timeout=120,check=True).stdout
call('push',str(root/'cases.json'),'/data/local/tmp/cranklab-online-cases.json')
call('shell','run-as','com.cranklab.onlineprobe','mkdir','-p','files')
call('shell','run-as','com.cranklab.onlineprobe','cp','/data/local/tmp/cranklab-online-cases.json','files/cases.json')
raw=call('shell','am','instrument','-w','-r','-e','provider',a.provider,'-e','repeats',str(a.repeats),'com.cranklab.onlineprobe/com.cranklab.onlineprobe.OnlineProbe')
(root/('phone-'+a.provider+'.log')).write_text(raw,'utf-8')
match=re.search(r'INSTRUMENTATION_RESULT: runs=(.+)',raw)
if not match:raise RuntimeError(raw[-2000:])
runs=json.loads(match.group(1));cases={c['name']:c for c in json.loads((root/'cases.json').read_text('utf-8'))}
for run in runs:
    case=cases[run['case']];text=run.get('translation','')
    retained=bool(text)
    for token,value in case['keep']:
        marker=re.fullmatch(r'(\D+)(\d+)(QXZ)',token)
        pattern=re.compile(re.escape(marker[1])+r'\s*'+marker[2]+r'\s*QXZ',re.I)
        retained=retained and len(pattern.findall(text))==1
        text=pattern.sub(lambda _:value,text)
    run['protectedTokensRetained']=retained
    run['translation']=text.replace('曲柄','摇柄');run['source']=case['source']
(root/('phone-'+a.provider+'.json')).write_text(json.dumps(runs,ensure_ascii=False,indent=2),'utf-8')
print(json.dumps([{k:v for k,v in run.items() if k not in ('translation','source')} for run in runs],ensure_ascii=False,indent=2))
if any('error' in r or not r['protectedTokensRetained'] for r in runs):raise SystemExit(2)
