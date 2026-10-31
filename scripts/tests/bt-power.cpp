
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
using vm_offset_t=uintptr_t;using vm_size_t=uintptr_t;
alignas(4) static std::array<uint8_t,131072> database;
static unsigned reads,maps,locks,fail_read,fail_map,cases;
static bool locked,panicking,irq,preempt,interrupts=true;
static void *initproc;
#define PROC_NULL nullptr
static void host_log(const char *,...){}
static bool panic_active(){return panicking;}
static bool ml_at_interrupt_context(){return irq;}
static bool ml_get_interrupts_enabled(){return interrupts;}
static void kebab_spmi_bus_lock(){assert(!locked);locked=true;++locks;}
static void kebab_spmi_bus_unlock(){assert(locked);locked=false;}
static uint32_t kebab_rpmh_read32(vm_offset_t base,uint32_t off){
 assert(locked&&base==(uintptr_t)database.data()&&!(off&3)&&off<=131068);
 ++reads;uint32_t value;memcpy(&value,database.data()+off,4);return value;
}
static vm_offset_t ml_io_map(vm_offset_t pa,vm_size_t bytes){
 assert(locked&&pa==0x80860000&&bytes==131072);++maps;
 return maps==fail_map?0:(uintptr_t)database.data();
}
/* Read-only fixed Bluetooth regulator discovery. No rail commands here.
 * cmd-db.c and kona-regulators.dtsi, LineageOS98a7970 define this layout.
 * The DB checksum field is unused by that firmware ABI; do not invent one.
 */
#ifndef KEBAB_BT_POWER_IMPL_H
#define KEBAB_BT_POWER_IMPL_H

#define KBP_DB_PA 0x80860000u
#define KBP_DB_SIZE 0x20000u
#define KBP_DB_HEADER 144u
#define KBP_RESOURCE_COUNT 3u

static const char kbp_names[KBP_RESOURCE_COUNT][8] = {
 "smpa5", "smpa6", "smpc8"
};
struct kbp_resource {
 uint32_t address;
 uint16_t slave_version, auxiliary_length;
 uint8_t auxiliary[16];
 uint32_t matches;
};
struct kbp_result {
 uint32_t version, standalone;
 struct kbp_resource resources[KBP_RESOURCE_COUNT];
};
struct kbp_conflict {
 uint32_t resource, slave_table, entry, offset, first_address, second_address;
};
struct kbp_io {
 int (*read32)(void *, uint32_t, uint32_t *);
 void *context;
};

/* Only aligned reads wholly inside the supplied reserved region. The native
 * callback independently enforces the physical mapping and the same bound. */
static int
kbp_word(const struct kbp_io *io, size_t length, uint32_t offset, uint32_t *value)
{
 if (!io || !io->read32 || !value || (offset & 3u) ||
     length < 4u || offset > length - 4u) return EINVAL;
 return io->read32(io->context, offset, value);
}

