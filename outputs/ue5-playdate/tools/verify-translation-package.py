"""Check the delivered translation ELF code, load closure and CPU variants."""
import argparse,hashlib,json,struct,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--apk',required=True);p.add_argument('--output',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
def elf(data):
    assert data[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',data,18)[0]==183
    offset=struct.unpack_from('<Q',data,32)[0];size,count=struct.unpack_from('<HH',data,54)
    headers=[struct.unpack_from('<IIQQQQQQ',data,offset+i*size) for i in range(count)]
    segments=[]
    for kind,flags,start,address,_,length,memory,align in headers:
        if kind==1:
            assert align>=16384 and start%16384==address%16384
            begin=start+(64 if start==0 else 0)
            segments.append((flags,start,address,length,memory,align,hashlib.sha256(data[begin:start+length]).hexdigest()))
    dynamic=next(h for h in headers if h[0]==2);tags=[]
    for at in range(dynamic[2],dynamic[2]+dynamic[5],16):
        tag,value=struct.unpack_from('<QQ',data,at)
        if not tag:break
        tags.append((tag,value))
    strings=next(v for t,v in tags if t==5)
    load=next(h for h in headers if h[0]==1 and h[3]<=strings<h[3]+h[5]);strings=load[2]+strings-load[3]
    def string(index):return data[strings+index:data.index(b'\0',strings+index)].decode()
    return segments,[string(v) for t,v in tags if t==1]
with zipfile.ZipFile(a.apk) as z:
    packed_names={Path(n).name:n for n in z.namelist() if n.startswith('lib/arm64-v8a/') and n.endswith('.so')}
    reports=[];variants=0
    for path in sorted((root/'Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Translation/Android/arm64-v8a').glob('*.so')):
        assert path.name in packed_names,path.name
        packed=z.read(packed_names[path.name]);loads,needed=elf(packed)
        assert loads==elf(path.read_bytes())[0],path.name
        for name in needed:
            assert name in packed_names or name in {'libc.so','libm.so','libdl.so','liblog.so','libandroid.so'},(path.name,name)
            assert not name.endswith('.so.0'),name
        variants+=path.name.startswith('libggml-cpu-')
        reports.append({'name':path.name,'needed':needed,'sha256':hashlib.sha256(packed).hexdigest(),'codeMatchesBuild':True,'loadAlignment16KB':True})
    assert variants==7
    assert not any(n.endswith('.gguf') for n in z.namelist())
result={'passed':True,'cpuVariants':variants,'modelDownloadedSeparately':True,'libraries':reports}
Path(a.output).write_text(json.dumps(result,indent=2),'utf-8');print(json.dumps({'passed':True,'libraries':len(reports),'cpuVariants':variants}))
