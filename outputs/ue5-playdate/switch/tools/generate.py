"""Generate the download-only launcher adapter and native SDL theme adapter."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1];out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
lua=(root/'launcher/pdgames.lua').read_text(encoding='utf-8')
(out/'pd_launcher_switch.inc').write_text('/* Generated; edit switch/launcher/pdgames.lua. */\nstatic const char pd_launcher_script[] =\n'+'\n'.join(json.dumps(line+'\n',ensure_ascii=True) for line in lua.splitlines())+';\n',encoding='utf-8')
theme=(root/'../Unreal/Source/CrankLab/CrankTheme.h').read_text(encoding='utf-8')
tokens=re.findall(r'inline FLinearColor (\w+)\(\).*?FColor\((\d+),(\d+),(\d+)\)',theme)
assert len(tokens)==15,'Canonical theme format changed'
(out/'theme.h').write_text('// Generated platform adapter; canonical source: CrankTheme.h\n#pragma once\n#include <SDL2/SDL.h>\nnamespace Theme {\n'+''.join(f'inline constexpr SDL_Color {name}={{{r},{g},{b},255}};\n' for name,r,g,b in tokens)+'}\n')
