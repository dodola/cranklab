"""List the owned demo's files so the packaged UFS copy can be materialized."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]/'Unreal/Content/Games'
fixture=root/'fixture.pdx'
files=sorted(p.relative_to(fixture).as_posix() for p in fixture.rglob('*') if p.is_file())
digest=hashlib.sha256()
for name in files:
    digest.update(name.encode()); digest.update((fixture/name).read_bytes())
(root/'fixture-manifest.json').write_text(json.dumps({'version':1,'sha256':digest.hexdigest(),'files':files},indent=2),encoding='utf-8')
print(f'Fixture manifest: {len(files)} files')
