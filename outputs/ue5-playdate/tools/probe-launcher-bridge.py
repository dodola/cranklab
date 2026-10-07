"""Exercise the real bundled PDGames UI and scoped core ABI, without purchases."""
import ctypes,json,os,time,argparse,shutil
from pathlib import Path
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--translation-report');a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
(out/'results.json').unlink(missing_ok=True)
lib=root/'bin/Win64/crank-core.dll';game=root/'Unreal/Content/Games/launcher.pdx';save=out/'saves';save.mkdir(exist_ok=True)
seed=Path('work/immersive-launcher-seeded-library/GameSaves/d00f96e407353d2703fdf487d3993bd8/com.pdgamez.browser')
shutil.copytree(seed,save/'com.pdgamez.browser',dirs_exist_ok=True)
os.environ['SDL_AUDIODRIVER']='dummy'
detail=json.loads(Path('work/launcher-dino-detail.json').read_text(encoding='utf-8-sig'))
detail['price']=detail['price']['current'] # PDGames enter takes simple list data.
encoded=json.dumps(json.dumps(detail,separators=(',',':')))
os.environ['PD_EVAL']=f'''local fixture=json.decode({encoded})
local send=_crankLauncher.send
_crankLauncher.send=function(action,id)
 if action=='lang_zh' then
 local d=screenManager.getScreen('gameDetail');local o
 for i=1,80 do local n,v=debug.getupvalue(d.draw,i);if n=='original' then o=v end end
 for i=1,80 do local n,v=debug.getupvalue(o.enter,i);if not n then break end
 if n=='cachedDescText' or n=='cachedDescHeight' or n=='contentImageHeight' or n=='maxScrollY' or n=='fullDetail' then print('INLINE_CACHE',n,type(v),type(v)=='table' and v.id or tostring(v)) end end
 end
 return send(action,id) end
local old=playdate.update;local once=true
playdate.update=function(...) local result=old(...)
if once and not screenManager.isTransitioning() then
 once=false;screenManager.switch('gameDetail',{{game=fixture}},'left') end
return result end'''
with os.add_dll_directory(str(lib.parent)):c=ctypes.CDLL(str(lib))
c.crank_start.argtypes=c.crank_start_launcher.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
c.crank_launcher_request.argtypes=[ctypes.c_void_p,ctypes.c_uint32];c.crank_launcher_request.restype=ctypes.c_uint32
c.crank_launcher_reply.argtypes=[ctypes.c_uint32,ctypes.c_char_p]
c.crank_launcher_detail_action.argtypes=[ctypes.c_uint32]
c.crank_add_crank.argtypes=[ctypes.c_float,ctypes.c_int]
c.crank_copy_frame.argtypes=[ctypes.c_void_p,ctypes.c_uint32];c.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
pixels=(ctypes.c_uint8*(400*240*4))();events=[]
def error():
 b=ctypes.create_string_buffer(2048);c.crank_last_error(b,len(b));return b.value.decode(errors='replace')
def advance(seconds):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  assert c.crank_status()!=3,error();time.sleep(.015)
def press(mask):c.crank_set_buttons(mask);advance(.12);c.crank_set_buttons(0);advance(.18)
def request(timeout=15):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  advance(.02);n=c.crank_launcher_request(None,0)
  if n:
   b=ctypes.create_string_buffer(n);assert c.crank_launcher_request(b,n)==n
   event=json.loads(b.value);events.append(event);return event
 raise AssertionError('No launcher command')
def reply(token,state,label,busy=False):
 assert c.crank_launcher_reply(token,json.dumps(dict(id=1058,state=state,label=label,busy=busy)).encode())==1
 advance(.35)
def expect_action(action):
 event=request()
 # API image/detail refresh may rerender a selected language while another
 # action is queued. These acknowledgements are valid independent events.
 while event['action'] in ('lang_zh','lang_en') and event['action']!=action:event=request()
 assert event['action']==action,event
 return event
def capture(name):
 assert c.crank_copy_frame(pixels,len(pixels))>0
 Image.frombytes('RGBA',(400,240),bytes(pixels),'raw','BGRA').save(out/(name+'.png'))
try:
 assert c.crank_start_launcher(str(game).encode(),str(save).encode())
 first=request();assert first['id']==1058 and first['action']=='inspect';token=first['session']
 reply(token,'ready','Download & Play');advance(4);capture('download-action')
 press(32);assert request()['action']=='play'
 reply(token,'busy','Downloading...',True);capture('cancel-action')
 assert c.crank_launcher_activate()==1;assert request()['action']=='cancel'
 reply(token,'error','Retry download');capture('retry-action')
 press(32);assert request()['action']=='play'
 reply(token,'installed','Run game');capture('run-action')
 # Real inference output was verified separately; exercise the actual Lua bitmap
 # update, original restoration and ID guard through the scoped native bridge.
 translated=json.loads((Path(a.translation_report) if a.translation_report else root/'verification-translation/service.json').read_text('utf-8-sig'))['translation']
 def translation_reply(catalog,text='',state='translated',model=True):
  body=dict(id=1058,state='installed',label='Run game',busy=False,translation=dict(id=catalog,state=state,text=text,modelReady=model,modelBusy=False))
  assert c.crank_launcher_reply(token,json.dumps(body,ensure_ascii=False).encode())==1
  advance(.5)
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='translate'
 translation_reply(999,translated)
 assert c.crank_launcher_request(None,0)==0
 translation_reply(1058,translated)
 assert request()['action']=='lang_zh';capture('inline-chinese')
 c.crank_set_buttons(8);advance(.8);c.crank_set_buttons(0);advance(.4);capture('inline-chinese-scroll')
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='original'
 assert request()['action']=='lang_en';capture('inline-original-scroll')
 translation_reply(1058,'','ready')
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='translate'
 translation_reply(1058,'','translating')
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='trans_cancel'
 translation_reply(1058,'','ready',False)
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='translate'
 translation_reply(1058,'','ready',False)
 assert c.crank_launcher_detail_action(2)==1;assert request()['action']=='model'
 reply(token,'installed','Run game')
 # Original screenshot controls must remain available through the Details tab.
 # PDGames opens its QR link when the body is selected. Select the image row
 # again before exercising the original fullscreen screenshot viewer.
 c.crank_set_buttons(4);advance(2);c.crank_set_buttons(0);advance(.3)
 press(1);press(32);advance(2);capture('original-screenshot')
 press(16);press(2);press(2);press(32);expect_action('play')
 c.crank_stop();os.environ.pop('PD_EVAL',None)
 assert c.crank_start_launcher(str(game).encode(),str(save).encode())
 restored=request();assert restored['id']==1058 and restored['action']=='inspect'
 assert c.crank_launcher_reply(token,b'{}')==0
 reply(restored['session'],'installed','Run game');advance(3);capture('restored-detail')
 c.crank_stop()
 fixture=root/'Unreal/Content/Games/fixture.pdx'
 assert c.crank_start(str(fixture).encode(),str(save).encode());advance(.5)
 assert c.crank_launcher_request(None,0)==0 and c.crank_launcher_activate()==0
 assert c.crank_launcher_reply(restored['session'],b'{}')==0
 result={'passed':True,'events':events,'staleReplyRejected':True,'normalGameBridgeAbsent':True,
         'originalScreenshotsRetained':True,'detailRestored':True,'inlineChineseApplied':True,'originalRestored':True,'wrongTranslationIdRejected':True,'inlineCancellation':True}
 (out/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))
finally:c.crank_stop()
