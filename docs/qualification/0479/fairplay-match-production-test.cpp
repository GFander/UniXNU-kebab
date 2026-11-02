
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define KEBAB 1
#define OSDynamicCast(t,p) dynamic_cast<t *>(p)
struct OSObject{virtual ~OSObject()=default;};
struct OSString:OSObject{const char*s;explicit OSString(const char*p):s(p){}const char*getCStringNoCopy(){return s;}};
static unsigned reads;
struct OSDictionary{OSObject*provider;OSObject*name;OSObject*getObject(const char*k){reads++;return !strcmp(k,"IOProviderClass")?provider:name;}};
static bool enabled;
static unsigned calls,gotkind,gotbound;static int gotresult,gotaux;static char gotname[128];
static bool kebab_fairplay_resource_observer_enabled(){return enabled;}
static void kebab_fairplay_service_evidence(unsigned kind,const char*name,unsigned bound,int result,int aux){calls++;gotkind=kind;gotbound=bound;gotresult=result;gotaux=aux;snprintf(gotname,sizeof(gotname),"%s",name);}
#if defined(KEBAB)
static void
kebab_fairplay_match_evidence(unsigned kind,OSDictionary *dict,int result,unsigned present)
{
 if(!kebab_fairplay_resource_observer_enabled())return;
 OSString *query=dict?OSDynamicCast(OSString,dict->getObject("IOProviderClass")):NULL;
 if(!query&&dict)query=OSDynamicCast(OSString,dict->getObject("IONameMatch"));
 kebab_fairplay_service_evidence(kind,query?query->getCStringNoCopy():"no-string-class",128,result,present!=0);
}
#endif


int main(){OSString provider("FairPlayModel"),name("DeviceName");OSObject notstring;OSDictionary dict{&provider,&name};
 enabled=false;kebab_fairplay_match_evidence(3,(OSDictionary*)1,-1,0);assert(!calls&&!reads);
 enabled=true;kebab_fairplay_match_evidence(3,&dict,-42,1);assert(calls==1&&reads==1&&gotkind==3&&gotbound==128&&gotresult==-42&&gotaux==1&&!strcmp(gotname,"FairPlayModel"));
 dict.provider=NULL;reads=0;kebab_fairplay_match_evidence(5,&dict,0,1);assert(reads==2&&gotkind==5&&gotresult==0&&!strcmp(gotname,"DeviceName"));
 dict.provider=&notstring;kebab_fairplay_match_evidence(3,&dict,-5,0);assert(gotaux==0&&!strcmp(gotname,"DeviceName"));
 dict.name=&notstring;kebab_fairplay_match_evidence(3,&dict,-6,0);assert(gotresult==-6&&!strcmp(gotname,"no-string-class"));
 reads=0;kebab_fairplay_match_evidence(5,NULL,-7,0);assert(!reads&&gotresult==-7&&gotaux==0&&!strcmp(gotname,"no-string-class"));
 enabled=false;unsigned before=calls;kebab_fairplay_match_evidence(3,&dict,0,1);assert(calls==before);
 puts("PASS7 matching dictionary class-name observations; IOKit objects modeled");}
