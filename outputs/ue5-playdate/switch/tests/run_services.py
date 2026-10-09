"""Owned packages and local TLS fixture; does not touch a user's library/account."""
import argparse, http.server, io, json, os, shutil, ssl, struct, subprocess, tempfile, threading, time, zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--executable',required=True);p.add_argument('--output',required=True);a=p.parse_args()
out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
# A fresh owned directory makes repeated runs independent without recursive deletes.
root=Path(tempfile.mkdtemp(prefix='services-',dir=out));fixtures=root/'fixtures';fixtures.mkdir()
base={'Fixture.pdx/pdxinfo':b'name=Fixture\nbundleID=local.switch.download\n','Fixture.pdx/main.lua':b'function playdate.update() playdate.graphics.clear(1) end\n'}
expected={}
def package(name,entries,valid=False,compression=zipfile.ZIP_DEFLATED):
    with zipfile.ZipFile(fixtures/(name+'.zip'),'w',compression=compression) as z:
        for path,body in entries:z.writestr(path,body)
    expected[name]=valid
package('good',list(base.items()),True)
package('stored',list(base.items()),True,zipfile.ZIP_STORED)
package('root',[(k.split('/',1)[1],v) for k,v in base.items()],True)
package('native',list(base.items())+[('Fixture.pdx/pdex.bin',b'ARM code')])
package('traversal',list(base.items())+[('../escape',b'bad')])
package('absolute',list(base.items())+[('/escape',b'bad')])
package('backslash',list(base.items())+[('Fixture.pdx/..\\escape',b'bad')])
package('duplicate',list(base.items())+[('Fixture.pdx/MAIN.LUA',b'duplicate')])
package('two-bundles',list(base.items())+[('Other.pdx/pdxinfo',b'name=Other')])
package('no-lua',[('Fixture.pdx/pdxinfo',b'name=Fixture')])
package('symlink',list(base.items()))
with zipfile.ZipFile(fixtures/'symlink.zip','a') as z:
    info=zipfile.ZipInfo('Fixture.pdx/link');info.create_system=3;info.external_attr=(0o120777<<16);z.writestr(info,b'/tmp')
package('corrupt',list(base.items()),compression=zipfile.ZIP_STORED)
data=bytearray((fixtures/'corrupt.zip').read_bytes());data[30+len('Fixture.pdx/pdxinfo')+4]^=255;(fixtures/'corrupt.zip').write_bytes(data)
package('encrypted',list(base.items()))
data=bytearray((fixtures/'encrypted.zip').read_bytes());pos=data.index(b'PK\x01\x02');data[pos+8]|=1;(fixtures/'encrypted.zip').write_bytes(data)
package('oversized',list(base.items()))
data=bytearray((fixtures/'oversized.zip').read_bytes());pos=data.index(b'PK\x01\x02');struct.pack_into('<I',data,pos+24,300*1024*1024);(fixtures/'oversized.zip').write_bytes(data)
package('slow',list(base.items())+[('Fixture.pdx/padding',os.urandom(256*1024))],True,zipfile.ZIP_STORED)
(fixtures/'archives.json').write_text(json.dumps(expected))
subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost',
    '-keyout',str(root/'key.pem'),'-out',str(fixtures/'cert.pem')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
counts={}
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        counts[self.path]=counts.get(self.path,0)+1
        if self.path=='/fail':self.send_error(503);return
        file=fixtures/self.path.removeprefix('/')
        if not file.is_file():self.send_error(404);return
        data=file.read_bytes();self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
        try:
            for i in range(0,len(data),4096):
                self.wfile.write(data[i:i+4096]);self.wfile.flush()
                if self.path=='/slow.zip':time.sleep(.025)
        except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
    def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(str(fixtures/'cert.pem'),str(root/'key.pem'));server.socket=ctx.wrap_socket(server.socket,server_side=True)
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    subprocess.run([a.executable,str(fixtures),f'https://localhost:{server.server_port}',str(root/'data')],check=True)
    assert counts.get('/good.zip')==2,counts # Duplicate play does not create a second transfer.
    report={'passed':True,'httpsRequests':counts,'fixtures':str(root),'tests':list(expected)}
    (out/'services-results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
finally:
    print('Local TLS request counts:',counts,flush=True)
    server.shutdown();server.server_close()
