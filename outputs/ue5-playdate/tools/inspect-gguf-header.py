"""Inspect GGUF metadata/tensor descriptors without interpreting quantized data."""
import argparse, json, struct
from collections import Counter
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('model');a=p.parse_args()
with open(a.model,'rb') as f:
    def read(n):
        b=f.read(n)
        if len(b)!=n: raise ValueError('truncated header')
        return b
    def number(fmt):return struct.unpack('<'+fmt,read(struct.calcsize(fmt)))[0]
    def string():return read(number('Q')).decode('utf-8')
    formats={0:'B',1:'b',2:'H',3:'h',4:'I',5:'i',6:'f',7:'?',10:'Q',11:'q',12:'d'}
    def value(t,keep=False):
        if t==8:return string()
        if t==9:
            kind,count=number('I'),number('Q')
            for _ in range(count):value(kind)
            return None
        return number(formats[t])
    if read(4)!=b'GGUF':raise ValueError('not GGUF')
    version,tensors,entries=number('I'),number('Q'),number('Q');meta={}
    for _ in range(entries):
        k=string();v=value(number('I'))
        if k.startswith('general.'):meta[k]=v
    rows=[]
    for _ in range(tensors):
        name=string();dims=[number('Q') for _ in range(number('I'))]
        type_offset=f.tell();kind=number('I');offset=number('Q')
        rows.append({'name':name,'shape':dims,'type':kind,'typeOffset':type_offset,'dataOffset':offset})
    print(json.dumps({'version':version,'metadata':meta,'headerBytes':f.tell(),'types':dict(Counter(r['type'] for r in rows)),'tensors':rows[:8]},ensure_ascii=False,indent=2))
