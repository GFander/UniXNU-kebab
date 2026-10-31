"""Save the explicitly requested primary guides and their exact response hashes."""
from pathlib import Path
import concurrent.futures
from html.parser import HTMLParser
import hashlib
import json
import urllib.request

r=Path(__file__).parent/'guides';r.mkdir(exist_ok=True)
paths=['porting/drivers','drivers/overview','drivers/xnu-drivers',
 'drivers/device-tree-matching','porting/platform','porting/ufs','porting/display',
 'porting/input','porting/gpu','porting/power']
class Content(HTMLParser):
    def __init__(self):super().__init__();self.depth=0;self.skip=0;self.rows=[]
    def handle_starttag(self,tag,attrs):
        if tag in ('script','style','svg'):self.skip+=1
        if tag=='main':self.depth+=1
        if self.depth and tag in ('p','div','li','tr','h1','h2','h3','pre'):self.rows.append('\n')
    def handle_endtag(self,tag):
        if tag=='main':self.depth=max(self.depth-1,0)
        if tag in ('script','style','svg'):self.skip=max(self.skip-1,0)
    def handle_data(self,data):
        if self.depth and not self.skip:self.rows.append(data)
def fetch(path):
    url='https://unixnu.com/zh/'+path
    try:
        with urllib.request.urlopen(url,timeout=25) as response:
            data=response.read();final=response.url
        name=path.replace('/','-');(r/(name+'.html')).write_bytes(data)
        parser=Content();parser.feed(data.decode('utf-8'))
        (r/(name+'.txt')).write_text(''.join(parser.rows),encoding='utf-8')
        return dict(requested=url,url=final,sha256=hashlib.sha256(data).hexdigest(),bytes=len(data))
    except Exception as e:return dict(requested=url,error=str(e))
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=list(pool.map(fetch,paths))
(r/'sources.json').write_text(json.dumps(results,indent=2)+'\n')
for result in results:print(result['requested'],result.get('bytes',result.get('error')))
