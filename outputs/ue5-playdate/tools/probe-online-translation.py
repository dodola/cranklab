"""Measure the public translation endpoints used by kiss-translator.

Source: fishjar/kiss-translator e7ccd36ac2261928e0e52f56a41531a7b30e0902,
src/apis/trans.js. Public game descriptions only; no model download or API keys.
"""
import argparse,json,time,uuid,urllib.request,urllib.error
from pathlib import Path

SHORT='A puzzle game for Playdate. Turn the crank to rotate the board and find the path. Use the D-pad to move, press A to select and B to go back. Each level introduces a new challenge. Your progress is saved automatically, so you can stop at any time and continue later. Try to complete all levels with as few moves as possible.'
CONTROLS='Controls: Press A to jump and hold B to brake. Turn the crank clockwise to speed up; turn it counterclockwise to slow down. Do not undock the crank during calibration. Double-tap the screen to exit fullscreen. There are 12 levels, 3 difficulty settings, and no in-app purchases. Your progress is saved automatically. An internet connection is only needed to download new games.'
UA='Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/110.0.0.0 Safari/537.36'

def translate(provider,text,timeout=12):
    headers={'Content-Type':'application/json','User-Agent':UA}
    if provider=='tencent':
        url='https://transmart.qq.com/api/imt'
        headers['Referer']='https://transmart.qq.com/zh-CN/index'
        body={'header':{'fn':'auto_translation','client_key':'browser-chrome-110.0.0-Windows-'+str(uuid.uuid4())+'-'+str(int(time.time()*1000))},'type':'plain','model_category':'normal','source':{'text_list':[text],'lang':'en'},'target':{'lang':'zh'}}
    elif provider=='microsoft':
        url='https://edge.microsoft.com/translate/translatetext?from=en&to=zh-Hans&isEnterpriseClient=false';body=[text]
    elif provider=='volcengine':
        url='https://translate.volcengine.com/crx/translate/v1';body={'source_language':'en','target_language':'zh','text':text}
    else:raise ValueError(provider)
    request=urllib.request.Request(url,data=json.dumps(body,ensure_ascii=False).encode(),headers=headers,method='POST')
    start=time.perf_counter()
    with urllib.request.urlopen(request,timeout=timeout) as response:
        raw=response.read(1024*1024+1);assert len(raw)<=1024*1024
        data=json.loads(raw);code=response.status
    return {'provider':provider,'seconds':round(time.perf_counter()-start,3),'http':code,'inputBytes':len(text.encode()),'response':data}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',required=True);p.add_argument('--long-fixture');p.add_argument('--providers',default='tencent,microsoft,volcengine');a=p.parse_args()
    cases={'short':SHORT,'controls':CONTROLS}
    if a.long_fixture:cases['long']=json.loads(Path(a.long_fixture).read_text('utf-8'))['long']
    else:cases['long']=next(c['source'] for c in json.loads(Path(__file__).with_name('online-translation-cases.json').read_text('utf-8')) if c['name']=='long')
    results=[]
    for provider in a.providers.split(','):
        for name,text in cases.items():
            start=time.perf_counter()
            try:result=translate(provider,text)
            except Exception as error:result={'provider':provider,'seconds':round(time.perf_counter()-start,3),'error':str(error)}
            result['case']=name;results.append(result)
            print(json.dumps({k:v for k,v in result.items() if k!='response'},ensure_ascii=False),flush=True)
            Path(a.output).parent.mkdir(parents=True,exist_ok=True)
            Path(a.output).write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8')
