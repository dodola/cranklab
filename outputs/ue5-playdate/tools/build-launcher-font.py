"""Compile pocket-reader's Source Han 20px atlas for the owned PDGames adapter."""
import hashlib,json,struct,zlib,re
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
fonts=root/'translation/fonts/pocket-reader'
atlas=Image.open(fonts/'SourceHanSansCN-M-20px-table-22-22.png').getchannel('A')
assert atlas.size==(1804,1826)
glyphs=[]
for line in (fonts/'SourceHanSansCN-M-20px.fnt').read_text('utf-8').splitlines():
    match=re.fullmatch(r'(.+?)\s+(\d+)\s*',line)
    assert match,line
    name,advance=match.groups();char=' ' if name=='space' else name
    assert len(char)==1,line
    glyphs.append((ord(char),int(advance)))
codes=set(cp for cp,_ in glyphs if cp<131072)
pages={}
for index,(cp,advance) in enumerate(glyphs):
    if cp>=131072:continue # PFT's 512-page map stops at U+1FFFF; keep atlas indexing.
    x,y=(index%82)*22,(index//82)*22
    image=atlas.crop((x,y,x+22,y+22));stride=3;ink=bytearray(stride*22)
    for row in range(22):
        for col in range(22):
            if image.getpixel((col,row))>=128:ink[row*stride+col//8]|=1<<(7-col%8)
    cell=struct.pack('<8H',22,22,stride,0,0,0,0,1)+bytes(b^255 for b in ink)+ink
    glyph=struct.pack('<BBH',advance,0,0)+cell
    pages.setdefault(cp//256,[]).append((cp%256,glyph))
flags=bytearray(64);offsets=[];data=bytearray()
for page,glyphs in sorted(pages.items()):
    flags[page//8]|=1<<(page%8);bits=bytearray(32);ends=[];cells=bytearray()
    for slot,glyph in sorted(glyphs):
        bits[slot//8]|=1<<(slot%8);cells+=glyph;ends.append(len(cells))
    assert len(cells)<65536
    table=struct.pack('<4B',0,0,0,len(glyphs)%256)+bits+b''.join(struct.pack('<H',n) for n in ends)
    table+=bytes((-len(table))%4);data+=table+cells;offsets.append(len(data))
body=struct.pack('<BBH',22,22,0)+flags+b''.join(struct.pack('<I',n) for n in offsets)+data
file=b'Playdate FNT'+struct.pack('<I',0x80000000)+struct.pack('<I',len(body))+bytes(12)+zlib.compress(body,9)
target=root/'Unreal/Content/Games/launcher.pdx/fonts/cranklab-zh.pft';target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(file)
# Extend the bundle with this host asset; preserve every original byte.
bundle=target.parents[1];manifest_path=bundle.parent/'launcher-manifest.json'
names=sorted(p.relative_to(bundle).as_posix() for p in bundle.rglob('*') if p.is_file())
digest=hashlib.sha256()
for name in names:digest.update(name.encode());digest.update((bundle/name).read_bytes())
manifest=json.loads(manifest_path.read_text(encoding='utf-8'));manifest.update(files=names,sha256=digest.hexdigest(),hostAssets=['fonts/cranklab-zh.pft']);manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({'glyphs':len(codes),'pages':len(pages),'bytes':len(file),'sha256':hashlib.sha256(file).hexdigest()}))
