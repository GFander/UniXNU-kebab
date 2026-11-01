from pathlib import Path
import hashlib,json,os,subprocess,tempfile
r=Path(__file__).resolve().parent;w=r/'surface-ool-0482';source=w/'candidate/iokit/Kernel/arm/KebabSurface.cpp';s=source.read_text()
def function(name):
 a=s.index('static IOReturn\n'+name+'(');z=s.index('\n}\n',a)+3;return s[a:z]
body='\n'.join(function(n) for n in ('kebab_surface_descriptor_copy','kebab_structure_in','kebab_struct_out'))
assert s.count('kebab_surface_descriptor_copy(')==5 and 'Descriptor->writeBytes' not in s and 'Descriptor->readBytes' not in s
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
using IOReturn=uint32_t;using IOByteCount=uint64_t;
constexpr IOReturn kIOReturnSuccess=0,kIOReturnVMError=10,kIOReturnNoSpace=11,kIOReturnBadArgument=12,kIOReturnNoMemory=13;
unsigned allocations,freed;bool allocFail;
void *IOMallocData(size_t n){if(allocFail)return nullptr;allocations++;return malloc(n);}
void IOFreeData(void *p,size_t n){(void)n;freed++;free(p);}
struct IOMemoryDescriptor {
 unsigned wire=0,prepares=0,completes=0,reads=0,writes=0;IOReturn prepError=0,completeError=0;bool shortCopy=false;uint8_t data[32]={};
 IOByteCount getLength(){return sizeof(data);}
 IOReturn prepare(){prepares++;if(prepError)return prepError;wire++;return 0;}
 IOReturn complete(){assert(wire);completes++;wire--;return completeError;}
 IOByteCount readBytes(IOByteCount off,void *p,IOByteCount n){assert(wire&&off==0&&n<=sizeof(data));reads++;IOByteCount k=shortCopy?n-1:n;memcpy(p,data,k);return k;}
 IOByteCount writeBytes(IOByteCount off,const void *p,IOByteCount n){assert(wire&&off==0&&n<=sizeof(data));writes++;IOByteCount k=shortCopy?n-1:n;memcpy(data,p,k);return k;}
};
struct IOExternalMethodArguments {
 IOMemoryDescriptor *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;const void *structureInput=nullptr;uint32_t structureInputSize=0;
 void *structureOutput=nullptr;uint32_t structureOutputSize=0;IOByteCount structureOutputDescriptorSize=777;
};
'''
tests=r'''
int main(){unsigned cases=0;uint8_t data[32];for(unsigned i=0;i<32;i++)data[i]=i+1;
 for(bool write:{false,true})for(unsigned initial:{0u,3u}){
  IOMemoryDescriptor m;m.wire=initial;if(!write)memcpy(m.data,data,32);uint8_t local[32]={};if(write)memcpy(local,data,32);
  assert(kebab_surface_descriptor_copy(&m,local,32,write)==0);assert(m.prepares==1&&m.completes==1&&m.wire==initial&&m.writes==unsigned(write)&&m.reads==unsigned(!write));
  assert(!memcmp(write?m.data:local,data,32));cases++;
 }
 for(bool write:{false,true}){
  IOMemoryDescriptor m;m.prepError=91;assert(kebab_surface_descriptor_copy(&m,data,32,write)==91);assert(!m.wire&&!m.completes&&!m.reads&&!m.writes);cases++;
  m.prepError=0;m.shortCopy=true;assert(kebab_surface_descriptor_copy(&m,data,32,write)==kIOReturnVMError);assert(m.completes==1&&!m.wire);cases++;
  m.shortCopy=false;m.completeError=92;assert(kebab_surface_descriptor_copy(&m,data,32,write)==92);assert(m.completes==2&&!m.wire);cases++;
 }
 for(unsigned fault=0;fault<4;fault++){
  IOMemoryDescriptor m;IOExternalMethodArguments a;a.structureOutputDescriptor=&m;m.prepError=fault==1?93:0;m.shortCopy=fault==2;m.completeError=fault==3?94:0;
  IOReturn got=kebab_struct_out(&a,data,32);assert(got==(fault==1?93:fault==2?kIOReturnVMError:fault==3?94:0));
  assert(a.structureOutputDescriptorSize==(fault?777:32));assert(m.completes==unsigned(fault!=1)&&!m.wire);cases++;
 }
 IOMemoryDescriptor m;IOExternalMethodArguments a;a.structureOutputDescriptor=&m;assert(kebab_struct_out(&a,data,33)==kIOReturnNoSpace);assert(!m.prepares&&a.structureOutputDescriptorSize==777);cases++;
 uint8_t inlineOut[32]={};a.structureOutputDescriptor=nullptr;a.structureOutput=inlineOut;a.structureOutputSize=32;
 assert(!kebab_struct_out(&a,data,32)&&a.structureOutputSize==32&&!memcmp(data,inlineOut,32));cases++;
 a.structureOutputSize=31;assert(kebab_struct_out(&a,data,32)==kIOReturnNoSpace);cases++;
 for(unsigned fault=0;fault<4;fault++){
  IOMemoryDescriptor input;memcpy(input.data,data,32);a.structureInputDescriptor=&input;uint8_t *out=nullptr;uint32_t size=0;
  input.prepError=fault==1?95:0;input.shortCopy=fault==2;input.completeError=fault==3?96:0;unsigned oldAlloc=allocations,oldFree=freed;
  IOReturn got=kebab_structure_in(&a,&out,&size,32);assert(got==(fault==1?95:fault==2?kIOReturnVMError:fault==3?96:0));
  if(!fault){assert(size==32&&out&&!memcmp(out,data,32));IOFreeData(out,32);}else assert(!out&&!size);
  assert(allocations==oldAlloc+1&&freed==oldFree+1&&!input.wire);cases++;
 }
 a.structureInputDescriptor=&m;uint8_t *out=nullptr;uint32_t size=0;unsigned oldPrepares=m.prepares;
 assert(kebab_structure_in(&a,&out,&size,31)==kIOReturnBadArgument&&m.prepares==oldPrepares);cases++;
 allocFail=true;assert(kebab_structure_in(&a,&out,&size,32)==kIOReturnNoMemory&&m.prepares==oldPrepares);allocFail=false;cases++;
 a.structureInputDescriptor=nullptr;a.structureInput=data;a.structureInputSize=32;assert(!kebab_structure_in(&a,&out,&size,32)&&size==32&&!memcmp(out,data,32));IOFreeData(out,size);cases++;
 assert(allocations==freed);printf("PASS %u actual C++ descriptor-copy/input/output cases; IOKit memory model only\n",cases);
}
'''
prefix='#include <initializer_list>\n'+prefix
mutants=[
 ('missing-prepare','IOReturn ret = memory->prepare();','IOReturn ret = kIOReturnSuccess;'),
 ('ignore-prepare-failure','return ret;\n\t}\n\tIOByteCount copied','return kIOReturnSuccess;\n\t}\n\tIOByteCount copied'),
 ('missing-complete','IOReturn completed = memory->complete();','IOReturn completed = kIOReturnSuccess;'),
 ('ignore-complete-error','copied == length ? completed : kIOReturnVMError','copied == length ? kIOReturnSuccess : kIOReturnVMError'),
 ('ignore-short-transfer','copied == length ? completed : kIOReturnVMError','completed'),
 ('reverse-direction','toDescriptor ? memory->writeBytes','!toDescriptor ? memory->writeBytes'),
 ('output-size-on-error','if (ret != kIOReturnSuccess) {\n\t\t\treturn ret;\n\t\t}\n\t\targs->structureOutputDescriptorSize','if (ret == 123456) {\n\t\t\treturn ret;\n\t\t}\n\t\targs->structureOutputDescriptorSize'),
 ('input-error-success','IOFreeData(copy, len);\n\t\t\treturn ret;','IOFreeData(copy, len);\n\t\t\treturn kIOReturnSuccess;'),
]
env=os.environ.copy();env['TMPDIR']='/home/albert/.cache'
with tempfile.TemporaryDirectory(dir=env['TMPDIR']) as directory:
 d=Path(directory);runs=[]
 for label,old,new in [('baseline',None,None)]+mutants:
  code=body
  if old is not None:assert code.count(old)==1,(label,code.count(old));code=code.replace(old,new)
  cpp=d/(label+'.cpp');cpp.write_text(prefix+code+tests);exe=d/label
  built=subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(cpp),'-o',str(exe)],capture_output=True,text=True,env=env)
  assert built.returncode==0,(label,'compile failure is not behavior rejection',built.stderr)
  run=subprocess.run([str(exe)],capture_output=True,text=True,env=env);assert (run.returncode==0)==(label=='baseline'),(label,run.stdout,run.stderr)
  runs.append(dict(name=label,compiled=True,exit_code=run.returncode,stdout=run.stdout.strip()))
  if label=='baseline':(w/'production-test.cpp').write_text(cpp.read_text(),newline='\n');print(run.stdout.strip())
(w/'test-surface-ool-result-0482.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256((w/'production-test.cpp').read_bytes()).hexdigest(),actual_three_production_function_bodies=True,IOKit_memory_model=True,compiled_behavioral_mutants=len(mutants),all_four_production_call_sites_present=True,full_create_and_valuecache_integration_host_qualified=False,native_or_physical_test=False,runs=runs),indent=2)+'\n')
print('PASS8 compiled behavioral mutants; real native and physical OOL lifecycle still pending')
