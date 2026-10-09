"""Inspect the actual ARM executable and embedded NRO assets, then export NRO."""
import argparse, hashlib, json, os, shutil, struct, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
elf=(a.build/'playdate-switch.elf').read_bytes()
assert elf[:6]==b'\x7fELF\x02\x01' and struct.unpack_from('<H',elf,18)[0]==183,'Expected AArch64 ELF64'
nro=(a.build/'playdate-switch.nro').read_bytes();assert nro[16:20]==b'NRO0'
end=struct.unpack_from('<I',nro,24)[0];assert 0x80<=end<len(nro) and nro[end:end+4]==b'ASET'
for offset in (0x20,0x28,0x30):
    start,size=struct.unpack_from('<II',nro,offset);assert start+size<=end
assets={}
for name,offset in [('icon',8),('nacp',24),('romfs',40)]:
    begin,size=struct.unpack_from('<QQ',nro,end+offset);assert size>0 and end+begin+size<=len(nro)
    assets[name]=nro[end+begin:end+begin+size]
assert assets['icon'][:2]==b'\xff\xd8' and len(assets['nacp'])==0x4000
assert assets['nacp'][:512].split(b'\0',1)[0]==b'Playdate'
rom=assets['romfs'];header,_,_,dir_offset,dir_size,_,_,file_offset,file_size,data_offset=struct.unpack_from('<10Q',rom)
assert header==0x50 and dir_offset+dir_size<=len(rom) and file_offset+file_size<=len(rom)
files={};seen_dirs=set();seen_files=set()
def directory(offset,path):
    assert offset not in seen_dirs and len(seen_dirs)<1024 and offset+24<=dir_size
    seen_dirs.add(offset)
    parent,sibling,child,file,_,name_size=struct.unpack_from('<6I',rom,dir_offset+offset)
    assert offset+24+name_size<=dir_size
    name=rom[dir_offset+offset+24:dir_offset+offset+24+name_size].decode()
    full=path+'/'+name if name else path
    while file!=0xffffffff:
        assert file not in seen_files and len(seen_files)<8192 and file+32<=file_size
        seen_files.add(file)
        _,next_file,start,size,_,n=struct.unpack_from('<IIQQII',rom,file_offset+file)
        assert file+32+n<=file_size and data_offset+start+size<=len(rom)
        filename=rom[file_offset+file+32:file_offset+file+32+n].decode()
        key=(full+'/'+filename).lstrip('/');assert key not in files
        files[key]=rom[data_offset+start:data_offset+start+size];file=next_file
    while child!=0xffffffff:child=directory(child,full)
    return sibling
directory(0,'')
staged={p.relative_to(ROOT/'romfs').as_posix():p.read_bytes() for p in (ROOT/'romfs').rglob('*') if p.is_file()}
assert files==staged,'NRO RomFS differs from staged assets; check incremental build dependencies'
assert 'launcher.pdx/main.pdz' in files and 'launcher-catalog.json' in files and 'cacert.pem' in files
assert not any(n.lower().endswith(('.bin','.dll','.dylib','.gguf','.pte')) for n in files)
assert hashlib.sha256(files['launcher.pdx/main.pdz']).hexdigest()=='8e7abd27d81cd9b6eb58da0db597e2160bad014aa8510fb7affc396e0bd4a6c3'
meta=json.loads(files['build.json']);assert meta['luaOnly'] and not meta['translation'] and not meta['unreal']
nm=Path(os.getenv('DEVKITPRO','/opt/devkitpro'))/'devkitA64/bin/aarch64-none-elf-nm'
symbols=subprocess.run([str(nm),'-g','--defined-only',str(a.build/'playdate-switch.elf')],check=True,capture_output=True,text=True).stdout
for name in ('crank_start_launcher','pd_native_load','pd_http_fetch','SDL_CreateRenderer','romfsInit'):
    # romfsInit is inline, so it may be represented only by romfsMountSelf.
    assert (name in symbols) or (name=='romfsInit' and 'romfsMountSelf' in symbols),name
assert not any(s in symbols for s in ('uc_open','uc_emu_start','llama_load','llama_model','ggml_backend','FEngineLoop'))
a.output.mkdir(parents=True,exist_ok=True);target=a.output/'Playdate.nro';shutil.copyfile(a.build/'playdate-switch.nro',target)
digest=hashlib.sha256(nro).hexdigest();(a.output/'Playdate.nro.sha256').write_text(digest+'  Playdate.nro\n')
report={'passed':True,'version':'0.1.0','platform':'Switch AArch64','size':len(nro),'sha256':digest,
        'embeddedFiles':{k:len(v) for k,v in files.items()},'luaOnly':True,'unreal':False,'translation':False,
        'hardwareVerified':False,'output':str(target)}
(a.output/'artifact-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report,indent=2))
