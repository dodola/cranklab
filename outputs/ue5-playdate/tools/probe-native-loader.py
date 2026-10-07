"""Reject damaged real PDX binaries and recover in the same hosted core."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import struct
import time
import zlib


def main(args):
    os.environ.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", PD_HEADLESS="1")
    library=Path(args.library).resolve()
    with os.add_dll_directory(str(library.parent)):
        core=ctypes.CDLL(str(library))
    core.crank_start.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
    core.crank_last_error.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    source=Path(args.game).resolve();output=Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=True)
    data=(source/"pdex.bin").read_bytes()
    code_size=struct.unpack_from("<I",data,32)[0]
    decoded=bytearray(zlib.decompress(data[48:]))
    assert struct.unpack_from("<I",data,44)[0]>0
    def field(offset,value):
        changed=bytearray(data);struct.pack_into("<I",changed,offset,value);return bytes(changed)
    invalid_relocation=decoded[:]
    struct.pack_into("<I",invalid_relocation,code_size,code_size+4)
    cases={
        "short":(data[:20],"file size"),
        "signature":(b"X"+data[1:],"signature"),
        "oversized":(field(32,0xffffffff),"header lengths"),
        "entry":(field(40,code_size+4),"entry bounds"),
        "compressed":(data[:48]+b"broken-payload","compressed"),
        "relocation":(data[:48]+zlib.compress(invalid_relocation),"relocation offset"),
    }
    def error():
        buffer=ctypes.create_string_buffer(2048)
        core.crank_last_error(buffer,len(buffer));return buffer.value.decode(errors="replace")
    def start(game,status):
        assert core.crank_start(str(game).encode(),str(output/"saves").encode())
        deadline=time.monotonic()+15
        while core.crank_status() not in (status,3) and time.monotonic()<deadline:
            time.sleep(.01)
        assert core.crank_status()==status,error()
    reports=[]
    for name,(content,message) in cases.items():
        target=output/name
        shutil.copytree(source,target,dirs_exist_ok=True)
        (target/"pdex.bin").write_bytes(content)
        try:
            start(target,3)
            diagnostic=error();assert message in diagnostic,diagnostic
            reports.append({"case":name,"error":diagnostic,"passed":True})
        finally:
            core.crank_stop()
        try:
            start(source,2)
            assert not error(),error()
        finally:
            core.crank_stop()
    if args.unsupported_game:
        try:
            start(Path(args.unsupported_game).resolve(),3)
            diagnostic=error()
            assert diagnostic=="Unimplemented C API: graphics.copyBitmap",diagnostic
            reports.append({"case":"unsupported-api","error":diagnostic,"passed":True})
        finally:
            core.crank_stop()
        try:
            start(source,2)
            assert not error(),error()
        finally:
            core.crank_stop()
    report={"passed":True,"sameProcessRecoveryCount":len(reports),"cases":reports}
    (output/"results.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--library",required=True)
    parser.add_argument("--game",required=True)
    parser.add_argument("--output",required=True)
    parser.add_argument("--unsupported-game")
    main(parser.parse_args())
