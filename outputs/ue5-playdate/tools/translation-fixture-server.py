"""Loopback-only real-file transfer fixture for the UE translation verifier."""
import argparse,json,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--model',required=True);p.add_argument('--detail',required=True);p.add_argument('--port',type=int,default=18781);a=p.parse_args()
model=Path(a.model).resolve();detail=json.loads(Path(a.detail).read_text('utf-8-sig'));detail['id']=1058
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        try:
            if self.path=='/game/1058.json':body=json.dumps(detail,ensure_ascii=False).encode()
            elif self.path=='/short.gguf':body=b'GGUF'
            elif self.path in ('/model.gguf','/slow.gguf'):
                self.send_response(200);self.send_header('Content-Length',str(model.stat().st_size));self.end_headers()
                with model.open('rb') as f:
                    while block:=f.read(65536):
                        self.wfile.write(block)
                        if self.path=='/slow.gguf':time.sleep(.02)
                return
            else:self.send_error(404);return
            self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
server=ThreadingHTTPServer(('127.0.0.1',a.port),Handler)
print(f'Ready: http://127.0.0.1:{a.port}',flush=True)
try:server.serve_forever()
except KeyboardInterrupt:pass
finally:server.server_close()
