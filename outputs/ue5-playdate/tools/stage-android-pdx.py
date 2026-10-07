"""Stage a diagnostic PDX in a debuggable Android app's private files."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import uuid

parser=argparse.ArgumentParser()
parser.add_argument('--adb',required=True)
parser.add_argument('--device',required=True)
parser.add_argument('--game',required=True)
parser.add_argument('--package',default='com.cranklab.emulator')
args=parser.parse_args()
game=Path(args.game).resolve()
assert game.is_dir() and (game/'pdxinfo').is_file(),game
token=uuid.uuid4().hex
temporary='/data/local/tmp/crank-stage-'+token
remote=f'/data/user/0/{args.package}/files/CrankLab/Diagnostics/{token}.pdx'
base=[args.adb,'-s',args.device]
def adb(*command):
    result=subprocess.run(base+list(command),capture_output=True,timeout=60)
    if result.returncode:raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
    return result.stdout
adb('push',str(game),temporary)
try:
    adb('shell','run-as '+shlex.quote(args.package)+' mkdir -p '+shlex.quote(remote))
    adb('shell','run-as '+shlex.quote(args.package)+' cp -R '+shlex.quote(temporary+'/.')+' '+shlex.quote(remote))
finally:
    # Exact temporary directory authored above; never remove a supplied path.
    adb('shell','rm','-rf',temporary)
print(json.dumps({'device':args.device,'package':args.package,'game':remote}))
