
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "kebab_tcm_wire.h"
struct Sink {std::vector<uint8_t> bytes;unsigned calls=0,fail=0;};
static int write(void *c,const uint8_t *p,uint32_t n){
 auto &s=*(Sink *)c;assert(n>=2&&n<=64);s.calls++;
 if(s.calls==s.fail)return -1;
 if(s.bytes.empty()){assert(p[0]==0x26);s.bytes.insert(s.bytes.end(),p,p+n);}
 else{assert(p[0]==1);s.bytes.insert(s.bytes.end(),p+1,p+n);}return 0;
}
int main(){
 uint8_t command[131],payload[128];for(unsigned i=0;i<128;i++)payload[i]=uint8_t(i*11+1);
 for(unsigned n=0;n<=128;n++){
  unsigned bytes=kebab_tcm_build_command(command,131,0x26,payload,n);assert(bytes==n+3);
  Sink s{};assert(!kebab_tcm_wire_command(command,bytes,write,&s));
  unsigned chunks=(n+2+62)/63;assert(s.calls==chunks&&s.bytes.size()==bytes&&!memcmp(s.bytes.data(),command,bytes));
  for(unsigned fail=1;fail<=chunks;fail++){Sink bad{};bad.fail=fail;assert(kebab_tcm_wire_command(command,bytes,write,&bad)!=0&&bad.calls==fail);}
 }
 Sink s{};assert(kebab_tcm_wire_command(nullptr,3,write,&s)!=0&&!s.calls);
 assert(kebab_tcm_wire_command(command,3,nullptr,&s)!=0&&!s.calls);
 assert(kebab_tcm_wire_command(command,2,write,&s)!=0&&!s.calls);
 command[1]=9;command[2]=0;assert(kebab_tcm_wire_command(command,4,write,&s)!=0&&!s.calls);
 std::puts("PASS129 complete command payload sizes,64-byte frames, exact reconstruction, all per-chunk failure stops and malformed length guards; I2C explicit sink");
}
