
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <zlib.h>
#define UFS_BLOCK 4096u
#define BOUNCE_SIZE (1u<<20)
#define DMA_BOUNCE 0
#define Z_WAITOK 0
static void *kalloc_data(size_t n,int){return malloc(n);}
static void kfree_data(void *p,size_t){free(p);}
static bool panicking=false,irq=false,preempt=false,interrupts=true;
static bool panic_active(){return panicking;}static bool ml_at_interrupt_context(){return irq;}static unsigned get_preemption_level(){return preempt;}static bool ml_get_interrupts_enabled(){return interrupts;}
static std::mutex kebab_ufs_lock;static std::atomic<bool> owned{false};
static void lck_mtx_lock(std::mutex *m){m->lock();assert(!owned.exchange(true));}
static void lck_mtx_unlock(std::mutex *m){assert(owned.exchange(false));m->unlock();}
static std::vector<uint8_t> medium(4u<<20),bounce(BOUNCE_SIZE);
static struct {uint8_t lun;uint64_t device_blocks,first_block,nblocks;bool ready,failed,writable;uintptr_t dma;} su;
static unsigned step=0,failAt=0,reads=0;
static int ufs_read_capacity(){assert(owned&&su.lun==4);su.device_blocks=medium.size()/UFS_BLOCK;return ++step==failAt?EIO:0;}
static int ufs_read_blocks(uint64_t lba,uint32_t count){assert(owned&&su.lun==4);++reads;if(++step==failAt){memset(bounce.data(),0xcc,bounce.size());return EIO;}if(!count||count>BOUNCE_SIZE/UFS_BLOCK||lba>=su.device_blocks||count>su.device_blocks-lba)return EINVAL;memcpy(bounce.data(),medium.data()+lba*UFS_BLOCK,count*UFS_BLOCK);return 0;}
static uint32_t
ufs_inspect_crc32(const uint8_t *bytes, uint32_t size)
{
	uint32_t crc=UINT32_MAX;
	for (uint32_t i=0; i<size; i++) {
		crc^=bytes[i];
		for (unsigned j=0; j<8; j++) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
	}
	return ~crc;
}

/* Read-only firmware VFAT reader. No allocation, writes or arbitrary path traversal,
 * executable trust claim or Bluetooth service publication. Fixed ASCII names;
 * one checked LFN slot covers every permitted image/firmware name (<=12). */
