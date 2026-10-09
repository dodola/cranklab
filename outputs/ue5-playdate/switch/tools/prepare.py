"""Stage the supplied launcher and public metadata. No SDK, C core, or model is bundled."""
import argparse,hashlib,json,shutil,ssl
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--launcher');p.add_argument('--ca-file');a=p.parse_args()
root=Path(__file__).resolve().parents[1];romfs=root/'romfs';romfs.mkdir(exist_ok=True)
source=Path(a.launcher) if a.launcher else root/'../Unreal/Content/Games/launcher.pdx'
info=(source/'pdxinfo').read_text(encoding='utf-8')
assert 'bundleID=com.pdgamez.browser' in info and 'buildNumber=18' in info,'Expected supplied PDGames build 18'
# This exact supplied launcher includes an optional 6 KiB native QR accelerator.
# Its documented Lua/CoreLibs QR fallback is used on Switch; arbitrary mixed
# games are still rejected. Never strip native code from downloaded games.
assert hashlib.sha256((source/'main.pdz').read_bytes()).hexdigest()=='8e7abd27d81cd9b6eb58da0db597e2160bad014aa8510fb7affc396e0bd4a6c3','Unexpected launcher bytecode'
shutil.copytree(source,romfs/'launcher.pdx',dirs_exist_ok=True,ignore=shutil.ignore_patterns('pdex.bin','pdex.dll','pdex.dylib','cranklab-zh.pft'))
for name in ('pdex.bin','pdex.dll','pdex.dylib'):
    (romfs/'launcher.pdx'/name).unlink(missing_ok=True)
(romfs/'launcher.pdx/fonts/cranklab-zh.pft').unlink(missing_ok=True)
shutil.copyfile(root/'../Unreal/Content/Games/launcher-catalog.json',romfs/'launcher-catalog.json')
ca=Path(a.ca_file or ssl.get_default_verify_paths().cafile or '/etc/ssl/certs/ca-certificates.crt')
assert ca.is_file(),'Pass --ca-file with a public Mozilla/system CA bundle'
shutil.copyfile(ca,romfs/'cacert.pem')
(romfs/'build.json').write_text(json.dumps({'platform':'Switch','version':'0.1.0','luaOnly':True,'translation':False,'unreal':False},indent=2))
print(f'Staged {sum(p.stat().st_size for p in romfs.rglob("*") if p.is_file())} bytes into {romfs}')
