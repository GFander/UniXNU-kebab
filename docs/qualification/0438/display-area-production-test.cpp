#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <initializer_list>
using IOReturn=uint32_t;using IOByteCount=uint64_t;
static constexpr IOReturn kIOReturnSuccess=0,kIOReturnNoSpace=0xe00002db,kIOReturnVMError=0xe00002d0,kIOReturnBadArgument=0xe00002c2;
enum{kKebabFBGetDisplayArea=29,KEBAB_FB_DOT_PITCH=458};
struct Descriptor{std::array<uint8_t,24>data;unsigned length;bool fail=false;uint64_t getLength(){return length;}uint64_t writeBytes(unsigned o,const void*p,unsigned n){assert(!o&&n<=length&&n<=data.size());if(fail)return 0;memcpy(data.data(),p,n);return n;}};
struct IOExternalMethodArguments{Descriptor *structureOutputDescriptor=nullptr;unsigned structureOutputDescriptorSize=0;void *structureOutput=nullptr;unsigned structureOutputSize=0;};
struct Framebuffer{uint32_t fLogicalW,fLogicalH;};
/* Exact IOMobileGraphicsFamily selector29: two binary32 dimensions in
 * height/width order, each divided by the same reported dot-pitch as sel28.
 * Only exactly representable positive integer inputs are accepted. Current
 * GOP/logical geometry and density are far inside this range. No kernel FP. */
#ifndef KEBAB_FRAMEBUFFER_GEOMETRY_H
#define KEBAB_FRAMEBUFFER_GEOMETRY_H
#include <stdint.h>

static inline uint32_t
kebab_fb_ratio_f32(uint32_t numerator, uint32_t denominator)
{
    uint64_t a = numerator, b = denominator;
    int exponent = 0;
    while (a < b) { a <<= 1; --exponent; }
    while (a >= b * 2) { b <<= 1; ++exponent; }
    uint64_t scaled = a << 23;
    uint64_t significand = scaled / b, remainder = scaled % b;
    if (remainder * 2 > b || (remainder * 2 == b && (significand & 1))) ++significand;
    if (significand == (1ULL << 24)) { significand >>= 1; ++exponent; }
    return ((uint32_t)(exponent + 127) << 23) | ((uint32_t)significand & 0x7fffffU);
}

static inline bool
kebab_fb_display_area(uint32_t width, uint32_t height, uint32_t pitch, uint32_t output[2])
{
    if (output == 0 || width == 0 || height == 0 || pitch == 0 ||
        width > (1U << 24) || height > (1U << 24) || pitch > (1U << 24)) return false;
    output[0] = kebab_fb_ratio_f32(height, pitch);
    output[1] = kebab_fb_ratio_f32(width, pitch);
    return true;
}
#endif

static IOReturn
kebab_fb_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		IOByteCount size = args->structureOutputDescriptor->getLength();
		if (size < len) {
			return kIOReturnNoSpace;
		}
		if (args->structureOutputDescriptor->writeBytes(0, buf, len) != len) {
			return kIOReturnVMError;
		}
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || args->structureOutputSize < len) {
		return kIOReturnNoSpace;
	}
	bzero(args->structureOutput, args->structureOutputSize);
	memcpy(args->structureOutput, buf, len);
	return kIOReturnSuccess;
}

static IOReturn invoke(IOExternalMethodArguments *args,uint32_t w,uint32_t h){Framebuffer fb{w,h};auto fFB=&fb;IOReturn ret=0;switch(29){
	case kKebabFBGetDisplayArea: {
		/* Exact donor selector29/virtual+0x5e8 returns two Float32 values,
		 * height then width, divided by the reported dot-pitch. These use
		 * this port's existing logical display profile, not a newly measured
		 * panel size. Encode with integer arithmetic; no kernel FP state. */
		uint32_t area[2];
		if (!kebab_fb_display_area(fFB->fLogicalW, fFB->fLogicalH,
		    KEBAB_FB_DOT_PITCH, area)) {
			ret = kIOReturnBadArgument;
			break;
		}
		ret = kebab_fb_struct_out(args, area, (uint32_t)sizeof(area));
		break;
	}
default:assert(false);}return ret;}
static uint32_t bits(float f){uint32_t u;memcpy(&u,&f,4);return u;}
static void compare(uint32_t w,uint32_t h,uint32_t p){uint32_t out[2];assert(kebab_fb_display_area(w,h,p,out));assert(out[0]==bits(float(h)/float(p)));assert(out[1]==bits(float(w)/float(p)));}
int main(){
 for(unsigned w:{1u,2u,3u,1080u,1125u,2400u,2436u,65535u,16777215u,16777216u})for(unsigned h:{1u,2u,2436u,16777216u})for(unsigned p:{1u,3u,395u,458u,65535u,16777215u,16777216u})compare(w,h,p);
 uint32_t state=0x83fe12;for(unsigned i=0;i<100000;i++){state=state*1664525u+1013904223u;uint32_t w=(state&0xffffff)+1;state=state*1664525u+1013904223u;uint32_t h=(state&0xffffff)+1;state=state*1664525u+1013904223u;uint32_t p=(state&0xffffff)+1;compare(w,h,p);}
 uint32_t out[2]={0x12345678,0x87654321};
 for(auto in:{std::array<uint32_t,3>{0,2436,458},{1125,0,458},{1125,2436,0},{16777217,2436,458},{1125,16777217,458},{1125,2436,16777217}}){assert(!kebab_fb_display_area(in[0],in[1],in[2],out));assert(out[0]==0x12345678&&out[1]==0x87654321);}assert(!kebab_fb_display_area(1125,2436,458,nullptr));
 std::array<uint8_t,24> bytes;IOExternalMethodArguments a;a.structureOutput=bytes.data();
 for(unsigned n=0;n<8;n++){bytes.fill(0xa5);a.structureOutputSize=n;assert(invoke(&a,1125,2436)==kIOReturnNoSpace);for(auto v:bytes)assert(v==0xa5);}
 for(unsigned n:{8u,16u,24u}){bytes.fill(0xa5);a.structureOutputSize=n;assert(!invoke(&a,1125,2436));memcpy(out,bytes.data(),8);assert(out[0]==bits(float(2436)/458)&&out[1]==bits(float(1125)/458));for(unsigned i=8;i<n;i++)assert(!bytes[i]);for(unsigned i=n;i<bytes.size();i++)assert(bytes[i]==0xa5);}
 Descriptor d;d.data.fill(0xa5);a.structureOutputDescriptor=&d;
 for(unsigned n=0;n<8;n++){d.length=n;assert(invoke(&a,1125,2436)==kIOReturnNoSpace);for(auto v:d.data)assert(v==0xa5);}
 d.length=8;d.fail=true;assert(invoke(&a,1125,2436)==kIOReturnVMError);d.fail=false;assert(!invoke(&a,1125,2436)&&a.structureOutputDescriptorSize==8);memcpy(out,d.data.data(),8);assert(out[0]==bits(float(2436)/458)&&out[1]==bits(float(1125)/458));for(unsigned i=8;i<d.data.size();i++)assert(d.data[i]==0xa5);
 puts("PASS actual display-area helper/case/output:100280 rational-vs-Float32 comparisons, exactheight/width order,8B output, inline/descriptor bounds and failures; descriptor hardware explicitly modeled");
}
