"""Small generated responses; never downloads or stores model weights."""
import argparse,json,time
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--port',type=int,default=18783);p.add_argument('--log',required=True);a=p.parse_args()
log=Path(a.log);log.parent.mkdir(parents=True,exist_ok=True)
class Handler(BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def log_message(self,*args):pass
    def do_GET(self):
        with log.open('a',encoding='utf-8') as f:f.write(json.dumps({'path':self.path,'time':time.time()})+'\n')
        try:
            if self.path=='/redirect':
                self.send_response(302);self.send_header('Location','/model');self.send_header('Content-Length','0');self.end_headers();return
            size=8*1024*1024 if self.path=='/slow' else 262144
            self.send_response(503 if self.path=='/error' else 200)
            self.send_header('Content-Length',str(size+1 if self.path=='/oversize' else size));self.end_headers()
            count=65536 if self.path=='/short' else size
            for index in range(count//65536):
                self.wfile.write(bytes(range(256))*256);self.wfile.flush()
                if self.path=='/slow':time.sleep(.05)
            self.close_connection=True
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError,OSError):pass
server=ThreadingHTTPServer(('0.0.0.0',a.port),Handler)
print(f'Ready: port {a.port}',flush=True)
try:server.serve_forever()
except KeyboardInterrupt:pass
finally:server.server_close()
