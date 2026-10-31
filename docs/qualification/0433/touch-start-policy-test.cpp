#include <cassert>
#include <cstdio>
#include <initializer_list>
static bool specified,requested;static unsigned argument;
#define KEBAB_HID_TOUCH 2u
static bool PE_parse_boot_argn(const char *,void *out,unsigned bytes){assert(bytes==4);if(specified)*(unsigned*)out=argument;return specified;}
static bool kebab_hid_backend_requested(unsigned role){assert(role==2);return requested;}
static unsigned policy(){unsigned boot=0,value=0;
	if (PE_parse_boot_argn("kebab_touch_boot", &value, sizeof(value))) {
		boot = value;
	} else if (kebab_hid_backend_requested(KEBAB_HID_TOUCH)) {
		boot = 1;
	}
return boot;}
int main(){for(bool sp:{false,true})for(bool rq:{false,true})for(unsigned val:{0u,1u,7u}){specified=sp;requested=rq;argument=val;assert(policy()==(sp?val:rq?1u:0u));}printf("PASS12 actual selected-touch boot policy cases; explicit0 preserved, no chip or HID readiness claim\n");}
