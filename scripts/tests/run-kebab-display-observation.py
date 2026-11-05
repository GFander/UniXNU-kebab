"""Compile authored SM8250 observation against sealed local Lineage facts."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess,tempfile,os
os.environ['TMPDIR']='/dev/shm'
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
manifest=json.loads((a.reference/'reference-manifest.json').read_text());assert manifest['head']=='7c2b1aedb5573d140684a9ecc918ce22af5f0817'
for name,meta in manifest['files'].items():assert hashlib.sha256((a.reference/name).read_bytes()).hexdigest()==meta['sha256']
dt=(a.reference/'arch/arm64/boot/dts/vendor/qcom/kona-sde.dtsi').read_text()
def prop(name):return [int(v,0)for v in re.search(re.escape(name)+r'\s*=\s*<([^>]+)>',dt)[1].split()]
intf=prop('qcom,sde-intf-off')[1:3];sspp=prop('qcom,sde-sspp-off');assert len(intf)==2 and len(sspp)==8
def define(file,name):return int(re.search(r'^#define\s+'+name+r'\s+(0x[0-9a-fA-F]+)',(a.reference/file).read_text(),re.M)[1],0)
idf='techpack/display/msm/sde/sde_hw_intf.c';sdf='techpack/display/msm/sde/sde_hw_sspp.c'
offsets=[define(idf,n) for n in ('INTF_STATUS','INTF_TIMING_ENGINE_EN','INTF_FRAME_LINE_COUNT_EN','INTF_DSI_CMD_MODE_TRIGGER_EN','INTF_FRAME_COUNT','INTF_LINE_COUNT','INTF_TEAR_INT_COUNT_VAL','INTF_TEAR_LINE_COUNT')]
sources=[define(sdf,n)for n in ('SSPP_SRC_SIZE','SSPP_SRC0_ADDR','SSPP_SRC_YSTRIDE0','SSPP_SRC_FORMAT')]
expected=[0,prop('qcom,sde-off')[0],prop('qcom,sde-off')[0]+0x10,prop('qcom,sde-off')[0]+0x14]+[base+off for base in intf for off in offsets]+[base+off for base in sspp for off in sources]
test='''
#include "KebabDisplayObservation.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const size_t allowed[]={OFFSETS};
static unsigned calls,failAt,mdpReads,mode,change;
static uint32_t gdsc,ahb,core;
static int read32(void *cookie,unsigned region,size_t offset,uint32_t *out){
 assert(cookie==&calls);calls++;if(calls==failAt)return 1;
 if(!region){assert(offset==0x3000 || offset==0x2080 || offset==0x200c);*out=offset==0x3000?gdsc:offset==0x2080?ahb:core;return 0;}
 unsigned i;for(i=0;i<sizeof allowed/sizeof *allowed;i++)if(allowed[i]==offset)break;
 assert(i<sizeof allowed/sizeof *allowed);mdpReads++;*out=(uint32_t)offset;
 if(offset==0x6b800+0x26c && mode)*out^=(change++&1);
 if(offset==0x6b800+0xac)*out+=calls; /* natural counter changes */
 return 0;
}
static void reset(void){calls=failAt=mdpReads=mode=change=ahb=core=0;gdsc=0x80000000u;}
static void empty(KebabDisplayObservation *s){const unsigned char *p=(void*)s;for(size_t i=0;i<sizeof *s;i++)assert(!p[i]);}
int main(void){
 KebabDisplayReadIo io={&calls,{0x20000,0x84208},read32};KebabDisplayObservation s;KebabDisplayReadResult r;
 reset();assert(!KebabDisplayObserve(&io,&s,&r));assert(r.reads==116 && calls==116 && mdpReads==104);assert(s.intf[0][4]!=0 && s.source[7][1]==0x2b014);
 for(unsigned f=1;f<=116;f++){reset();failAt=f;memset(&s,0xa5,sizeof s);assert(KebabDisplayObserve(&io,&s,&r)==KebabDisplayReadIoError && r.reads==f);empty(&s);}
 for(unsigned n=0;n<4;n++){reset();if(n==0)gdsc=0;if(n==1)gdsc|=1;if(n==2)ahb=0x80000000u;if(n==3)core=0x80000000u;assert(KebabDisplayObserve(&io,&s,&r)==KebabDisplayReadPowerUnavailable && !mdpReads);empty(&s);}
 reset();mode=1;assert(KebabDisplayObserve(&io,&s,&r)==KebabDisplayReadUnstable && r.attempts==3 && calls==348);empty(&s);
 reset();io.bytes[0]=0x3003;assert(KebabDisplayObserve(&io,&s,&r)==KebabDisplayReadArgument && !calls);io.bytes[0]=0x20000;
 for(unsigned sid=0;sid<65536;sid++)assert(!!KebabDisplayNonsecureSid(sid)==(sid==0x820 || sid==0x822 || sid==0xc20 || sid==0xc22));
 assert(!KebabDisplayNonsecureSid(0x10000820));
 const uint64_t granules[]={4096,16384,65536};
 for(unsigned g=0;g<3;g++){uint64_t z=granules[g];assert(KebabDisplayIovaSpan(0x20000,z,z));assert(KebabDisplayIovaSpan(0x100000000ULL-z,z,z));assert(!KebabDisplayIovaSpan(0x100000000ULL-z,2*z,z));assert(!KebabDisplayIovaSpan(0x20001,z,z));assert(!KebabDisplayIovaSpan(0x20000,0,z));assert(!KebabDisplayIovaSpan(0,z,z));assert(!KebabDisplayIovaSpan(UINT64_MAX-z+1,z,z));}
 assert(!KebabDisplayIovaSpan(0x20000,4096,8192));
 puts("PASS local-source-bound SM8250:116 read faults,4 power/clock refusals,3 unstable retries,65536 SID memberships and IOVA granule/bounds; software callbacks only");
}
'''.replace('OFFSETS',','.join(hex(x)for x in expected))
source=(a.repo/'package/KebabDisplayObservation.c').read_text();header=(a.repo/'package/KebabDisplayObservation.h').read_text()
variants=[('production',source,True),('wrong-SoC-intf',source.replace('0x6b800,0x6c000','0x35000,0x36000'),False),('ignore-GDSC',source.replace('!(s->gdsc&0x80000000u)','0'),False),('ignore-clock-halt',source.replace('(s->ahb&0x80000000u)','0'),False),('secure-SID-allowed',source.replace('(sid&~0x402u)==0x820u','(sid&~0x403u)==0x820u'),False),('ignore-IOVA-overrun',source.replace('bytes<=0x100000000ULL-address','1'),False),('ignore-mode-change',source.replace('if((a->intf[i][0]^b->intf[i][0])&1)return 0;','/* dropped */'),False)]
results=[]
with tempfile.TemporaryDirectory(dir='/dev/shm',prefix='kebab-sm8250-observe-')as folder:
 w=Path(folder);(w/'KebabDisplayObservation.h').write_text(header);(w/'test.c').write_text(test)
 for name,body,ok in variants:
  assert ok or body!=source;(w/'actual.c').write_text(body)
  subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(w/'actual.c'),str(w/'test.c'),'-o',str(w/'test')],check=True)
  q=subprocess.run([str(w/'test')],capture_output=True,text=True,timeout=15);assert(q.returncode==0)==ok,(name,q.stdout,q.stderr)
  if not ok:assert'Assertion'in q.stderr
  results.append(dict(name=name,compiled=True,returncode=q.returncode));print(q.stdout.strip()if ok else'Rejected compiled behavior: '+name,flush=True)
receipt=dict(passed=True,scope='Authored production observation and SID/IOVA checks with synthetic callback memory, not hardware access or DMA mapping',source_sha256=hashlib.sha256(source.encode()).hexdigest(),header_sha256=hashlib.sha256(header.encode()).hexdigest(),reference_head=manifest['head'],reference_manifest_sha256=hashlib.sha256((a.reference/'reference-manifest.json').read_bytes()).hexdigest(),results=results,native_build=False,hardware_qualified=False)
with a.output.open('x')as f:json.dump(receipt,f,indent=2);f.write('\n')
