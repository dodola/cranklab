"""Real inference: streaming UTF-8, cache isolation, verification reuse and release. No device changes."""
import argparse,ctypes,json,os,time
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--model',required=True);parser.add_argument('--dll',required=True);parser.add_argument('--output',required=True);args=parser.parse_args();out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True);folder=Path(args.dll).resolve().parent
with os.add_dll_directory(str(folder)):lib=ctypes.CDLL(str(Path(args.dll).resolve()))
C=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p);P=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_int,ctypes.c_int);S=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_char_p)
run=lib.crank_translate_stream;run.argtypes=[ctypes.c_char_p]*3+[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,C,P,S,ctypes.c_void_p];run.restype=ctypes.c_int
stats=lib.crank_translation_stats;stats.argtypes=[ctypes.c_void_p,ctypes.c_int]
verify=lib.crank_translation_verify_model;verify.argtypes=[ctypes.c_char_p,C,ctypes.c_void_p]
model=str(Path(args.model).resolve()).encode()
stopping=False;pieces=[];segments=[];begin=0
@C
def cancel(_):return int(stopping)
@P
def progress(_,part,total):segments.append([part,total])
@S
def stream(_,text):pieces.append([round(time.monotonic()-begin,3),text.decode('utf-8')])
def call(text):
 global begin,pieces,segments
 pieces=[];segments=[];buf=ctypes.create_string_buffer(262144);err=ctypes.create_string_buffer(256);begin=time.monotonic()
 code=run(model,str(folder).encode(),text.encode(),buf,len(buf),err,len(err),cancel,progress,stream,None)
 result=ctypes.create_string_buffer(2048);assert stats(result,len(result))==0
 return code,buf.value.decode('utf-8'),err.value.decode(),json.loads(result.value)
start=time.monotonic();assert verify(model,cancel,None)==0;first=time.monotonic()-start
start=time.monotonic();assert verify(model,cancel,None)==0;cached=time.monotonic()-start
source='A puzzle game for Playdate. Turn the crank to rotate the board and find the path. Use the D-pad to move, press A to select and B to go back. Each level introduces a new challenge. Your progress is saved automatically, so you can stop at any time and continue later. Try to complete all levels with as few moves as possible.'
runs=[]
for text in [source,'Hold B to return to the menu. Turn the crank counterclockwise to lower the bridge.',source]:
 code,textout,error,metrics=call(text);assert code==0,error
 assert any('\u4e00'<=c<='\u9fff' for c in textout)
 assert pieces and pieces[-1][1]==textout
 assert pieces[0][0] < metrics['totalMs']/1000
 runs.append(dict(metrics=metrics,firstCallbackSeconds=pieces[0][0],streamUpdates=len(pieces),translation=textout))
assert not runs[0]['metrics']['modelReused']
assert runs[1]['metrics']['modelReused'] and runs[1]['metrics']['prefixTokensReused']>0
assert runs[2]['metrics']['modelReused'] and runs[2]['metrics']['prefixTokensReused']>0
lib.crank_translation_release();code,textout,error,released=call('Press A to start.');assert code==0 and not released['modelReused']
stopping=True;code,textout,error,metrics=call('Turn the crank.');assert code==2 and not textout;stopping=False
report=dict(passed=True,streamUtf8Valid=True,completedTextMatchesStream=True,reusesModelAndFixedPrefix=True,releaseAndReloadPassed=True,earlyCancellationPassed=True,verificationSeconds=round(first,3),cachedVerificationSeconds=round(cached,6),runs=runs)
(out/'native-stream.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in report.items() if k!='runs'}));lib.crank_translation_release()
