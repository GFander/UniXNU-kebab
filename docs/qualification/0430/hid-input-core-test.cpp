#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "kebab_hid_reports.h"
static unsigned mode,mask;static bool specified,defaults;
static bool validRole(unsigned r){return r==1||r==2;}
static bool PE_parse_boot_argn(const char *,void *out,unsigned n){assert(n==4);if(specified)memcpy(out,&mode,4);return specified;}
static bool PE_get_default(const char *,void *out,unsigned n){assert(n==4);if(defaults)memcpy(out,&mask,4);return defaults;}
extern "C" int
kebab_hid_backend_requested(unsigned role)
{
    if (!validRole(role)) return 0;
    uint32_t legacy=0;
    bool specified=PE_parse_boot_argn(role==KEBAB_HID_KEYS?"kebab_keys":"kebab_touch",&legacy,sizeof(legacy));
    if (specified && legacy==0) return 0;
    if (specified && legacy==2) return 1;
    uint32_t mask=0;
    return PE_get_default("kebab,hid-input",&mask,sizeof(mask)) && !(mask&~3u) &&
        (mask&(1u<<(role-1)));
}


int main(){
 unsigned cases=0;
 for(unsigned r=0;r<4;r++)for(unsigned m=0;m<8;m++)for(int sp=0;sp<2;sp++)for(unsigned md:{0u,1u,2u,7u}){
  mask=m;defaults=true;specified=sp;mode=md;
  bool expected=(r==1||r==2) && !(sp&&md==0) && ((sp&&md==2)||(m<4&&(m&(1u<<(r-1)))));
  assert(!!kebab_hid_backend_requested(r)==expected);cases++;
 }
 kebab_hid_queue q{};kebab_hid_frame f{},out{},saved{};
 saved=out;assert(!kebab_hid_queue_claim(&q,0));assert(!kebab_hid_queue_push(&q,1,&f));
 assert(!q.latest_valid&&!q.count);assert(!kebab_hid_queue_pop(&q,1,&out));assert(!memcmp(&out,&saved,sizeof(out)));
 uint8_t bytes[KEBAB_HID_TOUCH_BYTES+2];
 for(unsigned keys=0;keys<8;keys++){
  f={};f.timestamp_ns=keys+1;f.keys=keys;
  assert(kebab_hid_queue_push(&q,1,&f)==2);
  assert(kebab_hid_encode(1,&f,nullptr,bytes,sizeof(bytes))==2);
  assert(bytes[0]==2&&bytes[1]==keys);cases++;
 }
 assert(kebab_hid_queue_claim(&q,99));assert(!kebab_hid_queue_claim(&q,100));
 assert(!kebab_hid_queue_pop(&q,100,&out));assert(!memcmp(&out,&saved,sizeof(out)));
 assert(!kebab_hid_queue_release(&q,100));assert(q.owner==99);
 assert(kebab_hid_queue_pop(&q,99,&out)&&out.keys==7&&out.timestamp_ns==8);
 f.timestamp_ns=9;assert(kebab_hid_queue_push(&q,1,&f)==1&&!q.count&&q.latest.timestamp_ns==9);
 f.timestamp_ns=1;assert(!kebab_hid_queue_push(&q,1,&f)&&q.latest.timestamp_ns==9);
 for(unsigned i=0;i<200;i++){f.timestamp_ns=10+i;f.keys=i&1;assert(kebab_hid_queue_push(&q,1,&f)==2);}
 assert(q.count==64&&q.dropped==136);
 assert(kebab_hid_queue_release(&q,99));assert(kebab_hid_queue_claim(&q,100));
 assert(kebab_hid_queue_pop(&q,100,&out)&&out.keys==1&&out.timestamp_ns==209);
 kebab_hid_frame previous{};previous.timestamp_ns=1;previous.active=1023;
 for(unsigned i=0;i<10;i++){previous.x[i]=100+i;previous.y[i]=200+i;previous.pressure[i]=33;}
 for(unsigned active=0;active<1024;active++){
  f={};f.timestamp_ns=2;f.active=active;
  for(unsigned i=0;i<10;i++){f.x[i]=1124-i;f.y[i]=2435-i;f.pressure[i]=77;}
  memset(bytes,0xa5,sizeof(bytes));
  assert(kebab_hid_encode(2,&f,&previous,bytes,sizeof(bytes))==72);
  assert(bytes[0]==3&&bytes[71]==10&&bytes[72]==0xa5&&bytes[73]==0xa5);
  for(unsigned i=0;i<10;i++){
   const uint8_t *p=bytes+1+7*i;bool down=active&(1u<<i);unsigned x=p[2]|unsigned(p[3])<<8,y=p[4]|unsigned(p[5])<<8;
   assert(p[0]==(down?3:2)&&p[1]==i&&x==(down?f.x[i]:previous.x[i])&&y==(down?f.y[i]:previous.y[i])&&p[6]==(down?77:0));
  }cases++;
 }
 f={};f.timestamp_ns=3;
 assert(kebab_hid_encode(2,&f,nullptr,bytes,sizeof(bytes))==72&&bytes[71]==0);
 memset(bytes,0xa5,sizeof(bytes));assert(!kebab_hid_encode(2,&f,nullptr,bytes,71));
 for(auto x:bytes)assert(x==0xa5);
 f.active=1;f.x[0]=1125;assert(!kebab_hid_encode(2,&f,nullptr,bytes,sizeof(bytes)));
 f.x[0]=0;f.y[0]=2436;assert(!kebab_hid_encode(2,&f,nullptr,bytes,sizeof(bytes)));
 f.y[0]=0;f.active=1024;assert(!kebab_hid_frame_valid(2,&f));
 f.active=0;f.keys=8;assert(!kebab_hid_frame_valid(1,&f));
 printf("PASS %u policy/key/multitouch cases plus queue ownership/overflow/rollback/short-buffer edges; no hardware or HID consumption proof\n",cases);
}
