
#include <cassert>
#include <cstdio>
#include <cstring>
#include "kebab_tcm_wire.h"
struct Stream {uint8_t bytes[129];unsigned offset=0,calls=0,length=0;int fail=0;bool marker=true,code=true,badpad=false;};
static int read(void *c,uint8_t *out,uint32_t n){
 auto &s=*(Stream *)c;s.calls++;assert(n>=4&&n<=64&&s.offset+n-2<=s.length+1);
 if(int(s.calls)==s.fail)return -1;
 out[0]=s.marker?0xa5:0xa4;out[1]=s.code?3:2;
 memcpy(out+2,s.bytes+s.offset,n-2);
 if(s.badpad&&s.offset+n-2==s.length+1)out[n-1]=0;
 s.offset+=n-2;return 0;
}
int main(){
 unsigned cases=0;uint8_t output[131];
 for(unsigned n=0;n<=128;n++){
  Stream s{};s.length=n;for(unsigned i=0;i<n;i++)s.bytes[i]=uint8_t(i*7+9);s.bytes[n]=0x5a;
  memset(output,0xa7,sizeof(output));assert(!kebab_tcm_wire_body(n,output+1,129,read,&s));
  assert(!memcmp(output+1,s.bytes,n)&&output[0]==0xa7&&output[n+1]==0x5a&&output[n+2]==0xa7);
  if(!n)assert(!s.calls);else assert(s.calls);
  for(unsigned error=0;error<4;error++){
   Stream bad{};bad.length=n;memcpy(bad.bytes,s.bytes,n+1);
   bad.marker=error!=0;bad.code=error!=1;bad.fail=error==2?1:0;bad.badpad=error==3;
   int status=kebab_tcm_wire_body(n,output,129,read,&bad);
   if(n&&((error<3)||((n+1)%62!=1)))assert(status!=0);
  }
  Stream shortcap{};shortcap.length=n;memset(output,0xa7,sizeof(output));
  assert(kebab_tcm_wire_body(n,output,n,read,&shortcap)!=0&&!shortcap.calls);
  for(auto b:output)assert(b==0xa7);cases++;
 }
 for(int failure=1;failure<=3;failure++){
  Stream s{};s.length=128;s.bytes[128]=0x5a;s.fail=failure;
  assert(kebab_tcm_wire_body(128,output,129,read,&s)!=0);
 }
 Stream s{};assert(kebab_tcm_wire_body(129,output,131,read,&s)!=0&&!s.calls);
 assert(kebab_tcm_wire_body(1,nullptr,131,read,&s)!=0&&!s.calls);
 assert(kebab_tcm_wire_body(1,output,131,nullptr,&s)!=0&&!s.calls);
 // Synthetic complete application-info record; no hardware payload claim.
 Stream app{};app.length=46;app.bytes[0]=1;app.bytes[12]=128;app.bytes[15]=1;
 app.bytes[32]=0x37;app.bytes[33]=4;app.bytes[34]=0x5f;app.bytes[35]=9;app.bytes[36]=10;app.bytes[40]=16;app.bytes[42]=36;app.bytes[46]=0x5a;
 assert(!kebab_tcm_wire_body(46,output,129,read,&app));kebab_tcm_app_info decoded{};
 assert(kebab_tcm_parse_app_info(output,46,&decoded)==KEBAB_TCM_OK);
 assert(decoded.version==1&&!decoded.status&&decoded.max_x==1079&&decoded.max_y==2399&&decoded.max_objects==10&&decoded.max_touch_report_config_size==128&&decoded.max_touch_report_payload_size==256);
 printf("PASS%u production continued-read lengths, split/omitted-pad boundaries, prefix/pad/read faults, capacity guards and real app-info decoder; I2C model explicit\n",cases);
}
