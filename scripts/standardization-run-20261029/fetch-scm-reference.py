"""Fetch fixed upstream reference text; no credentials, checkout or execution."""
from pathlib import Path
import hashlib,json,urllib.request
r=Path('/home/albert/.cache/hios-scm-reference-v6.12');r.mkdir(exist_ok=True)
paths=['drivers/firmware/qcom/qcom_scm-smc.c','drivers/firmware/qcom/qcom_scm.h',
 'drivers/firmware/qcom/qcom_scm.c','include/linux/arm-smccc.h',
 'arch/arm64/boot/dts/qcom/sm8250.dtsi']
report={}
for p in paths:
    url='https://raw.githubusercontent.com/torvalds/linux/v6.12/'+p
    f=r/Path(p).name
    if not f.exists():f.write_bytes(urllib.request.urlopen(url,timeout=45).read())
    b=f.read_bytes();report[p]={'url':url,'sha256':hashlib.sha256(b).hexdigest(),'bytes':len(b)}
    (r/'sources.json').write_text(json.dumps(report,indent=2)+'\n')
    print(p,len(b),report[p]['sha256'],flush=True)
(r/'sources.json').write_text(json.dumps(report,indent=2)+'\n')