struct kbf_fat {
 const uint8_t *raw;
 uint32_t bytes,bps,cluster_bytes,clusters,fat,fat_bytes,root,root_bytes,data,bits;
 uint64_t logical_bytes;
 uint32_t logical_clusters;
};
struct kbf_file { uint32_t first,bytes; };
static uint16_t kbf_u16(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static uint32_t kbf_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t kbf_u64(const uint8_t *p){return (uint64_t)kbf_u32(p)|(uint64_t)kbf_u32(p+4)<<32;}
static bool kbf_power2(uint32_t n){return n&&!(n&(n-1));}
static bool kbf_cluster(const struct kbf_fat *f,uint32_t n){return n>=2&&n-2<f->clusters&&n<(f->bits==12?0xff0u:f->bits==16?0xfff0u:0xffffff0u);}
static bool kbf_eoc(const struct kbf_fat *f,uint32_t n){return n>=(f->bits==12?0xff8u:f->bits==16?0xfff8u:0xffffff8u);}
static bool
kbf_name_equal(const uint8_t *entry,const char name[11])
{
 for(unsigned i=0;i<11;++i){
  unsigned a=entry[i],b=(uint8_t)name[i];
  if(a>='a'&&a<='z')a-=32;
  if(b>='a'&&b<='z')b-=32;
  if(a!=b)return false;
 }
 return true;
}
/* A fixed 8.3 lookup target is at most 12 ASCII characters with its dot.
 * Only a complete single-slot LFN can equal it. Reject orphan/multi-slot,
 * malformed termination/padding, type/cluster and alias-checksum mismatches. */
static bool
kbf_lfn_equal(const uint8_t *e,const char name[11])
{
 if(e[0]!=0x41||e[11]!=0x0f||e[12]||kbf_u16(e+26))return false;
 char wanted[13];unsigned count=0,base=8,ext=3;
 while(base&&name[base-1]==' ')--base;
 while(ext&&name[8+ext-1]==' ')--ext;
 for(unsigned j=0;j<base;++j)wanted[count++]=name[j];
 if(ext){wanted[count++]='.';for(unsigned j=0;j<ext;++j)wanted[count++]=name[8+j];}
 static const uint8_t positions[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
 for(unsigned j=0;j<13;++j){
  unsigned ch=kbf_u16(e+positions[j]);
  if(j>=count){if(ch!=(j==count?0u:0xffffu))return false;continue;}
  unsigned expected=(uint8_t)wanted[j];
  if(ch>='a'&&ch<='z')ch-=32;
  if(expected>='a'&&expected<='z')expected-=32;
  if(ch!=expected)return false;
 }
 return true;
}
static uint8_t
kbf_alias_checksum(const uint8_t *e)
{
 uint8_t sum=0;
 for(unsigned j=0;j<11;++j)sum=(uint8_t)((sum>>1)|((sum&1)<<7))+e[j];
 return sum;
}
static int
kbf_next(const struct kbf_fat *f,uint32_t n,uint32_t *next)
{
 if(!kbf_cluster(f,n))return EINVAL;
 uint32_t off=f->bits==12?n+n/2:f->bits==16?n*2:n*4;
 unsigned size=f->bits==32?4:2;
 if(off>f->fat_bytes||size>f->fat_bytes-off)return EINVAL;
 const uint8_t *p=f->raw+f->fat+off;
 uint32_t v=f->bits==32?kbf_u32(p)&0xfffffff:kbf_u16(p);
 if(f->bits==12)v=(n&1)?v>>4:v&0xfff;
 if(!kbf_eoc(f,v)&&!kbf_cluster(f,v))return EINVAL;
 *next=v;return 0;
}
static int
kbf_init(struct kbf_fat *f,const uint8_t *raw,uint32_t bytes)
{
 if(!f||!raw||bytes<512||kbf_u16(raw+510)!=0xaa55)return EINVAL;
 uint32_t bps=kbf_u16(raw+11),spc=raw[13],reserved=kbf_u16(raw+14),fats=raw[16];
 uint32_t entries=kbf_u16(raw+17),total=kbf_u16(raw+19),fatsectors=kbf_u16(raw+22),bits=0,active=0;
 if(!total)total=kbf_u32(raw+32);
 if(!kbf_power2(bps)||bps<512||bps>4096||!kbf_power2(spc)||spc>128||!reserved||fats<1||fats>2||!total)return EINVAL;
 if(!fatsectors){
  fatsectors=kbf_u32(raw+36);bits=32;
  uint32_t flags=kbf_u16(raw+40);
  if(entries||kbf_u16(raw+42)||(flags&~0x8fu))return EINVAL;
  if(flags&0x80)active=flags&15;
  if(active>=fats)return EINVAL;
 }else if(!entries||entries%(bps/32))return EINVAL;
 uint64_t fatbytes=(uint64_t)fatsectors*bps,rootbytes=((uint64_t)entries*32+bps-1)/bps*bps;
 uint64_t root=((uint64_t)reserved+(uint64_t)fats*fatsectors)*bps,data=root+rootbytes,extent=(uint64_t)total*bps;
 /* Firmware images may retain only the populated prefix of a larger logical
  * FAT volume. BPB geometry determines FAT width; the resident container is
  * the absolute read limit. All metadata and each complete file cluster must
  * fit the resident bytes. This is not a writable mount or a full FS check. */
 if(!fatsectors||data>=extent||data>=bytes||fatbytes>bytes)return EINVAL;
 uint32_t clusterbytes=bps*spc,logical=(uint32_t)((extent-data)/clusterbytes);
 uint32_t clusters=(bytes-(uint32_t)data)/clusterbytes;if(clusters>logical)clusters=logical;
 if(!logical||!clusters||logical>0xfffffedu)return EINVAL;
 if(!bits)bits=logical<4085?12:16;
 if(bits==16&&logical>=65525)return EINVAL;
 uint64_t needed=bits==12?((uint64_t)(logical+2)*3+1)/2:(uint64_t)(logical+2)*(bits/8);
 if(needed>fatbytes)return EINVAL;
 uint32_t fat=(uint32_t)(((uint64_t)reserved+(uint64_t)active*fatsectors)*bps);
 if(fats==2&&!(bits==32&&(kbf_u16(raw+40)&0x80))&&memcmp(raw+fat,raw+fat+(uint32_t)fatbytes,(size_t)fatbytes))return EINVAL;
 struct kbf_fat result={raw,bytes,bps,clusterbytes,clusters,fat,(uint32_t)fatbytes,
     bits==32?kbf_u32(raw+44):(uint32_t)root,(uint32_t)rootbytes,(uint32_t)data,bits,extent,logical};
 if(bits==32&&!kbf_cluster(&result,result.root))return EINVAL;
 *f=result;return 0;
}
static uint32_t kbf_cluster_offset(const struct kbf_fat *f,uint32_t cluster){return f->data+(cluster-2)*f->cluster_bytes;}
static int
kbf_directory(const struct kbf_fat *f,uint32_t first,bool fixed,const char name[11],bool directory,struct kbf_file *file)
{
 uint32_t cluster=first;bool seen=false,long_match=false;uint8_t checksum=0;struct kbf_file found={0,0};
 if(!fixed){
  uint32_t probe=first;
  for(uint32_t i=0;;++i){
   uint32_t next;if(i>=f->clusters||kbf_next(f,probe,&next))return EINVAL;
   if(kbf_eoc(f,next))break;
   probe=next;
  }
 }
 for(uint32_t walked=0;walked<=f->clusters;++walked){
  if(!fixed&&!kbf_cluster(f,cluster))return EINVAL;
  uint32_t off=fixed?f->root:kbf_cluster_offset(f,cluster),size=fixed?f->root_bytes:f->cluster_bytes;
  for(uint32_t i=0;i<size;i+=32){
   const uint8_t *e=f->raw+off+i;
   if(!e[0]){if(seen){*file=found;return 0;}return ENOENT;}
   if(e[0]==0xe5){long_match=false;continue;}
   if(e[11]==0x0f){long_match=kbf_lfn_equal(e,name);checksum=e[13];continue;}
   bool match=kbf_name_equal(e,name)||(long_match&&checksum==kbf_alias_checksum(e));long_match=false;
   if((e[11]&0x08)||!match)continue;
   if(seen||(e[11]&0xc0)||((e[11]&0x10)!=0)!=directory)return EINVAL;
   uint32_t n=kbf_u16(e+26),bytes=kbf_u32(e+28);
   if(f->bits==32)n|=(uint32_t)kbf_u16(e+20)<<16;
   else if(kbf_u16(e+20))return EINVAL;
   if(!kbf_cluster(f,n)||(directory?bytes!=0:!bytes))return EINVAL;
   found.first=n;found.bytes=bytes;seen=true;
  }
  if(fixed){if(seen){*file=found;return 0;}return ENOENT;}
  uint32_t next;int error=kbf_next(f,cluster,&next);if(error)return error;
  if(kbf_eoc(f,next)){if(seen){*file=found;return 0;}return ENOENT;}
  cluster=next;
 }
 return EINVAL; /* Directory cycle or unreasonable chain. */
}
static int
kbf_find(const struct kbf_fat *f,const char name[11],struct kbf_file *file)
{
 struct kbf_file dir,found;
 int error=kbf_directory(f,f->root,f->bits!=32,"IMAGE      ",true,&dir);if(error)return error;
 error=kbf_directory(f,dir.first,false,name,false,&found);if(error)return error;
 if(found.bytes>f->bytes)return EINVAL;
 uint32_t remaining=found.bytes,cluster=found.first;
 for(uint32_t walked=0;walked<f->clusters;++walked){
  uint32_t next;if(kbf_next(f,cluster,&next))return EINVAL;
  if(remaining<=f->cluster_bytes){if(!kbf_eoc(f,next))return EINVAL;*file=found;return 0;}
  if(kbf_eoc(f,next))return EINVAL;
  remaining-=f->cluster_bytes;cluster=next;
 }
 return EINVAL;
}
static int
kbf_copy(const struct kbf_fat *f,const struct kbf_file *file,void *output,uint32_t capacity)
{
 if(!output||capacity<file->bytes)return ERANGE;
 uintptr_t begin=(uintptr_t)output,source=(uintptr_t)f->raw;
 if(begin>UINTPTR_MAX-file->bytes||(begin<source+f->bytes&&source<begin+file->bytes))return EINVAL;
 uint8_t *to=(uint8_t *)output;uint32_t cluster=file->first,remaining=file->bytes;
 while(remaining){
  uint32_t n=remaining<f->cluster_bytes?remaining:f->cluster_bytes;
  memcpy(to,f->raw+kbf_cluster_offset(f,cluster),n);to+=n;remaining-=n;
  if(remaining){uint32_t next;int error=kbf_next(f,cluster,&next);if(error)return error;cluster=next;}
 }
 return 0;
}
/* Fixed slot-b Bluetooth firmware, read-only. This code never changes the
 * keymaster inspector, enables the radio or publishes a fictional controller. */
#define KBF_IMAGE_BYTES 0x100000u
static const char kbf_names[4][12]={"HTBTFW20TLV","HTNV20  BIN","HTBTXM  TLV","HTNV20XMBIN"};
static int
kbf_load_image(void *output)
{
 if(!output)return EINVAL;
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled())return EBUSY;
 lck_mtx_lock(&kebab_ufs_lock);
 uint8_t saved_lun=su.lun;
 uint64_t saved_capacity=su.device_blocks,saved_first=su.first_block,saved_blocks=su.nblocks;
 int error=ENXIO;uint8_t *b=(uint8_t *)(su.dma+DMA_BOUNCE);
 uint32_t hsize,hcrc,tcrc,nent,esz,table_blocks;uint64_t current,backup,table,first,last,chosen=0;
 unsigned matches=0;
 if(!su.ready||su.failed||saved_lun!=0)goto restore;
 su.lun=4;error=ufs_read_capacity();if(error)goto restore;
 error=ufs_read_blocks(1,1);if(error)goto restore;
 hsize=kbf_u32(b+12);hcrc=kbf_u32(b+16);current=kbf_u64(b+24);backup=kbf_u64(b+32);
 first=kbf_u64(b+40);last=kbf_u64(b+48);table=kbf_u64(b+72);nent=kbf_u32(b+80);esz=kbf_u32(b+84);tcrc=kbf_u32(b+88);
 if(memcmp(b,"EFI PART",8)||kbf_u32(b+8)!=0x10000||kbf_u32(b+20)||hsize<92||hsize>UFS_BLOCK||
     current!=1||backup!=su.device_blocks-1||first<2||last<first||last>=backup||table<2||
     esz!=128||!nent||nent>BOUNCE_SIZE/128){error=EINVAL;goto restore;}
 table_blocks=(nent*128+UFS_BLOCK-1)/UFS_BLOCK;
 if(table>first||table_blocks>first-table){error=EINVAL;goto restore;}
 bzero(b+16,4);if(ufs_inspect_crc32(b,hsize)!=hcrc){error=EINVAL;goto restore;}
 error=ufs_read_blocks(table,table_blocks);if(error)goto restore;
 if(ufs_inspect_crc32(b,nent*128)!=tcrc){error=EINVAL;goto restore;}
 for(uint32_t i=0;i<nent;++i){
  const uint8_t *e=b+i*128;static const char name[]="bluetooth_b";bool match=true;unsigned nonzero=0;
  for(unsigned j=0;j<36;++j)if(kbf_u16(e+56+2*j)!=(j<sizeof(name)-1?(uint8_t)name[j]:0))match=false;
  if(!match)continue;
  for(unsigned j=0;j<16;++j)nonzero|=e[j];
  uint64_t begin=kbf_u64(e+32),end=kbf_u64(e+40);
  if(!nonzero||begin<first||end<begin||end>last||end-begin+1!=KBF_IMAGE_BYTES/UFS_BLOCK||end>UINT32_MAX){error=EINVAL;goto restore;}
  chosen=begin;++matches;
 }
 if(matches!=1){error=matches?EINVAL:ENOENT;goto restore;}
 error=ufs_read_blocks(chosen,KBF_IMAGE_BYTES/UFS_BLOCK);
 if(!error)memcpy(output,b,KBF_IMAGE_BYTES);
restore:
 su.lun=saved_lun;su.device_blocks=saved_capacity;su.first_block=saved_first;su.nblocks=saved_blocks;
 lck_mtx_unlock(&kebab_ufs_lock);return error;
}

/* Consumer selects a family only after reading the controller version.
 * Enumeration alone is NOT selection, signature validation or RF readiness. */
int
kebab_bluetooth_firmware_read(uint32_t family,void *output,uint32_t capacity,uint32_t *actual)
{
 if(!actual||!output||family>=4)return EINVAL;
 *actual=0;
 if(panic_active()||ml_at_interrupt_context()||get_preemption_level()||!ml_get_interrupts_enabled())return EBUSY;
 uint8_t *image=(uint8_t *)kalloc_data(KBF_IMAGE_BYTES,Z_WAITOK);if(!image)return ENOMEM;
 struct kbf_fat fat;struct kbf_file file;
 int error=kbf_load_image(image);
 if(!error)error=kbf_init(&fat,image,KBF_IMAGE_BYTES);
 if(!error)error=kbf_find(&fat,kbf_names[family],&file);
 if(!error)error=kbf_copy(&fat,&file,output,capacity);
 if(!error)*actual=file.bytes;
 kfree_data(image,KBF_IMAGE_BYTES);return error;
}

static unsigned cases;
static void put16(uint8_t *p,uint16_t x){p[0]=x;p[1]=x>>8;}
static void put32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;++i)p[i]=x>>(i*8);}
static void put64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;++i)p[i]=x>>(i*8);}
static void setfat(std::vector<uint8_t>&v,unsigned bits,uint32_t n,uint32_t value){
 uint32_t bps=512,res=bits==32?32:1,fs=bits==32?16:bits==16?33:6;
 for(unsigned f=0;f<2;++f){uint8_t *fat=v.data()+(res+f*fs)*bps;
  if(bits==12){unsigned off=n+n/2;uint16_t old=kbf_u16(fat+off);put16(fat+off,(n&1)?(old&15)|(value<<4):(old&0xf000)|(value&0xfff));}
  else if(bits==16)put16(fat+n*2,value);else put32(fat+n*4,value);
 }
}
static std::vector<uint8_t> makefat(unsigned bits){
 uint32_t size=bits==16?4u<<20:1u<<20,bps=512,res=bits==32?32:1,fs=bits==32?16:bits==16?33:6,rootentries=bits==32?0:32;
 std::vector<uint8_t> v(size);put16(v.data()+11,bps);v[13]=1;put16(v.data()+14,res);v[16]=2;put16(v.data()+17,rootentries);put16(v.data()+19,size/bps);put16(v.data()+22,bits==32?0:fs);put16(v.data()+510,0xaa55);
 if(bits==32){put32(v.data()+36,fs);put32(v.data()+44,2);}
 unsigned eof=bits==12?0xfff:bits==16?0xffff:0xfffffff;setfat(v,bits,0,eof);setfat(v,bits,1,eof);
 struct kbf_fat f;assert(kbf_init(&f,v.data(),v.size())==0);
 unsigned dir=bits==32?3:2,first=dir+1;uint8_t *root=v.data()+(bits==32?kbf_cluster_offset(&f,2):f.root);
 if(bits==32)setfat(v,bits,2,eof);
 memcpy(root,"IMAGE      ",11);root[11]=0x10;put16(root+26,dir);setfat(v,bits,dir,eof);
 uint8_t *entry=v.data()+kbf_cluster_offset(&f,dir);
 memcpy(entry,".          ",11);entry[11]=0x10;
 entry+=32;memcpy(entry,kbf_names[0],11);entry[11]=0x20;put16(entry+26,first);put32(entry+28,1200);
 entry+=32;memcpy(entry,kbf_names[1],11);entry[11]=0x20;put16(entry+26,first+1);put32(entry+28,64);
 setfat(v,bits,first,first+4);setfat(v,bits,first+4,first+2);setfat(v,bits,first+2,eof);setfat(v,bits,first+1,eof);
 unsigned pos=0;for(unsigned n:{first,first+4,first+2})for(unsigned j=0;j<512&&pos<1200;++j,++pos)v[kbf_cluster_offset(&f,n)+j]=(uint8_t)(pos*17+3);
 return v;
}
static void baseline(){su={0,0x100000,0x2000,0x80000,true,false,true,(uintptr_t)bounce.data()};step=reads=failAt=0;}
static void restored(){assert(su.lun==0&&su.device_blocks==0x100000&&su.first_block==0x2000&&su.nblocks==0x80000&&su.writable&&!owned);}
static void gptcrc(){uint8_t *h=medium.data()+4096;put32(h+88,::crc32(0,medium.data()+8192,128*128));put32(h+16,0);put32(h+16,::crc32(0,h,kbf_u32(h+12)));}
static void fixture(){
 baseline();std::fill(medium.begin(),medium.end(),0);uint8_t *h=medium.data()+4096;memcpy(h,"EFI PART",8);put32(h+8,0x10000);put32(h+12,92);put64(h+24,1);put64(h+32,medium.size()/4096-1);put64(h+40,6);put64(h+48,medium.size()/4096-2);put64(h+72,2);put32(h+80,128);put32(h+84,128);
 uint8_t *entry=medium.data()+8192;entry[0]=1;entry[16]=2;put64(entry+32,16);put64(entry+40,16+256-1);
 const char *name="bluetooth_b";for(unsigned j=0;j<11;++j)put16(entry+56+j*2,name[j]);
 auto fat=makefat(12);memcpy(medium.data()+16*4096,fat.data(),fat.size());gptcrc();
}
static void refuses_fat(std::vector<uint8_t> v){struct kbf_fat f;struct kbf_file file;if(kbf_init(&f,v.data(),v.size())==0)assert(kbf_find(&f,kbf_names[0],&file)!=0);++cases;}
static std::vector<uint8_t> qca_container(){
 std::vector<uint8_t> v(1u<<20);put16(v.data()+11,4096);v[13]=4;put16(v.data()+14,1);v[16]=2;put16(v.data()+17,512);put16(v.data()+22,3);put32(v.data()+32,16384);memcpy(v.data()+54,"FAT16   ",8);put16(v.data()+510,0xaa55);
 auto set=[&](unsigned n,unsigned value){for(unsigned copy=0;copy<2;++copy)put16(v.data()+4096+copy*12288+n*2,value);};
 set(0,0xfff8);set(1,0xffff);set(2,0xffff);set(3,7);set(7,5);set(5,0xffff);set(4,0xffff);
 struct kbf_fat f;assert(kbf_init(&f,v.data(),v.size())==0&&f.bits==16&&f.logical_bytes==(64u<<20)&&f.logical_clusters==4093&&f.clusters==61);
 uint8_t *root=v.data()+f.root;memcpy(root,"IMAGE      ",11);root[11]=0x10;put16(root+26,2);
 uint8_t *e=v.data()+kbf_cluster_offset(&f,2);memcpy(e,kbf_names[0],11);e[11]=0x20;put16(e+26,3);put32(e+28,40000);
 e+=32;memcpy(e,kbf_names[1],11);e[11]=0x20;put16(e+26,4);put32(e+28,64);
 unsigned pos=0;for(unsigned cluster:{3u,7u,5u})for(unsigned j=0;j<f.cluster_bytes&&pos<40000;++j,++pos)v[kbf_cluster_offset(&f,cluster)+j]=(uint8_t)(pos*17+3);
 return v;
}
// Independent media construction: arbitrary SFN aliases paired with real LFNs.
static void fixture_lfn(uint8_t *e,const char *name,const char alias[11]){
 memset(e,0,32);e[0]=0x41;e[11]=0x0f;
 unsigned sum=0;for(unsigned i=0;i<11;++i){sum=((sum>>1)+((sum&1)*128)+(unsigned char)alias[i])&255;}e[13]=(uint8_t)sum;
 const unsigned offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};unsigned n=(unsigned)strlen(name);
 for(unsigned i=0;i<13;++i)put16(e+offsets[i],i<n?(unsigned char)name[i]:i==n?0:0xffff);
}
static std::vector<uint8_t> lfnfat(unsigned bits){
 auto v=makefat(bits);struct kbf_fat f;assert(kbf_init(&f,v.data(),v.size())==0);
 uint8_t *root=v.data()+(bits==32?kbf_cluster_offset(&f,2):f.root);
 memcpy(root+32,root,32);memcpy(root+32,"PCAFUI~2   ",11);fixture_lfn(root,"image","PCAFUI~2   ");
 unsigned dir=bits==32?3:2;uint8_t *e=v.data()+kbf_cluster_offset(&f,dir);
 memcpy(e+128,e+64,32);memcpy(e+64,e+32,32);
 memcpy(e+64,"EXBNED~2TLV",11);fixture_lfn(e+32,"htbtfw20.tlv","EXBNED~2TLV");
 memcpy(e+128,"LSB5SP~2BIN",11);fixture_lfn(e+96,"htnv20.bin","LSB5SP~2BIN");return v;
}
int main(){
 assert(ufs_inspect_crc32((const uint8_t*)"123456789",9)==0xcbf43926);++cases;
 for(unsigned bits:{12u,16u,32u}){
  auto v=makefat(bits);struct kbf_fat f;struct kbf_file file;assert(kbf_init(&f,v.data(),v.size())==0&&f.bits==bits);assert(kbf_find(&f,kbf_names[0],&file)==0&&file.bytes==1200);
  std::vector<uint8_t> output(1200,0xa5);assert(kbf_copy(&f,&file,output.data(),output.size())==0);for(unsigned i=0;i<1200;++i)assert(output[i]==(uint8_t)(i*17+3));++cases;
  assert(kbf_copy(&f,&file,v.data(),v.size())==EINVAL);++cases;std::fill(output.begin(),output.end(),0xa5);assert(kbf_copy(&f,&file,output.data(),1199)==ERANGE);for(auto x:output)assert(x==0xa5);++cases;
  unsigned dir=bits==32?3:2,first=dir+1;
  auto lower=v;auto lowercase=[](uint8_t *e){for(unsigned i=0;i<11;++i)if(e[i]>='A'&&e[i]<='Z')e[i]+=32;};
  lowercase(lower.data()+(bits==32?kbf_cluster_offset(&f,2):f.root));lowercase(lower.data()+kbf_cluster_offset(&f,dir)+32);lowercase(lower.data()+kbf_cluster_offset(&f,dir)+64);
  struct kbf_fat lf;struct kbf_file lfile;assert(kbf_init(&lf,lower.data(),lower.size())==0&&kbf_find(&lf,kbf_names[0],&lfile)==0&&lfile.bytes==1200);++cases;
  for(unsigned bad:{0u,1u,first,bits==12?0xff7u:bits==16?0xfff7u:0xffffff7u,bits==12?0xff1u:bits==16?0xfff1u:0xffffff1u}){auto m=v;setfat(m,bits,first,bad);refuses_fat(m);}
  auto cycle=v;setfat(cycle,bits,dir,dir);refuses_fat(cycle);
  auto early=v;setfat(early,bits,first,bits==12?0xfffu:bits==16?0xffffu:0xfffffffu);refuses_fat(early);
  auto duplicate=v;unsigned at=kbf_cluster_offset(&f,dir);memcpy(duplicate.data()+at+3*32,duplicate.data()+at+32,32);refuses_fat(duplicate);
  lowercase(duplicate.data()+at+3*32);refuses_fat(duplicate);
 }
 for(unsigned bits:{12u,16u,32u}){
  auto v=lfnfat(bits);struct kbf_fat f;struct kbf_file file;assert(kbf_init(&f,v.data(),v.size())==0&&kbf_find(&f,kbf_names[0],&file)==0&&file.bytes==1200);++cases;
  std::vector<uint8_t> out(1200);assert(kbf_copy(&f,&file,out.data(),out.size())==0);for(unsigned j=0;j<1200;++j)assert(out[j]==(uint8_t)(j*17+3));++cases;
  assert(kbf_find(&f,kbf_names[1],&file)==0&&file.bytes==64);++cases;
  unsigned root=bits==32?kbf_cluster_offset(&f,2):f.root,dir=bits==32?3:2,at=kbf_cluster_offset(&f,dir);
  for(unsigned off:{0u,1u,11u,12u,13u,14u,16u,26u,27u}){auto m=v;m[root+off]^=1;refuses_fat(m);}
  for(unsigned off:{0u,1u,11u,12u,13u,26u,27u,30u}){auto m=v;m[at+32+off]^=1;refuses_fat(m);}
  {auto m=v;m[root]=0x42;refuses_fat(m);}
  {auto m=v;m[root+32]^=1;refuses_fat(m);}
  {auto m=v;memcpy(m.data()+at+160,m.data()+at+32,64);refuses_fat(m);}
  // LFN at final directory slot must bind to the SFN in the next cluster.
  {auto m=v;unsigned next=dir+10,eof=bits==12?0xfff:bits==16?0xffff:0xfffffff;setfat(m,bits,dir,next);setfat(m,bits,next,eof);
   std::vector<uint8_t> pair(m.begin()+at+32,m.begin()+at+96);memset(m.data()+at+32,0,f.cluster_bytes-32);
   for(unsigned off=32;off<f.cluster_bytes;off+=32)m[at+off]=0xe5;
   memcpy(m.data()+at+f.cluster_bytes-32,pair.data(),32);unsigned dest=kbf_cluster_offset(&f,next);memcpy(m.data()+dest,pair.data()+32,32);
   struct kbf_fat other;assert(kbf_init(&other,m.data(),m.size())==0&&kbf_find(&other,kbf_names[0],&file)==0&&file.bytes==1200);++cases;
  }
 }
 auto good=makefat(12);
 for(unsigned off:{11u,12u,13u,14u,16u,17u,19u,22u,510u,511u}){auto m=good;m[off]^=0xff;refuses_fat(m);}
 {auto m=good;m[512+20]^=1;refuses_fat(m);}
 {auto v=qca_container();struct kbf_fat f;struct kbf_file file;assert(kbf_init(&f,v.data(),v.size())==0&&kbf_find(&f,kbf_names[0],&file)==0&&file.bytes==40000);std::vector<uint8_t> out(40000);assert(kbf_copy(&f,&file,out.data(),out.size())==0);for(unsigned i=0;i<40000;++i)assert(out[i]==(uint8_t)(i*17+3));++cases;
  for(unsigned next:{63u,4094u}){auto m=v;for(unsigned copy=0;copy<2;++copy)put16(m.data()+4096+copy*12288+3*2,next);refuses_fat(m);}
  auto cut=v;put32(cut.data()+32,8);refuses_fat(cut);
  auto missing=v;put16(missing.data()+17,0xff80);refuses_fat(missing);
 }
 fixture();std::vector<uint8_t> image(1u<<20,0xa5);assert(kbf_load_image(image.data())==0);restored();assert(image==good);++cases;
 uint32_t actual=99;std::vector<uint8_t> output(1200,0xa5);assert(kebab_bluetooth_firmware_read(0,output.data(),output.size(),&actual)==0&&actual==1200);restored();for(unsigned i=0;i<1200;++i)assert(output[i]==(uint8_t)(i*17+3));++cases;
 fixture();{auto v=qca_container();memcpy(medium.data()+16*4096,v.data(),v.size());std::vector<uint8_t> out(40000);assert(kebab_bluetooth_firmware_read(0,out.data(),out.size(),&actual)==0&&actual==40000);for(unsigned i=0;i<40000;++i)assert(out[i]==(uint8_t)(i*17+3));restored();++cases;}
 fixture();su.writable=false;assert(kbf_load_image(image.data())==0&&su.lun==0&&!su.writable&&!owned);++cases;
 for(unsigned state=0;state<3;++state){fixture();if(state==0)su.ready=false;else if(state==1)su.failed=true;else su.lun=2;assert(kbf_load_image(image.data())==ENXIO&&step==0);assert(su.lun==(state==2?2:0)&&su.device_blocks==0x100000&&su.first_block==0x2000&&su.nblocks==0x80000&&su.writable&&!owned);++cases;}
 fixture();std::fill(output.begin(),output.end(),0xa5);actual=99;assert(kebab_bluetooth_firmware_read(0,output.data(),1199,&actual)==ERANGE&&actual==0);for(auto b:output)assert(b==0xa5);++cases;
 actual=99;assert(kebab_bluetooth_firmware_read(2,output.data(),output.size(),&actual)==ENOENT&&actual==0);for(auto b:output)assert(b==0xa5);++cases;
 actual=99;assert(kebab_bluetooth_firmware_read(4,output.data(),output.size(),&actual)==EINVAL&&actual==99);++cases;
 for(unsigned failure=1;failure<=4;++failure){fixture();failAt=failure;std::fill(image.begin(),image.end(),0xa5);assert(kbf_load_image(image.data())==EIO);restored();for(auto b:image)assert(b==0xa5);++cases;}
 for(unsigned context=0;context<4;++context){fixture();panicking=context==0;irq=context==1;preempt=context==2;interrupts=context!=3;assert(kbf_load_image(image.data())==EBUSY&&step==0);restored();++cases;}panicking=irq=preempt=false;interrupts=true;
 for(unsigned invalid=0;invalid<14;++invalid){
  fixture();uint8_t *h=medium.data()+4096,*e=medium.data()+8192;
  switch(invalid){case 0:put32(h+8,2);break;case 1:put32(h+12,91);break;case 2:put32(h+20,1);break;case 3:put64(h+24,2);break;case 4:put64(h+32,3);break;case 5:put64(h+72,0);break;case 6:put32(h+80,8193);break;case 7:put32(h+84,64);break;case 8:put64(e+32,1);break;case 9:put64(e+40,271-1);break;case 10:put16(e+56,'X');break;case 11:memcpy(e+128,e,128);break;case 12:h[16]^=1;break;case 13:e[100]^=1;break;}
  if(invalid<12&&invalid!=6)gptcrc();
  std::fill(image.begin(),image.end(),0xa5);assert(kbf_load_image(image.data())!=0);restored();for(auto b:image)assert(b==0xa5);++cases;
 }
 fixture();std::atomic<bool> stop{false};std::atomic<unsigned> normal{0};
 std::thread root([&]{while(!stop){lck_mtx_lock(&kebab_ufs_lock);assert(su.lun==0&&su.device_blocks==0x100000&&su.first_block==0x2000&&su.nblocks==0x80000&&su.writable);++normal;lck_mtx_unlock(&kebab_ufs_lock);std::this_thread::yield();}});
 while(!normal)std::this_thread::yield();
 // A root I/O observer must see only its original LUN/capacity/partition.
 for(unsigned i=0;i<100;++i){assert(kbf_load_image(image.data())==0);++cases;}
 stop=true;root.join();restored();assert(normal>0);++cases;
 uint32_t rng=0x270123;
 for(unsigned iteration=0;iteration<2000;++iteration){auto m=good;for(unsigned j=0;j<4;++j){rng=rng*1664525+1013904223;m[(rng>>9)%8192]^=(uint8_t)(rng>>24);}struct kbf_fat f;struct kbf_file file;if(!kbf_init(&f,m.data(),m.size())&&!kbf_find(&f,kbf_names[0],&file)){std::vector<uint8_t> out(file.bytes);assert(kbf_copy(&f,&file,out.data(),out.size())==0);}++cases;}
 printf("PASS %u actual GPT/FAT/read/refusal/interleaving cases; mocked UFS, physical firmware and RF not claimed\n",cases);
}