static int
kbp_discover(const struct kbp_io *io, size_t length, struct kbp_result *out,
    struct kbp_conflict *conflict)
{
 if (conflict) memset(conflict, 0, sizeof(*conflict));
 if (!out) return EINVAL;
 memset(out, 0, sizeof(*out));
 if (!io || !io->read32 || length < KBP_DB_HEADER || length > KBP_DB_SIZE ||
     (length & 3u)) return EINVAL;
 struct kbp_result found;
 memset(&found, 0, sizeof(found));
 uint32_t magic, reserved, mask = 0;
 int error = kbp_word(io, length, 0, &found.version);
 if (error) return error;
 error = kbp_word(io, length, 4, &magic);
 if (error) return error;
 if (found.version != 1u || magic != 0x0c0330dbu) return EINVAL;
 error = kbp_word(io, length, 140, &reserved);
 if (error) return error;
 found.standalone = reserved & 1u;
 for (unsigned slave = 0; slave < 8u; ++slave) {
  uint32_t w0, w1, w2, header = 8u + slave * 16u;
  error = kbp_word(io, length, header, &w0);
  if (error) return error;
  error = kbp_word(io, length, header + 4u, &w1);
  if (error) return error;
  error = kbp_word(io, length, header + 8u, &w2);
  if (error) return error;
  uint32_t id = w0 & 0xffffu, entries = w1 >> 16;
  uint32_t headers = KBP_DB_HEADER + (w0 >> 16);
  uint32_t auxiliary = KBP_DB_HEADER + (w1 & 0xffffu);
  if (!entries) continue;
  if (!id || id > 7u || entries > 1024u || (headers & 3u) ||
      headers > length || entries > (length - headers) / 24u ||
      auxiliary > length) return EINVAL;
  /* Firmware can have multiple tables of the same slave class. Search all
  * of them. Firmware may repeat an identical accelerator mapping (actual
  * smpa5 on this board does). The upstream query returns the first; here
  * merge only exact address/version/aux equivalence and report the count.
  * Conflicting selected identifiers remain a hard error. */
  for (unsigned entry = 0; entry < entries; ++entry) {
   uint32_t offset = headers + entry * 24u, name[2];
   error = kbp_word(io, length, offset, &name[0]);
   if (error) return error;
   error = kbp_word(io, length, offset + 4u, &name[1]);
   if (error) return error;
   for (unsigned index = 0; index < KBP_RESOURCE_COUNT; ++index) {
    if (memcmp(name, kbp_names[index], 8)) continue;
    uint32_t address, tail;
    error = kbp_word(io, length, offset + 16u, &address);
    if (error) return error;
    error = kbp_word(io, length, offset + 20u, &tail);
    if (error) return error;
    uint32_t bytes = tail & 0xffffu, relative = tail >> 16;
    if (id != 4u || (address & 0xffff0000u) != 0x00040000u ||
        (address & 0xffu) || relative > length - auxiliary ||
        bytes > length - auxiliary - relative) return EINVAL;
    struct kbp_resource resource;
    memset(&resource, 0, sizeof(resource));
    if (bytes > sizeof(resource.auxiliary)) return E2BIG;
    resource.address = address;
    resource.slave_version = (uint16_t)w2;
    resource.auxiliary_length = (uint16_t)bytes;
    resource.matches = 1;
    for (unsigned byte = 0; byte < bytes; ++byte) {
     uint32_t at = auxiliary + relative + byte, value;
     error = kbp_word(io, length, at & ~3u, &value);
     if (error) return error;
     resource.auxiliary[byte] = (uint8_t)(value >> ((at & 3u) * 8u));
    }
    if (mask & (1u << index)) {
     const struct kbp_resource *previous = &found.resources[index];
     if (previous->address != address ||
         previous->slave_version != resource.slave_version ||
         previous->auxiliary_length != bytes ||
         memcmp(previous->auxiliary, resource.auxiliary, sizeof(resource.auxiliary))) {
      if (conflict) *conflict = (struct kbp_conflict) {
       index, slave, entry, offset, previous->address, address
      };
      return EEXIST;
     }
     ++found.resources[index].matches;
     continue;
    }
    /* No alias may map two rail names to the same accelerator register. */
    for (unsigned previous = 0; previous < KBP_RESOURCE_COUNT; ++previous)
     if ((mask & (1u << previous)) &&
         found.resources[previous].address == address) return EINVAL;
    found.resources[index] = resource;
    mask |= 1u << index;
   }
  }
 }
 if (mask != (1u << KBP_RESOURCE_COUNT) - 1u) return ENOENT;
 *out = found;
 return 0;
}
#endif

