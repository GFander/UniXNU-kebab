// SPDX-License-Identifier: MIT
// HOST callback model only. No IOKit connection, GPU, sandbox or physical fence.
#include "NativeAdrenoClient.h"
#include <IOKit/IOReturn.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
static_assert(NativeAdrenoAbiVersion == 2);
static_assert(NativeAdrenoAbiMaxBuffers == 128);
static_assert(sizeof(NativeAdrenoAbiSubmit) == 3104);
static_assert(offsetof(NativeAdrenoAbiSubmit, buffers) == 32);
static_assert(sizeof(NativeAdrenoAbiParams) == 24);
static_assert(sizeof(NativeAdrenoAbiDeviceInfo) == 64);
struct Model {
 NativeAdrenoAbiParams params{2,32,36,0,65536};
 unsigned calls=0, submits=0;
 IOReturn failure=kIOReturnSuccess;
 static IOReturn call(void *cookie,uint32_t method,const void *input,size_t in_bytes,void *output,size_t out_bytes) {
  auto &m=*static_cast<Model *>(cookie);++m.calls;
  if(method==NativeAdrenoMethodQuery) {
   assert(!input && !in_bytes && output && out_bytes==sizeof(m.params));
   std::memcpy(output,&m.params,sizeof(m.params));return 0;
  }
  if(method==NativeAdrenoMethodSubmit) {
   assert(input && in_bytes==3104 && output && out_bytes==8);++m.submits;
   auto &p=*static_cast<const NativeAdrenoAbiSubmit *>(input);
   assert(p.reserved==0 && p.queue==7 && p.wait_fence==0 && p.wait_timeout_ns==0);
   assert(p.buffer_count==128);
   for(unsigned i=0;i<128;++i)assert(p.buffers[i].handle==i+1 && p.buffers[i].bytes==4);
   if(m.failure)return m.failure;
   *static_cast<NativeAdrenoAbiSubmitReply *>(output)={99};return 0;
  }
  if(method==NativeAdrenoMethodBoAlloc) {
   assert(in_bytes==16 && out_bytes==8);
   auto &p=*static_cast<const NativeAdrenoAbiBoAlloc *>(input);
   assert(p.bytes==65536 && p.align_log2==14 && p.contiguous==0);
   *static_cast<NativeAdrenoAbiBoAllocReply *>(output)={88};return 0;
  }
  assert(false);return kIOReturnUnsupported;
 }
};
int main() {
 Model m;NativeAdrenoTransport t{&m,Model::call};NativeAdrenoClient client;
 assert(client.open(nullptr)==kIOReturnBadArgument && m.calls==0);
 // A v1 peer with the identical callback signature must be rejected.
 m.params.abi_version=1;assert(client.open(&t)==kIOReturnUnsupported && !client.isOpen());
 m.params.abi_version=2;m.params.flags=0x80000000;
 assert(client.open(&t)==kIOReturnUnsupported && !client.isOpen());
 m.params.flags=0;m.params.output_bits=31;
 assert(client.open(&t)==kIOReturnUnsupported && !client.isOpen());
 m.params.output_bits=36;assert(client.open(&t)==0);
 uint64_t handle=123;unsigned before=m.calls;
 assert(client.allocBo(65537,14,false,&handle)==kIOReturnBadArgument && handle==0 && m.calls==before);
 assert(client.allocBo(4097,14,false,&handle)==kIOReturnBadArgument && m.calls==before);
 assert(client.allocBo(65536,14,false,&handle)==0 && handle==88);
 NativeAdrenoAbiSubmitBuffer buffers[129]{};
 for(unsigned i=0;i<129;++i)buffers[i]={i+1,0x90000000ULL+i*4096,4};
 uint64_t fence=123;before=m.calls;
 assert(client.submit(buffers,129,0,0,&fence,7)==kIOReturnBadArgument && fence==0 && m.calls==before);
 assert(client.submit(buffers,0,0,0,&fence,7)==kIOReturnBadArgument && m.calls==before);
 assert(client.submit(buffers,128,0,0,&fence,7)==0 && fence==99 && m.submits==1);
 m.failure=kIOReturnTimeout;fence=123;
 assert(client.submit(buffers,128,0,0,&fence,7)==kIOReturnTimeout && fence==0 && m.submits==2);
 assert(client.close()==0 && !client.isOpen());before=m.calls;
 assert(client.submit(buffers,128,0,0,&fence,7)==kIOReturnNotReady && fence==0 && m.calls==before);
 std::puts("PASS HOST_MODEL_ONLY: v1/flags/address/count refusal, exact v2 layouts, max BO, 128 IB envelopes, transport failure and closed-state gating; no GPU commands");
}
