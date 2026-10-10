"""Stage the user's two local Lua games; packages remain ignored by Git."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--mahjong', type=Path, default=Path('C:/Users/dodol/Downloads/PoohMiniMahjong-1.0.pdx/PoohMiniMahjong.pdx'))
p.add_argument('--music', type=Path, default=Path('C:/Users/dodol/Downloads/music_box_1.1.4.pdx/music_box_1.1.4.pdx'))
args = p.parse_args()
games = []
for key, title, author, source in [('mahjong', 'Pooh Mini Mahjong', 'Pooh', args.mahjong), ('music', 'Music Box', 'ambierambier', args.music)]:
    if not (source / 'main.pdz').is_file() or not (source / 'pdxinfo').is_file():
        raise SystemExit(f'Invalid game directory: {source}')
    native = source / 'pdex.bin'
    if native.exists() and native.stat().st_size:
        raise SystemExit(f'The web build requires a Lua game: {source}')
    target = root / 'public/games' / key
    target.mkdir(parents=True, exist_ok=True)
    entries = []
    digest = hashlib.sha256()
    for file in sorted(source.rglob('*')):
        if not file.is_file() or file.suffix.lower() in ('.dll', '.dylib'):
            continue
        name = file.relative_to(source).as_posix()
        data = file.read_bytes()
        digest.update(name.encode() + b'\0' + hashlib.sha256(data).digest())
        (target / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target / name)
        entries.append({'path': name, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    info = dict(line.split('=', 1) for line in (source / 'pdxinfo').read_text(encoding='utf-8-sig').splitlines() if '=' in line)
    manifest = {'id': key, 'title': info.get('name', title), 'author': info.get('author', author), 'bundle': info.get('bundleID', key), 'digest': digest.hexdigest(), 'files': entries}
    (target / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False), encoding='utf-8')
    games.append({k: v for k, v in manifest.items() if k != 'files'})
    print(f'{key}: {len(entries)} files, {sum(e["size"] for e in entries)} bytes')
(root / 'public/games/catalog.json').write_text(json.dumps(games, ensure_ascii=False), encoding='utf-8')