#define printf(...) host_log(__VA_ARGS__)
extern int get_preemption_level(void);
static vm_offset_t kbp_native_db;
static int
kbp_native_read32(void *context, uint32_t offset, uint32_t *value)
{
 (void)context;
 if (!value || (offset & 3u) || offset > KBP_DB_SIZE - 4u) return EINVAL;
 if (!kbp_native_db) {
  kbp_native_db = ml_io_map(KBP_DB_PA, KBP_DB_SIZE);
  if (!kbp_native_db) return ENOMEM;
 }
 *value = kebab_rpmh_read32(kbp_native_db, offset);
 return 0;
}
void
kebab_bluetooth_power_report(void)
{
 if (panic_active() || ml_at_interrupt_context() || get_preemption_level() ||
     !ml_get_interrupts_enabled()) {
  printf("KEBAB BTPWR: end error=%d unsafe-context; no register access\n", EBUSY);
  return;
 }
 if (__atomic_load_n(&initproc, __ATOMIC_ACQUIRE) == PROC_NULL) {
  printf("KEBAB BTPWR: end error=%d BSD startup incomplete; no register access\n", EAGAIN);
  return;
 }
 const struct kbp_io io = { kbp_native_read32, NULL };
 struct kbp_result result;
 struct kbp_conflict conflict;
 kebab_spmi_bus_lock();
 int error = kbp_discover(&io, KBP_DB_SIZE, &result, &conflict);
 kebab_spmi_bus_unlock();
 printf("KEBAB BTPWR: cmd-db=%08x+%x version=%u standalone=%u error=%d\n",
     KBP_DB_PA, KBP_DB_SIZE, result.version, result.standalone, error);
 if (error == EEXIST)
  printf("KEBAB BTPWR: duplicate resource-index=%u table=%u entry=%u offset=%x first=%08x second=%08x\n",
      conflict.resource, conflict.slave_table, conflict.entry, conflict.offset,
      conflict.first_address, conflict.second_address);
 if (!error) for (unsigned i = 0; i < KBP_RESOURCE_COUNT; ++i)
  printf("KEBAB BTPWR: resource=%s address=%08x slave-version=%04x aux-bytes=%u matches=%u\n",
      kbp_names[i], result.resources[i].address,
      result.resources[i].slave_version, result.resources[i].auxiliary_length,
      result.resources[i].matches);
 printf("KEBAB BTPWR: end error=%d read-only; board binding and rail ownership not established\n", error);
}


#undef printf

