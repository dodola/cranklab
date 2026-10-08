"""Regression: fractional progress must render in the real bundled PDGames detail."""
import argparse,ctypes,json,os,shutil,time
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
seed=Path('work/immersive-launcher-seeded-library/GameSaves/d00f96e407353d2703fdf487d3993bd8/com.pdgamez.browser')
shutil.copytree(seed,out/'saves/com.pdgamez.browser',dirs_exist_ok=True)
detail=json.loads(Path('work/launcher-dino-detail.json').read_text('utf-8-sig'));detail['price']=detail['price']['current']
encoded=json.dumps(json.dumps(detail,separators=(',',':')))
os.environ['SDL_AUDIODRIVER']='dummy'
os.environ['PD_EVAL']=f'''local fixture=json.decode({encoded});local d=screenManager.getScreen('gameDetail');local draw=d.draw;local label
for i=1,80 do local name,value=debug.getupvalue(draw,i);if name=='languageLabel' then label=value end end
assert(label,'missing adapter label owner')
d.draw=function(self,...)local r=draw(self,...);local current
 for i=1,80 do local name,value=debug.getupvalue(label,i);if name=='state' then current=value end end
 local t=(current or {{}}).translation or {{}}
 if t.testExpected then assert(label()==t.testExpected,'progress label: '..label()..' != '..t.testExpected)end;return r end
local update=playdate.update;local once=true
playdate.update=function(...)local r=update(...);if once and not screenManager.isTransitioning()then once=false;screenManager.switch('gameDetail',{{game=fixture}},'left')end;return r end'''
with os.add_dll_directory(str(root/'bin/Win64')):c=ctypes.CDLL(str(root/'bin/Win64/crank-core.dll'))
c.crank_start_launcher.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
c.crank_launcher_request.argtypes=[ctypes.c_void_p,ctypes.c_uint32];c.crank_launcher_request.restype=ctypes.c_uint32
c.crank_launcher_reply.argtypes=[ctypes.c_uint32,ctypes.c_char_p];c.crank_launcher_detail_action.argtypes=[ctypes.c_uint32]
c.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32];c.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
def advance(seconds):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        error=ctypes.create_string_buffer(2048);c.crank_last_error(error,len(error))
        assert c.crank_status()!=3,error.value.decode(errors='replace')
        time.sleep(.02)
cases=[(0,'下载0%'),(.00001,'下载0%'),(.12345,'下载12%'),(.501,'下载50%'),(.9999,'下载99%'),(1,'下载100%'),(2,'下载100%'),(-1,'下载0%'),('invalid','连接中'),(None,'连接中')]
try:
    assert c.crank_start_launcher(str(root/'Unreal/Content/Games/launcher.pdx').encode(),str(out/'saves').encode())
    deadline=time.monotonic()+20;token=None
    while time.monotonic()<deadline:
        n=c.crank_launcher_request(None,0)
        if n:
            buffer=ctypes.create_string_buffer(n);c.crank_launcher_request(buffer,n);token=json.loads(buffer.value)['session'];break
        advance(.03)
    assert token is not None
    def reply(translation):
        body=dict(id=1058,state='ready',label='Download & Play',busy=False,translation=dict(id=1058,state='ready',modelReady=False,**translation))
        assert c.crank_launcher_reply(token,json.dumps(body,ensure_ascii=False).encode())==1
        advance(.4)
    reply(dict(modelBusy=False));advance(3)
    assert c.crank_launcher_detail_action(2)==1 # Enter Chinese mode, without a model download.
    advance(.4)
    n=c.crank_launcher_request(None,0);assert n>0,'Chinese action was not consumed'
    event=ctypes.create_string_buffer(n);c.crank_launcher_request(event,n)
    assert json.loads(event.value)['action']=='translate',event.value
    for index,(progress,label) in enumerate(cases):
        translation=dict(modelBusy=True,modelState='downloading',testExpected=label)
        if progress is not None:translation['modelProgress']=progress
        reply(translation)
        if index in (2,4):
            pixels=(ctypes.c_uint8*(400*240*4))();assert c.crank_copy_frame(pixels,len(pixels))>0
            Image.frombytes('RGBA',(400,240),bytes(pixels),'raw','BGRA').resize((800,480),Image.Resampling.NEAREST).save(out/f'progress-{index}.png')
    reply(dict(modelBusy=True,modelState='verifying',testExpected='校验中'))
    reply(dict(modelBusy=False,modelState='wifi',testExpected='需Wi-Fi'))
    result={'passed':True,'fractionalAndBoundedProgressCases':len(cases),'verificationStagePassed':True,'wifiLabelPassed':True,'runtimeError':''}
    (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
finally:c.crank_stop()
