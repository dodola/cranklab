"""Loopback fixture for UE's real async HTTP, fallback and cancellation paths."""
import argparse,json,re,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
p=argparse.ArgumentParser();p.add_argument('--case',default='controls');p.add_argument('--cases',default=str(Path(__file__).with_name('online-translation-cases.json')));p.add_argument('--port',type=int,default=18791);p.add_argument('--mode',choices=['normal','fallback','marker-spacing','error','cancel','malformed','extra-item','missing-marker'],default='normal');p.add_argument('--log',required=True);a=p.parse_args()
source=('ⒶⒷ Playdate D-pad 🚀 '*450) if a.case=='dense' else next(c['source'] for c in json.loads(Path(a.cases).read_text('utf-8')) if c['name']==a.case)
requests=[]
class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def send(self,code,body):
        raw=json.dumps(body,ensure_ascii=False).encode();self.send_response(code);self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
    def do_GET(self):
        try:self.send(200,dict(id=1058,title='Online translation fixture',description=source))
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
    def do_POST(self):
        try:
            body=json.loads(self.rfile.read(int(self.headers['Content-Length'])));provider=self.path.strip('/')
            requests.append(provider);Path(a.log).write_text(json.dumps(requests),'utf-8')
            if a.mode=='cancel':time.sleep(2)
            if a.mode=='error' or (a.mode in ('fallback','marker-spacing') and provider=='microsoft'):self.send(503,dict(error='fixture unavailable'));return
            texts=body if provider=='microsoft' else body['source']['text_list']
            assert all(len(t.encode('utf-16-le'))//2<=3500 for t in texts), 'Oversize protected item'
            output=['测试译文：'+text for text in texts]
            if a.mode=='marker-spacing':output=[re.sub(r'(ZXQK\w*?)(\d+)(QXZ)',r'\1 \2 \3',t) for t in output]
            if a.mode=='malformed':output=[]
            if a.mode=='missing-marker':output=['测试译文：已丢失受保护的按键']*len(texts)
            if provider=='microsoft':
                items=[dict(translations=[dict(text=t,to='zh-Hans')]) for t in output]
                if a.mode=='extra-item':items.append(dict(unexpected=True))
                self.send(200,items)
            else:
                if a.mode=='extra-item':output.append(123)
                self.send(200,dict(header=dict(ret_code='succ'),auto_translation=output))
        except (BrokenPipeError,ConnectionResetError,ConnectionAbortedError):pass
server=ThreadingHTTPServer(('127.0.0.1',a.port),Handler);print('Ready',a.port,a.mode,flush=True)
try:server.serve_forever()
except KeyboardInterrupt:pass
finally:server.server_close()
