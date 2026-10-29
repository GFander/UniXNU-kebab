"""Run the extracted production bus against FIFO and cancel/abort fault MMIO."""
from pathlib import Path
import re
import subprocess
import tempfile

r=Path(__file__).parent
s=(r/'after/bsd/dev/kebab_geni.c').read_text()
s=re.sub(r'^#include [^\n]+\n','',s,flags=re.M)
for name,replacement in [('kt_rd','static inline uint32_t\nkt_rd(vm_offset_t b,uint32_t o) { return mock_read(b,o); }\n'),
                         ('kt_wr','static inline void\nkt_wr(vm_offset_t b,uint32_t o,uint32_t v) { mock_write(b,o,v); }\n')]:
    match=re.search(r'^static inline (?:uint32_t|void)\n'+name+r'\([^;]*?\)\n\{',s,re.M)
    end=s.index('\n}\n',match.end())+3
    s=s[:match.start()]+replacement+s[end:]
header=(r/'after/bsd/dev/kebab_geni.h').read_text()
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define KEBAB 1
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
static uint32_t mock_read(vm_offset_t,uint32_t);
static void mock_write(vm_offset_t,uint32_t,uint32_t);
static int scenario, transfers, txcount, rxcount, io, se_io, cancels, aborts, delays, mapped;
static bool active;
static uint32_t tx[64], command, fail_map;
void delay(unsigned n){assert(n<=200000);delays++;}
bool kebab_usb_console_ready(void){return false;}
void kebab_usb_log_flush(void){abort();}
vm_offset_t ml_io_map(vm_offset_t p, vm_size_t n){
 assert(n<=0x40000);mapped++;return p==fail_map ? 0:p;
}
'''
backend=r'''
static uint32_t mock_read(vm_offset_t b,uint32_t o){
 io++;
 if(b==kg.se && b!=0){
  se_io++;
  if(o==SE_GENI_STATUS) return active ? M_GENI_CMD_ACTIVE:0;
  if(o==SE_GENI_M_IRQ_STATUS){
    if(scenario==2) return M_GP_IRQ_1_NACK;
    if(scenario>=3) return 0;
    return (command>>M_OPCODE_SHFT)==I2C_WRITE && !txcount ? M_TX_FIFO_WATERMARK:M_CMD_DONE;
  }
  if(o==SE_GENI_RX_FIFO_STATUS) return scenario==1 ? 1:32;
  if(o==SE_GENI_RX_FIFO) return (rxcount++==0) ? 0x04030201:0x08070605;
  if(o==GENI_FW_REVISION_RO) return GENI_SE_I2C_PROTO<<FW_REV_PROTOCOL_SHFT;
 }
 if(b==kg.tlmm && (o%TLMM_REG_SIZE)==4) return scenario==7 ? 0:1;
 return 0;
}
static void mock_write(vm_offset_t b,uint32_t o,uint32_t v){
 io++;
 if(b!=kg.se || !b)return;
 se_io++;
 if(o==SE_GENI_M_CMD0){command=v;transfers++;active=scenario>=3;}
 if(o==SE_GENI_TX_FIFO)tx[txcount++]=v;
 if(o==SE_GENI_M_CMD_CTRL_REG){
   if(v==M_GENI_CMD_CANCEL_REQ){cancels++;if(scenario==3)active=false;}
   if(v==M_GENI_CMD_ABORT_REQ){aborts++;if(scenario==4)active=false;}
 }
}
static void reset(int mode){
 memset(&kg,0,sizeof(kg));kg.se=KT_SE13_PA_DEFAULT;kg.tlmm=KT_TLMM_PA+TLMM_TILE_SOUTH;
 scenario=mode;transfers=txcount=rxcount=io=se_io=cancels=aborts=delays=mapped=0;
 command=fail_map=0;active=false;
}
int main(void){
 uint8_t data[129]={1,2,3,4,5};
 reset(0);assert(kebab_geni_i2c_write(data,5)==0);
 assert(txcount==2 && tx[0]==0x04030201 && tx[1]==5);
 assert(((command&SLV_ADDR_MSK)>>SLV_ADDR_SHFT)==0x4b);
 reset(0);assert(kebab_geni_i2c_read(data,5)==0);assert(data[0]==1&&data[4]==5);
 reset(1);assert(kebab_geni_i2c_read(data,5)==-1);assert(!kg.se_dead);
 reset(2);assert(kebab_geni_i2c_write(data,1)==-1);assert(cancels==0&&!kg.se_dead);
 for(int mode=3;mode<=5;mode++){
   reset(mode);assert(kebab_geni_i2c_write(data,1)==-1);
   assert(cancels==1 && aborts==(mode!=3));assert(kg.se_dead==(mode==5));
   if(mode==5){int prior=io;assert(kebab_geni_i2c_read(data,1)==-1);assert(io==prior);}
   reset(mode);assert(kebab_geni_i2c_read(data,1)==-1);assert(cancels==1&&delays<600);
 }
 reset(0);assert(kebab_geni_i2c_write(data,0)==-1);assert(kebab_geni_i2c_write(data,65)==-1);
 assert(kebab_geni_i2c_read(data,0)==-1);assert(kebab_geni_i2c_read(data,129)==-1);assert(io==0);
 reset(0);kg.bb=true;assert(kebab_geni_i2c_write(data,1)==-1);assert(se_io==0);
 reset(7);kg.bb=true;assert(kebab_geni_i2c_read(data,1)==-1);assert(se_io==0&&delays<1000);
 reset(0);memset(&kg,0,sizeof(kg));assert(kebab_geni_map(KT_SE13_PA_DEFAULT,true,1,1,false)==0);
 assert(kg.se==0 && kg.wrap==0 && mapped==1);
 struct kebab_geni_info info;kebab_geni_get_info(&info);assert(info.bitbang&&info.mapped);
 reset(0);memset(&kg,0,sizeof(kg));fail_map=KT_SE13_PA_DEFAULT;
 assert(kebab_geni_map(KT_SE13_PA_DEFAULT,false,1,1,false)==-1&&mapped==1);
 reset(0);assert(kebab_geni_record_poll_error()==1);kg.errors++;assert(kebab_geni_record_poll_error()==3);
 puts("GENI production FIFO/short/NACK/bounds/timeouts/cancel/abort/dead gate/bitbang/mapping/error count PASS");
}
'''
with tempfile.TemporaryDirectory(dir=Path.home()/'.cache') as directory:
    p=Path(directory)/'test.c';exe=p.with_suffix('')
    p.write_text(prefix+header+s+backend)
    subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
    run=subprocess.run([str(exe)],text=True,capture_output=True)
    print('\n'.join(run.stdout.splitlines()[-5:]))
    if run.returncode:print(run.stderr);raise SystemExit(run.returncode)
