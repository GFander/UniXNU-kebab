#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using IOReturn=uint32_t;
constexpr IOReturn kIOReturnUnsupported=0xe00002c7;
constexpr unsigned kKebabFBSupportedFrameInfo=74;
struct IOExternalMethodArguments {unsigned scalarInputCount,structureInputSize,scalarOutputCount,structureOutputSize;};
static unsigned logs;
#define IOLog(...) (++logs)
static void proc_selfname(char *p,unsigned n){assert(n>=5);memcpy(p,"test",5);}
struct IOMobileFramebufferUserClient {uint64_t fLogged[2]{};void logCall(uint32_t,IOExternalMethodArguments *,IOReturn);};
void
IOMobileFramebufferUserClient::logCall(uint32_t selector,
    IOExternalMethodArguments *args, IOReturn result)
{
	if (selector >= 128) {
		return;
	}
	uint64_t bit = 1ULL << (selector & 63);
	uint64_t *logged = &fLogged[selector >> 6];
	if (!(*logged & bit)) {
		*logged |= bit;
		char name[32];
		proc_selfname(name, sizeof(name));
		IOLog("KEBAB FB: %s sel %u in %u/%u out %u/%u -> 0x%x\n", name, selector,
		    args ? args->scalarInputCount : 0, args ? args->structureInputSize : 0,
		    args ? args->scalarOutputCount : 0, args ? args->structureOutputSize : 0, result);
	}
}

static IOReturn optional(unsigned selector){IOReturn ret=0;switch(selector){
	case kKebabFBSupportedFrameInfo:
		/* Exact 20H392 _kern_SupportedFrameInfo uses scalar index/count
		 * and a 64-byte trace-key string. No trace-key subscription backend
		 * exists here. Never return an invented display-mode structure. */
		ret = kIOReturnUnsupported;
		break;
default:ret=kIOReturnUnsupported;break;}return ret;}
int main(){
 assert(optional(74)==kIOReturnUnsupported);
 IOMobileFramebufferUserClient a,b;IOExternalMethodArguments args{1,0,1,64};
 a.logCall(8,&args,0);assert(logs==1);a.logCall(8,&args,0);assert(logs==1);
 b.logCall(8,&args,0);assert(logs==2);
 a.logCall(74,&args,kIOReturnUnsupported);assert(logs==3);
 b.logCall(74,nullptr,kIOReturnUnsupported);assert(logs==4);
 for(unsigned i:{0u,63u,64u,127u})a.logCall(i,&args,0);
 assert(logs==8);a.logCall(128,&args,0);assert(logs==8);
 puts("PASS actual optional-trace rejection and per-connection logging; host stand-ins, no display proof");
}
