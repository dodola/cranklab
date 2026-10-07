"""Actual CPU GGUF inference and abort checks; no phone interaction."""
import argparse, ctypes, json, os, time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--model',required=True);p.add_argument('--dll',required=True);p.add_argument('--source',required=True);p.add_argument('--output',required=True);a=p.parse_args()
folder=Path(a.dll).resolve().parent
directory=os.add_dll_directory(str(folder))
lib=ctypes.CDLL(str(Path(a.dll).resolve()))
cancel_type=ctypes.CFUNCTYPE(ctypes.c_int,ctypes.c_void_p)
progress_type=ctypes.CFUNCTYPE(None,ctypes.c_void_p,ctypes.c_int,ctypes.c_int)
run=lib.crank_translate;run.argtypes=[ctypes.c_char_p]*3+[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_int,cancel_type,progress_type,ctypes.c_void_p];run.restype=ctypes.c_int
verify=lib.crank_translation_verify_model;verify.argtypes=[ctypes.c_char_p,cancel_type,ctypes.c_void_p];verify.restype=ctypes.c_int
stop=False;parts=[];cancel_at_prefill=False
@cancel_type
def cancel(_):return int(stop)
@progress_type
def progress(_,part,total):
    global stop
    parts.append([part,total])
    if cancel_at_prefill and part>0:stop=True
output=ctypes.create_string_buffer(262144);error=ctypes.create_string_buffer(256)
def call(text):
    output.value=b'';error.value=b''
    return run(a.model.encode(),str(folder).encode(),text.encode(),output,len(output),error,len(error),cancel,progress,None)
begin=time.monotonic();assert verify(a.model.encode(),cancel,None)==0
assert call('')==1
stop=True;assert call('Move the crank clockwise.')==2;stop=False
source=json.loads(Path(a.source).read_text(encoding='utf-8'))['description']
# Force multiple complete chunks, including Unicode, paragraph breaks, controls.
source=source+'\n\n'+('Use the crank to turn clockwise. Hold A and B together to continue.\n'*24)
code=call(source);translation=output.value.decode('utf-8');assert code==0,error.value
assert any('\u4e00'<=c<='\u9fff' for c in translation)
assert len([part for part in parts if part[0]>0])>=2
long_parts=parts.copy();cancel_at_prefill=True
assert call('Hold A to start. Turn the crank to continue.')==2,error.value
assert not output.value
report={'passed':True,'sha256Verified':True,'emptyRejected':True,'earlyCancelPassed':True,'prefillCancelPassed':True,'longDescriptionPassed':True,'segments':long_parts,'seconds':round(time.monotonic()-begin,2),'translation':translation}
Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='translation'}))
