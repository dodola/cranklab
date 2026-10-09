"""Check local inference and inline PDGames/cache/original behavior in an isolated phone library.

Seed this library's launcher saves and Translation/Descriptions/1058.json with the controls
fixture first. The model must already be present; this tool never downloads it.
"""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--adb',required=True);p.add_argument('--serial',required=True)
p.add_argument('--library',required=True);p.add_argument('--output',required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1]
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
pkg='com.cranklab.emulator';base=[a.adb,'-s',a.serial]
assert a.library.startswith('/data/user/0/'+pkg+'/files/CrankLab/local-htp-test-'), 'Use an isolated test library'
def adb(*args):return subprocess.run(base+list(args),capture_output=True,timeout=30,check=True).stdout
policy=adb('shell','dumpsys','window','policy').decode(errors='replace')
assert not re.search(r'\bmIsShowing=true\b',policy), 'Unlock the device for scene tests'
serverlog=(out/'server.log').open('wb')
server=subprocess.Popen([sys.executable,str(root/'tools/online-translation-fixture.py'),
    '--case','controls','--port','18791','--log',str(out/'requests.json')],stdout=serverlog,stderr=subprocess.STDOUT,
    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
results=[]
try:
    adb('reverse','tcp:18791','tcp:18791')
    for name,flags in [('cpu-inline',[]),('cached-offline',['-CrankTranslationDetailBase=http://127.0.0.1:9']),
                       ('original',['-CrankTranslationOriginalProbe'])]:
        directory=out/name
        args=[sys.executable,str(root/'tools/probe-ue-android.py'),'--adb',a.adb,'--device',a.serial,
              '--output',str(directory),'--timeout','180','--arg=-CrankTranslationProbe','--arg=-CrankImmersive',
              '--arg=-CrankLibraryRoot='+a.library,'--arg=-CrankTranslationProvider=local',
              '--arg=-CrankTranslationDevice=cpu','--arg=-CrankTranslationDetailBase=http://127.0.0.1:18791',
              '--arg=-CrankTranslationApiBase=http://127.0.0.1:18791']
        args+=['--arg='+flag for flag in flags]
        with (out/(name+'.log')).open('wb') as log:
            done=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=220)
        assert done.returncode==0,name
        result=json.loads((directory/'results.json').read_text('utf-8'))
        assert result['translationPassed'] and result['dialogAbsent'],result
        assert result['translationOriginalPassed'] if name=='original' else result['inlineChineseApplied'],result
        result=dict(name=name,passed=True,inlineChineseApplied=result['inlineChineseApplied'],
                    originalRestored=result['translationOriginalPassed'],translated=result['translated'])
        results.append(result);print(json.dumps(result,ensure_ascii=True),flush=True)
    requests=json.loads((out/'requests.json').read_text()) if (out/'requests.json').exists() else []
    assert requests==[], 'Explicit local provider must never call an online translation service'
    source=json.loads(adb('exec-out','run-as',pkg,'cat',a.library+'/Translation/Descriptions/1058.json').decode('utf-8-sig'))['description']
    normalized=source.replace('\r\n','\n').replace('\r','\n').replace('&nbsp;',' ').replace('&amp;','&').strip()
    key=hashlib.md5(('1058|hy-mt2-q4_0-5458acf2|zh-CN|prompt-1|'+normalized).encode()).hexdigest()
    cached=json.loads(adb('exec-out','run-as',pkg,'cat',a.library+'/Translation/Cache/'+key+'.json').decode('utf-8-sig'))
    assert cached['key']==key and cached['provider']=='local' and cached['translation']==results[0]['translated']
    assert all(run['translated']==cached['translation'] for run in results)
    report=dict(passed=True,testLibrary=a.library,noOnlineTranslationRequests=True,localCachePersisted=True,runs=results)
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
finally:
    server.terminate();server.wait(timeout=5);serverlog.close()
    adb('reverse','--remove','tcp:18791')
