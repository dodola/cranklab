"""Stage the supplied unmodified PDGames build and public-source metadata.

Download packages on D: are not bundled. Itch CDN URLs are intentionally omitted;
the host resolves a fresh URL from the publisher's currently exposed button.
"""
import argparse,hashlib,json,re,shutil
from pathlib import Path
from urllib.parse import urlsplit
p=argparse.ArgumentParser()
p.add_argument('--source',default='C:/Users/dodol/Downloads/pdgames.pdx')
p.add_argument('--catalog',default='D:/PlaydateGames/manifest.json')
a=p.parse_args()
content=Path(__file__).resolve().parents[1]/'Unreal/Content/Games'
source=Path(a.source);target=content/'launcher.pdx'
if 'bundleID=com.pdgamez.browser' not in (source/'pdxinfo').read_text() or 'buildNumber=18' not in (source/'pdxinfo').read_text():
    raise SystemExit('Expected supplied PDGames build 18')
target.mkdir(parents=True,exist_ok=True)
names=[];digest=hashlib.sha256()
for path in sorted(source.rglob('*')):
    if not path.is_file():continue
    name=path.relative_to(source).as_posix()
    if name in {'pdex.dylib','pdex.dll'}:continue # ARM binary provides its QR extension.
    names.append(name);data=path.read_bytes();digest.update(name.encode());digest.update(data)
    dest=target/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
(content/'launcher-manifest.json').write_text(json.dumps({'version':1,'bundleID':'com.pdgamez.browser','build':18,'sha256':digest.hexdigest(),'files':names},indent=2),encoding='utf-8')
manifest=json.loads(Path(a.catalog).read_text(encoding='utf-8'))
games={}
for g in manifest['games'].values():
    if not g.get('id'):continue
    plan={'kind':'publisher','source':next(iter(g.get('store_urls',[])),'')}
    files=[f for f in g.get('files',[]) if f.get('format')=='zip' and f.get('bundle_structure')=='compiled_entrypoint_and_metadata_present'
           and len(f.get('pdx_bundles',f.get('compiled_bundle_roots',[])))==1 and re.fullmatch(r'itch-upload-\d+',f.get('key',''))]
    files.sort(key=lambda f:(f.get('demo',False),-int(f['key'].split('-')[-1])))
    if files:
        f=files[0];u=f.get('source_url','')
        if urlsplit(u).hostname and urlsplit(u).hostname.endswith('.itch.io'):
            plan={'kind':'itch','source':u.split('?')[0].rstrip('/'),'upload':f['key'].split('-')[-1],'demo':f.get('demo',False)}
    ids=[g['id']]+g.get('alternate_database_ids',[])
    for game_id in ids:games[str(game_id)]=plan
result={'version':1,'checkedAt':manifest.get('updated_at'),'scope':'Verified public package buttons; runtime revalidates availability','games':games}
(content/'launcher-catalog.json').write_text(json.dumps(result,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
print(json.dumps({'launcherFiles':len(names),'bytes':sum((target/n).stat().st_size for n in names),'catalogGames':len(games),
 'publicPackagePlans':sum(v['kind']=='itch' for v in games.values()),'launcherSha256':digest.hexdigest()},indent=2))
