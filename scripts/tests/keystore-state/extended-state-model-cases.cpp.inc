static const uint8_t expected[]={0x31,0x13,0x30,0x07,0x0c,0x02,'s','s',0x02,0x01,0x04,0x30,0x08,0x0c,0x03,'s','l','s',0x02,0x01,0x03};
int main(){unsigned cases=0;
for(unsigned role=0;role<8;role++)for(unsigned selector:{17u,35u}){
 AppleKeyStoreUserClient c(role);uint64_t handle=0,scalar=0xdeadbeef;uint8_t buffer[128];IOMemoryDescriptor d;
 auto call=[&](IOExternalMethodArguments&q){cases++;return c.externalMethod(selector,&q,nullptr,nullptr,nullptr);};
 auto args=[&](){IOExternalMethodArguments q;q.scalarInput=&handle;q.scalarInputCount=1;q.structureOutput=buffer;q.structureOutputSize=128;return q;};
 auto q=args();memset(buffer,0xa5,128);assert(call(q)==kIOReturnNotOpen&&buffer[0]==0xa5);
 IOExternalMethodArguments open;assert(c.externalMethod(0,&open,nullptr,nullptr,nullptr)==0);
 for(unsigned bad=0;bad<19;bad++){
  q=args();memset(buffer,0xa5,128);bank={1,1,0x18};statusError=0;ready=true;handle=0;
  int want=kIOReturnBadArgument;switch(bad){
  case 0:q.scalarInputCount=0;break;case 1:q.scalarInputCount=2;break;case 2:q.scalarInput=nullptr;break;
  case 3:handle=1;break;case 4:q.scalarOutput=&scalar;q.scalarOutputCount=1;break;case 5:q.structureInputSize=1;break;
  case 6:q.structureInputDescriptor=&d;break;case 7:q.structureOutputDescriptor=&d;break;case 8:q.asyncWakePort=1;break;
  case 9:q.asyncReferenceCount=1;break;case 10:q.structureVariableOutputData=(OSObject**)&scalar;break;
  case 11:ready=false;want=kIOReturnNotReady;break;case 12:statusError=kIOReturnIOError;want=kIOReturnIOError;break;
  case 13:bank.version=2;want=kIOReturnNotReady;break;case 14:bank.policy=2;want=kIOReturnNotReady;break;
  case 15:bank.available_classes=0x10;want=kIOReturnNotReady;break;case 16:bank.available_classes=0x1a;want=kIOReturnNotReady;break;
  case 17:q.structureOutput=nullptr;want=kIOReturnNoSpace;break;case 18:q.structureOutputSize=20;want=kIOReturnNoSpace;break;}
  assert(call(q)==want);assert(scalar==0xdeadbeef);for(auto v:buffer)assert(v==0xa5);assert(q.structureOutputSize==(bad==18?20u:128u));
 }
 bank={1,1,0x18};statusError=0;ready=true;handle=0;
 for(unsigned descriptor=0;descriptor<2;descriptor++){
  for(unsigned capacity=0;capacity<22;capacity++){
   q=args();memset(buffer,0xa5,128);memset(d.bytes,0xa5,128);d.length=capacity;d.shortWrite=false;
   q.structureOutputSize=capacity;if(descriptor){q.structureOutput=nullptr;q.structureOutputDescriptor=&d;q.structureOutputDescriptorSize=99;}
   unsigned prior=statusCalls;int result=call(q);
   if(capacity<21){assert(result==kIOReturnNoSpace&&statusCalls==prior);for(auto v:buffer)assert(v==0xa5);for(auto v:d.bytes)assert(v==0xa5);}
   else{assert(!result&&!memcmp(descriptor?d.bytes:buffer,expected,21));assert((descriptor?q.structureOutputDescriptorSize:q.structureOutputSize)==21);assert((descriptor?d.bytes:buffer)[21]==0xa5);}
  }
 }
 q=args();q.structureOutput=nullptr;q.structureOutputDescriptor=&d;d.length=128;d.shortWrite=true;assert(call(q)==kIOReturnIOError);
 assert(c.administrative==bool(role&1)&&c.keychainAccess==bool(role&2)&&c.deviceAccess==bool(role&4));assert(mutations==0);
}
 AppleKeyStoreUserClient publicClient(0);IOExternalMethodArguments q;assert(!publicClient.externalMethod(0,&q,nullptr,nullptr,nullptr));
 for(unsigned selector=1;selector<128;selector++){if(selector==7||selector==14||selector==17||selector==35)continue;unsigned prior=statusCalls;assert(publicClient.externalMethod(selector,&q,nullptr,nullptr,nullptr)==kIOReturnNotPrivileged);assert(prior==statusCalls&&mutations==0);cases++;}
 printf("PASS %u actual state/helper/dispatch cases; backend and IOKit are explicit models; no key or lock mutation\n",cases);return 0;}