int get_preemption_level(){return preempt;}
static void word(unsigned off,uint32_t value){assert(off<=131068);memcpy(database.data()+off,&value,4);}
static void fixture(){
 database.fill(0);word(0,1);word(4,0x0c0330db);word(140,1);
 // Independent little-endian firmware layout: two class3 tables are legal.
 word(8,3);word(12,(1<<16)|0x200);word(144,0x636f6c63);
 word(24,(24<<16)|3);word(28,(1<<16)|0x200);word(168,0x306b6c63);
 word(40,(0x100<<16)|4);word(44,(3<<16)|0x200);word(48,0x100);
 const char names[3][8]={"smpa5","smpa6","smpc8"};
 for(unsigned i=0;i<3;++i){unsigned off=400+i*24;memcpy(database.data()+off,names[i],8);word(off+16,0x41000+i*0x100);word(off+20,4|(i*4<<16));}
 reads=maps=locks=fail_read=fail_map=0;locked=panicking=irq=preempt=false;interrupts=true;initproc=(void*)1;kbp_native_db=0;
}
static int reader(void *,uint32_t off,uint32_t *out){
 assert(!(off&3)&&off<=131068);if(++reads==fail_read)return EIO;
 memcpy(out,database.data()+off,4);return 0;
}
static const kbp_io io={reader,nullptr};
static bool zero(const kbp_result &value){kbp_result empty{};return !memcmp(&value,&empty,sizeof(value));}
static void rejected(int expected){kbp_result out;memset(&out,0xa5,sizeof(out));int actual=kbp_discover(&io,131072,&out,nullptr);if(actual!=expected)fprintf(stderr,"case%u expected%d actual%d\n",cases,expected,actual);assert(actual==expected);assert(zero(out));++cases;}
int main(){
 fixture();const auto unchanged=database;kbp_result out;
 assert(kbp_discover(&io,131072,&out,nullptr)==0&&out.version==1&&out.standalone==1);
 for(unsigned i=0;i<3;++i)assert(out.resources[i].address==0x41000+i*0x100&&out.resources[i].slave_version==0x100&&out.resources[i].auxiliary_length==4);
 assert(database==unchanged);unsigned successful_reads=reads;++cases;
 // Every possible truncated reserved window must fail until all aux bounds fit.
 for(unsigned length=0;length<668;++length){fixture();memset(&out,0xa5,sizeof(out));int error=kbp_discover(&io,length,&out,nullptr);assert(error&&zero(out));++cases;}
 for(unsigned length:{668u,672u,4096u,131072u}){fixture();assert(kbp_discover(&io,length,&out,nullptr)==0);++cases;}
 for(unsigned length:{669u,131073u,0xffffffffu}){fixture();assert(kbp_discover(&io,length,&out,nullptr)==EINVAL&&zero(out));++cases;}
 fixture();assert(kbp_discover(nullptr,131072,&out,nullptr)==EINVAL&&zero(out));assert(kbp_discover(&io,131072,nullptr,nullptr)==EINVAL);++cases;
 for(unsigned version:{0u,2u,0xffffffffu}){fixture();word(0,version);rejected(EINVAL);}
 fixture();word(4,0x0c0330da);rejected(EINVAL);
 for(unsigned count:{1025u,65535u}){fixture();word(44,(count<<16)|0x200);rejected(EINVAL);}
 fixture();word(40,(0x101<<16)|4);rejected(EINVAL);
 fixture();word(40,(0xfff0<<16)|4);word(44,(4096<<16)|0x200);rejected(EINVAL);
 for(unsigned slave:{0u,2u,3u,5u,8u,65535u}){fixture();word(40,(0x100<<16)|slave);rejected(EINVAL);}
 for(unsigned address:{0u,0x31000u,0x51000u,0x41004u,0x10041000u,0x41001u}){fixture();word(416,address);rejected(EINVAL);}
 fixture();word(440,0x41000);rejected(EINVAL);
 // Identical firmware aliases are real records, not a reason to fabricate a
 // new address or suppress conflicting mappings. All three resources remain.
 fixture();word(44,(4<<16)|0x200);memcpy(database.data()+472,database.data()+400,24);
 assert(kbp_discover(&io,131072,&out,nullptr)==0&&out.resources[0].matches==2&&out.resources[1].matches==1&&out.resources[2].matches==1);++cases;
 word(488,0x41400);rejected(EEXIST);
 fixture();word(44,(4<<16)|0x200);memcpy(database.data()+472,database.data()+400,24);word(492,(12<<16)|4);word(668,1);rejected(EEXIST);
 fixture();word(56,(0x300<<16)|4);word(60,(1<<16)|0x200);word(64,0x101);memcpy(database.data()+912,database.data()+400,24);rejected(EEXIST);
 fixture();word(420,17);rejected(E2BIG);

 fixture();memcpy(database.data()+448,"smpa5",8);rejected(EEXIST);
 kbp_conflict conflict;assert(kbp_discover(&io,131072,&out,&conflict)==EEXIST&&zero(out));assert(conflict.resource==0&&conflict.slave_table==2&&conflict.entry==2&&conflict.offset==448&&conflict.first_address==0x41000&&conflict.second_address==0x41200);++cases;
 fixture();word(44,(2<<16)|0x200);rejected(ENOENT);
 fixture();memcpy(database.data()+400,"smpa5x",8);rejected(ENOENT);
 fixture();word(420,65535u|(65535u<<16));word(44,(3<<16)|65535u);rejected(EINVAL);
 // A later same-class table with a duplicate identifier must not be ignored.
 fixture();word(56,(0x300<<16)|4);word(60,(1<<16)|0x200);memcpy(database.data()+912,database.data()+400,24);rejected(EEXIST);
 for(unsigned fault=1;fault<=successful_reads;++fault){fixture();fail_read=fault;rejected(EIO);assert(reads==fault);}
 // Native guards precede mapping and locks; valid report reads only fixed DB.
 for(unsigned context=0;context<5;++context){fixture();panicking=context==0;irq=context==1;preempt=context==2;interrupts=context!=3;if(context==4)initproc=nullptr;const auto before=database;kebab_bluetooth_power_report();assert(!maps&&!locks&&!reads&&database==before);++cases;}
 fixture();kebab_bluetooth_power_report();assert(maps==1&&locks==1&&reads&&!locked&&database==unchanged);++cases;
 reads=0;kebab_bluetooth_power_report();assert(maps==1&&locks==2&&reads&&!locked&&database==unchanged);++cases;
 fixture();fail_map=1;kebab_bluetooth_power_report();assert(maps==1&&locks==1&&!reads&&!locked);++cases;
 for(unsigned off:{1u,2u,131069u,131072u,0xfffffffcu}){fixture();uint32_t value=0xa5;assert(kbp_native_read32(nullptr,off,&value)==EINVAL&&value==0xa5&&!maps&&!reads);++cases;}
 fixture();uint32_t value;assert(kbp_word(&io,2,0,&value)==EINVAL&&!reads);assert(kbp_word(&io,131072,1,&value)==EINVAL&&!reads);++cases;
 printf("PASS actual fixed command-DB/native read-only discovery: %u cases; explicit memory/context/mutex/barrier stand-ins, no hardware or RF claim\n",cases);
}
