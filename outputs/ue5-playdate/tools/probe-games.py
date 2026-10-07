"""Run real PDX packages with bounded frames; retain logs and framebuffer evidence."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--runtime', required=True)
parser.add_argument('--inventory', required=True)
parser.add_argument('--output', required=True)
parser.add_argument('--frames', type=int, default=180)
parser.add_argument('--script', default='30:a,60:crank,130:right,150:a')
parser.add_argument('--paced', action='store_true')
args = parser.parse_args()
runtime = str(Path(args.runtime).resolve())
output = Path(args.output).resolve()
output.mkdir(parents=True, exist_ok=True)
inventory = json.loads(Path(args.inventory).read_text(encoding='utf-8-sig'))

def probe(game):
    identifier = game['metadata']['bundleID']
    evidence = output / identifier
    evidence.mkdir(exist_ok=True)
    saves = evidence / 'saves'
    saves.mkdir(exist_ok=True)
    framebuffer = evidence / 'frame.pbm'
    if framebuffer.exists():
        framebuffer.unlink()
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', PD_HEADLESS='1',
               PD_MAX_FRAMES=str(args.frames), PD_DUMP_FB=str(args.frames),
               PD_FB_PATH=str(framebuffer), PD_FAST='1', PD_STUB_LOG='1',
               PD_SAVE_DIR=str(saves), PD_SCRIPT=args.script)
    if args.paced:
        env.pop('PD_FAST', None)
    try:
        result = subprocess.run([runtime, game['root']], env=env, cwd=output,
                                capture_output=True, timeout=40)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        code = result.returncode
    except subprocess.TimeoutExpired as exc:
        log = ((exc.stdout or b'') + (exc.stderr or b'')).decode('utf-8', errors='replace')
        code = 'timeout'
    (evidence / 'runtime.log').write_text(log, encoding='utf-8')
    stubs = sorted({line.removeprefix('[stub called] ') for line in log.splitlines()
                    if line.startswith('[stub called] ')})
    report = {'game': game['metadata']['name'], 'exitCode': code,
              'requestedFrames': args.frames, 'script': args.script,
              'framebufferWritten': framebuffer.exists(), 'calledStubs': stubs,
              'log': str(evidence / 'runtime.log'),
              'scope': 'Scripted headless smoke run; visual and complete gameplay compatibility not established.'}
    return report

with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    reports = list(pool.map(probe, inventory['games']))
(output / 'results.json').write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(reports, ensure_ascii=False, indent=2))
