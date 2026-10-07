"""Adversarial archives for the actual product installer commandlet."""
import argparse,json,struct,zipfile,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True);library=out/('library-'+uuid.uuid4().hex)
info=b'name=Launcher Install Probe\nbundleID=com.cranklab.launcherprobe\nversion=1\n'
lua=b'function playdate.update() playdate.graphics.clear(1) end'
cases=[]
def archive(name,entries,valid=False,cancel=False,mutate=None):
 path=out/(name+'.zip')
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as z:
  for n,b in entries:z.writestr(n,b)
 if mutate:mutate(path)
 cases.append(dict(name=name,archive=str(path),library=str(library),valid=valid,cancelled=cancel))
normal=[('probe.pdx/pdxinfo',info),('probe.pdx/main.lua',lua)]
archive('valid',normal,True);archive('repeat',normal,True)
archive('cancelled',normal,cancel=True)
archive('traversal',normal+[('../escaped.lua',lua)])
archive('absolute',normal+[('/escaped.lua',lua)])
archive('windows-path',normal+[('C:/escaped.lua',lua)])
def raw_backslash(path):path.write_bytes(path.read_bytes().replace(b'probe.pdx/escaped.lua',b'probe.pdx\\escaped.lua'))
archive('backslash',normal+[('probe.pdx/escaped.lua',lua)],mutate=raw_backslash)
archive('case-duplicate',normal+[('PROBE.PDX/MAIN.LUA',lua)])
archive('multiple-games',normal+[('other.pdx/pdxinfo',info),('other.pdx/main.lua',lua)])
archive('missing-entry',[('probe.pdx/pdxinfo',info)])
archive('native-invalid',normal+[('probe.pdx/pdex.bin',b'bad'*32)])
link=zipfile.ZipInfo('probe.pdx/link');link.create_system=3;link.external_attr=(0o120777<<16)
archive('symlink',normal+[(link,b'../../outside')])
def bad_crc(path):
 data=bytearray(path.read_bytes());pos=data.index(b'PK\x01\x02');data[pos+16]^=1;path.write_bytes(data)
archive('crc-mismatch',normal,mutate=bad_crc)
def oversized(path):
 data=bytearray(path.read_bytes());pos=data.index(b'PK\x01\x02');struct.pack_into('<I',data,pos+24,513*1024*1024);path.write_bytes(data)
archive('oversized-entry',normal,mutate=oversized)
def truncated(path):path.write_bytes(path.read_bytes()[:-15])
archive('truncated',normal,mutate=truncated)
(out/'cases.json').write_text(json.dumps(dict(cases=cases),indent=2),encoding='utf-8')
print(out/'cases.json')
