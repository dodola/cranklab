"""Verify the online-only APK, native core identity and 16 KiB alignment."""
import argparse,hashlib,json,re,struct,zipfile
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
    return segments

apk=Path(a.apk)
with zipfile.ZipFile(apk) as z:
    names=z.namelist();libraries={Path(n).name:n for n in names if n.startswith('lib/arm64-v8a/') and n.endswith('.so')}
    obsolete=re.compile(r'lib(?:ggml.*|llama|crank-translate|vendor-hash)\.so$')
    assert not any(obsolete.fullmatch(n) for n in libraries),libraries
    assert not any(n.lower().endswith(('.gguf','.litertlm')) for n in names)
    assert not any(b'CrankModelDownload' in z.read(n) for n in names if n.endswith('.dex'))
    assert {'libUnreal.so','libcrank-core.so'}<=libraries.keys()
    reports=[]
    for name,path in sorted(libraries.items()):
        packed=z.read(path);loads=elf(packed)
        report=dict(name=name,sha256=hashlib.sha256(packed).hexdigest(),loadAlignment16KB=True)
        if name in ('libcrank-core.so','libcrank-runtime.so'):
            built=root/'Unreal/Plugins/CrankRuntime/Binaries/ThirdParty/Android/arm64-v8a'/name
            assert loads==elf(built.read_bytes()),name
            report['codeMatchesBuild']=True
        reports.append(report)
result=dict(passed=True,onlineOnly=True,localInferenceLibrariesAbsent=True,modelDownloaderAbsent=True,
            bundledModelsAbsent=True,apkBytes=apk.stat().st_size,apkSha256=hashlib.sha256(apk.read_bytes()).hexdigest(),libraries=reports)
Path(a.output).write_text(json.dumps(result,indent=2),'utf-8');print(json.dumps({k:v for k,v in result.items() if k!='libraries'}))
