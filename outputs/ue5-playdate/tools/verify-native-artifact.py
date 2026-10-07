"""Verify APK ABI, 16 KB load alignment and the packaged runtime's code."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile


def loads(data):
    assert data[:6]==b'\x7fELF\x02\x01','Expected 64-bit little-endian ELF'
    offset=struct.unpack_from('<Q',data,32)[0]
    size,count=struct.unpack_from('<HH',data,54)
    segments=[]
    for index in range(count):
        kind,flags,start,address,physical,file_size,memory_size,alignment=struct.unpack_from('<IIQQQQQQ',data,offset+index*size)
        if kind!=1:continue
        assert alignment>=16384,'ELF load segment is not 16 KB aligned'
        # Stripping changes the ELF section-directory fields in the first
        # header, while executable/data load contents must remain identical.
        begin=start+(64 if start==0 else 0)
        segments.append({'address':address,'flags':flags,'fileSize':file_size,'memorySize':memory_size,
                         'alignment':alignment,'sha256':hashlib.sha256(data[begin:start+file_size]).hexdigest()})
    assert segments
    return segments


parser=argparse.ArgumentParser()
parser.add_argument('--apk',required=True)
parser.add_argument('--abi',choices=('arm64-v8a','x86_64'),required=True)
parser.add_argument('--library',required=True)
parser.add_argument('--ue-library',help='Optional unstripped Unreal application library to compare with packaged libUnreal.so')
parser.add_argument('--output',required=True)
args=parser.parse_args()
apk=Path(args.apk).resolve();library=Path(args.library).resolve()
with zipfile.ZipFile(apk) as archive:
    entry=f'lib/{args.abi}/libcrank-core.so'
    assert [n for n in archive.namelist() if n.endswith('/libcrank-core.so')]==[entry]
    packed=archive.read(entry);built=library.read_bytes()
    machine=struct.unpack_from('<H',packed,18)[0]
    assert machine=={'arm64-v8a':183,'x86_64':62}[args.abi]
    assert b'crank_copy_native_info\0' in packed
    assert loads(packed)==loads(built),'Packaged loadable runtime code differs from the verified build'
    for name in archive.namelist():
        if name.startswith('lib/') and name.endswith('.so'):loads(archive.read(name))
    report={'passed':True,'abi':args.abi,'apkBytes':apk.stat().st_size,
            'apkSha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'coreEntry':entry,
            'packedCoreSha256':hashlib.sha256(packed).hexdigest(),'builtCoreSha256':hashlib.sha256(built).hexdigest(),
            'loadedCodeMatchesBuild':True,'allNativeElfSegmentsAtLeast16KB':True,'coreSegments':loads(packed)}
    if args.ue_library:
        ue_library=Path(args.ue_library).resolve()
        ue_packed=archive.read(f'lib/{args.abi}/libUnreal.so')
        assert loads(ue_packed)==loads(ue_library.read_bytes()),'Packaged Unreal application differs from the verified build'
        report['unrealLoadedCodeMatchesBuild']=True
        report['unrealLibrary']=str(ue_library)
        report['unrealPackedSha256']=hashlib.sha256(ue_packed).hexdigest()
output=Path(args.output).resolve();output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
