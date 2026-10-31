from pathlib import Path
import json
r=Path(__file__).parent
a=json.loads((r/'ubuntu-source-hashes-0217.json').read_text())['files'];b=json.loads((r/'mac-source-hashes-0217.json').read_text())['files']
different=[{'path':n,'ubuntu':a.get(n),'mac':b.get(n)} for n in sorted(a.keys()|b.keys()) if a.get(n)!=b.get(n)]
(r/'actual-input-comparison-0217.json').write_text(json.dumps({'scope':'Same explicit source/header/config extension inventory on both build hosts.',
    'matched':sum(a.get(n)==b.get(n) for n in a.keys()|b.keys()),'different':different},indent=2)+'\n')
print('Matched',len(a.keys()|b.keys())-len(different),'different',len(different))
for row in different:print(row)
