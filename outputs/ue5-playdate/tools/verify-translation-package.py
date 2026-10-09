"""Verify the APK: local Hy-MT2 inference libraries (CPU/OpenCL/Hexagon + DSP skeletons), downloader, no bundled model, native identity and 16 KiB alignment."""
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
    required={'libUnreal.so','libcrank-core.so','libcrank-translate.so','libllama.so','libggml.so','libggml-base.so','libggml-cpu.so',
              'libggml-opencl.so','libggml-hexagon.so','libggml-htp-v73.so','libggml-htp-v75.so','libggml-htp-v79.so','libggml-htp-v81.so'}
    missing=required-libraries.keys();assert not missing,sorted(missing)
    assert not any(n.lower().endswith(('.gguf','.litertlm')) for n in names)
    assert any(b'CrankModelDownload' in z.read(n) for n in names if n.endswith('.dex'))
    reports=[]
    for name,path in sorted(libraries.items()):
        packed=z.read(path)
        report=dict(name=name,sha256=hashlib.sha256(packed).hexdigest())
        if name.startswith('libggml-htp-'):
            # Hexagon DSP skeletons run on the NPU, not the app CPU; only their identity is checked.
            assert packed[:4]==b'\x7fELF' and struct.unpack_from('<H',packed,18)[0]==164,name
            report['hexagonDspElf']=True
        else:
            loads=elf(packed);report['loadAlignment16KB']=True
            for folder in ('Android/arm64-v8a','Translation/Android/arm64-v8a'):
                built=root/'Unreal/Plugins/CrankRuntime/Binaries/ThirdParty'/folder/name
                if built.exists() and name in ('libcrank-core.so','libcrank-runtime.so','libcrank-translate.so'):
                    assert loads==elf(built.read_bytes()),name
                    report['codeMatchesBuild']=True
        reports.append(report)
result=dict(passed=True,localInferenceLibrariesPresent=True,hexagonSkeletonsPresent=True,modelDownloaderPresent=True,
            bundledModelsAbsent=True,apkBytes=apk.stat().st_size,apkSha256=hashlib.sha256(apk.read_bytes()).hexdigest(),libraries=reports)
Path(a.output).write_text(json.dumps(result,indent=2),'utf-8');print(json.dumps({k:v for k,v in result.items() if k!='libraries'}))
