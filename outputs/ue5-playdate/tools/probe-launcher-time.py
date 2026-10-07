"""SDK epoch golden values and round trips used by downloaded games."""
import ctypes,json,os,time,argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True);game=out/'time.pdx';game.mkdir(exist_ok=True)
(game/'pdxinfo').write_text('name=SDK epoch verification\nbundleID=com.cranklab.epochprobe\n')
(game/'main.lua').write_text('''local pd=playdate
local s,ms=pd.epochFromGMTTime({year=2000,month=1,day=1,millisecond=123})
assert(s==0 and ms==123)
local t=pd.GMTTimeFromEpoch(0,123)
assert(t.year==2000 and t.month==1 and t.day==1 and t.millisecond==123)
local leap={year=2024,month=2,day=29,hour=12,minute=34,second=56,millisecond=789}
local n,m=pd.epochFromGMTTime(leap);local r=pd.GMTTimeFromEpoch(n,m)
assert(n==762525296 and m==789 and r.day==29 and r.second==56)
local now,sub=pd.getSecondsSinceEpoch();local utc=pd.epochFromGMTTime(pd.getGMTTime())
assert(type(now)=='number' and sub>=0 and sub<1000 and math.abs(now-utc)<=1)
local localTime=pd.getTime();local seconds,millis=pd.epochFromTime(localTime)
local again=pd.timeFromEpoch(seconds,millis)
assert(again.year==localTime.year and again.hour==localTime.hour and again.second==localTime.second)
function pd.update() pd.graphics.clear(1);pd.graphics.drawText('Epoch assertions passed',10,10) end
''')
root=Path(__file__).resolve().parents[1];lib=root/'bin/Win64/crank-core.dll';os.environ['SDL_AUDIODRIVER']='dummy'
with os.add_dll_directory(str(lib.parent)):c=ctypes.CDLL(str(lib))
c.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p];c.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
try:
 assert c.crank_start(str(game).encode(),str(out/'saves').encode());time.sleep(1)
 error=ctypes.create_string_buffer(2048);c.crank_last_error(error,len(error));assert c.crank_status()==2,error.value.decode(errors='replace')
 result=dict(passed=True,epoch='2000-01-01 UTC',leapSecondValue=762525296,roundTrip=True)
 (out/'results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
finally:c.crank_stop()
