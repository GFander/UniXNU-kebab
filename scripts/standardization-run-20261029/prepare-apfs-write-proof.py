"""Add an exclusive fixed-file write and reboot-verification probe on Preboot."""
from pathlib import Path
import hashlib,json,subprocess
r=Path(__file__).parent;d=r/'apfs-write-proof'
files=['bsd/kern/kebab_apfs_probe.c','bsd/kern/kebab_apfs_probe.h','bsd/dev/kebab_install.c']
if (d/'before').exists():
    got={p:{'source':(d/'before'/p).read_text()} for p in files}
else:
    got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps(files),text=True,capture_output=True,check=True).stdout)
for p,value in got.items():
    for name in ['before','after']:
        f=d/name/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(value['source'],newline='\n')
p=d/'after/bsd/kern/kebab_apfs_probe.h';p.write_text(p.read_text().replace('KEBAB_DATA_H','KEBAB_APFS_PROBE_H'),newline='\n')
size=65559;payload=bytes((i*17+(i>>8)+0x5a)&255 for i in range(size));digest=hashlib.sha256(payload).digest()
(d/'proof.json').write_text(json.dumps({'path':'/private/preboot/.hios-apfs-proof-v1','size':size,'sha256':digest.hex(),
 'pattern':'byte(i) = (17*i + (i>>8) + 0x5a) & 255','scope':'Preboot fixed exclusive test file only; System remains read-only'},indent=2)+'\n')
helper=r'''
#define KEBAB_APFS_PROOF_SIZE 65559u
#define KEBAB_APFS_PROOF_PATH "/private/preboot/.hios-apfs-proof-v1"
extern int kebab_ufs_sync(void);

static uint8_t
kebab_apfs_proof_byte(uint64_t i)
{
 return (uint8_t)(17 * i + (i >> 8) + 0x5a);
}

static int
kebab_apfs_proof_file(mount_t expected_mount, bool write_file)
{
 vfs_context_t ctx = vfs_context_current();
 uint8_t *buffer = kalloc_data(4096, Z_WAITOK);
 if (!buffer) return ENOMEM;
 vnode_t file = NULLVP;
 int error = 0, close_error;
 if (write_file) {
  error = vnode_open(KEBAB_APFS_PROOF_PATH, O_CREAT | O_EXCL | O_NOFOLLOW | FWRITE, 0600, 0, &file, ctx);
  if (error) goto done;
  if (!vnode_isreg(file) || vnode_mount(file) != expected_mount) { error = EINVAL; goto close_write; }
  for (uint64_t off = 0; off < KEBAB_APFS_PROOF_SIZE; ) {
   int count = KEBAB_APFS_PROOF_SIZE - off > 4096 ? 4096 : (int)(KEBAB_APFS_PROOF_SIZE - off);
   for (int i = 0; i < count; i++) buffer[i] = kebab_apfs_proof_byte(off + i);
   int residual = 0;
   error = vn_rdwr(UIO_WRITE, file, (caddr_t)buffer, count, (off_t)off, UIO_SYSSPACE, IO_SYNC,
       vfs_context_ucred(ctx), &residual, NULL);
   if (!error && residual) error = EIO;
   if (error) break;
   off += count;
  }
  { int fsync_error = VNOP_FSYNC(file, MNT_WAIT, ctx); if (!error) error = fsync_error; }
close_write:
  close_error = vnode_close(file, FWRITE, ctx);
  file = NULLVP;
  if (!error) error = close_error;
  if (error) goto done;
 }
 error = vnode_open(KEBAB_APFS_PROOF_PATH, O_NOFOLLOW | FREAD, 0, 0, &file, ctx);
 if (error) goto done;
 if (!vnode_isreg(file) || vnode_mount(file) != expected_mount) { error = EINVAL; goto close_read; }
 struct vnode_attr attributes;
 VATTR_INIT(&attributes);
 VATTR_WANTED(&attributes, va_data_size);
 error = vnode_getattr(file, &attributes, ctx);
 if (!error && attributes.va_data_size != KEBAB_APFS_PROOF_SIZE) error = EINVAL;
 if (error) goto close_read;
 SHA256_CTX hash;
 uint8_t digest[SHA256_DIGEST_LENGTH];
 SHA256_Init(&hash);
 for (uint64_t off = 0; off < KEBAB_APFS_PROOF_SIZE; ) {
  int count = KEBAB_APFS_PROOF_SIZE - off > 4096 ? 4096 : (int)(KEBAB_APFS_PROOF_SIZE - off);
  int residual = 0;
  error = vn_rdwr(UIO_READ, file, (caddr_t)buffer, count, (off_t)off, UIO_SYSSPACE, 0,
      vfs_context_ucred(ctx), &residual, NULL);
  if (!error && residual) error = EIO;
  if (error) break;
  for (int i = 0; i < count; i++) if (buffer[i] != kebab_apfs_proof_byte(off + i)) { error = EIO; break; }
  if (error) break;
  SHA256_Update(&hash, buffer, count);
  off += count;
 }
 if (!error) {
  static const uint8_t expected[SHA256_DIGEST_LENGTH] = {DIGEST_BYTES};
  SHA256_Final(digest, &hash);
  if (memcmp(digest, expected, sizeof(digest))) error = EIO;
  printf("KEBAB APFS PROOF: %s bytes=%u sha256=", write_file ? "write/read" : "reboot-read", KEBAB_APFS_PROOF_SIZE);
  for (unsigned i = 0; i < sizeof(digest); i++) printf("%02x", digest[i]);
  printf(" %s\n", error ? "FAIL" : "PASS");
 }
close_read:
 close_error = vnode_close(file, FREAD, ctx);
 file = NULLVP;
 if (!error) error = close_error;
done:
 kfree_data(buffer, 4096);
 return error;
}

'''.replace('DIGEST_BYTES',','.join('0x%02x'%x for x in digest))
p=d/'after/bsd/kern/kebab_apfs_probe.c';s=p.read_text();needle='#include "kebab_apfs_probe.h"'
s=s.replace(needle,'#include <sys/fcntl.h>\n#include <kern/kalloc.h>\n#include <sys/uio.h>\n#include <libkern/crypto/sha2.h>\n'+needle+helper,1)
s=s.replace('/* Opt-in APFS Preboot qualification. System remains read-only. No raw writes. */',
'''/* Opt-in Preboot qualification: 1 read-only mount, 2 exclusive file write,
 * 3 read-only reboot verification. System always remains read-only. */''',1)
s=s.replace('if (mode != 1 || !scratch) return EINVAL;','if (mode > 3 || !scratch) return EINVAL;\n bool write_file = (mode == 2);',1)
s=s.replace('if (rw) return EINVAL; /* first mounted-volume test must be read-only throughout */',
 'if ((write_file && rw != 1) || (!write_file && rw)) return EINVAL;',1)
s=s.replace('System=%u/%u readonly=1\\n','System=%u/%u readonly=%u\\n',1)
s=s.replace('data.backing_id, system.major_number, system.minor_number);','data.backing_id, system.major_number, system.minor_number, !write_file);',1)
s=s.replace('.flags = MNT_RDONLY, .data = scratch + 0x200','.flags = write_file ? 0 : MNT_RDONLY, .data = scratch + 0x200',1)
s=s.replace('!(vfs_flags(mounted) & MNT_RDONLY) || strcmp',
 '((vfs_flags(mounted) & MNT_RDONLY) != 0) == write_file || strcmp',1)
s=s.replace(' vnode_put(data_root);\n return error;',r''' if (!error && mode != 1) error = kebab_apfs_proof_file(mounted, write_file);
 if (write_file) {
  /* Flush even after a failed file operation, before a diagnostic panic. */
  int filesystem_error = VFS_SYNC(mounted, MNT_WAIT, vfs_context_current());
  int device_error = kebab_ufs_sync();
  printf("KEBAB APFS PROOF: durable flush filesystem=%d UFS=%d\n", filesystem_error, device_error);
  if (!error) error = filesystem_error ? filesystem_error : device_error;
 }
 vnode_put(data_root);
 return error;''',1)
p.write_text(s,newline='\n')
p=d/'after/bsd/dev/kebab_install.c';s=p.read_text();a=s.index('/*\n * VFS_SYNC on the root mount.');b=s.index('\n/*',s.index('\n}\n',a)+3)
s=s[:a]+r'''/* Normal reset must also flush a separately mounted writable APFS volume.
 * vfs_iterate(0) holds iteration references and calls outside the list lock.
 * Panic reset never reaches this function. Preserve the first sync failure. */
struct ki_sync_state { vfs_context_t context; int error; };
static int
ki_sync_mount(mount_t mount, void *argument)
{
 struct ki_sync_state *state = argument;
 if (!(vfs_flags(mount) & MNT_RDONLY)) {
  int error = VFS_SYNC(mount, MNT_WAIT, state->context);
  if (!state->error) state->error = error;
 }
 return VFS_RETURNED;
}

static int
ki_vfs_sync(void)
{
 if (rootvnode == NULLVP || rootvnode->v_mount == NULL) return ENODEV;
 struct ki_sync_state state = { vfs_context_current(), 0 };
 int error = vfs_iterate(0, ki_sync_mount, &state);
 return state.error ? state.error : error;
}
''' +s[b:];p.write_text(s,newline='\n')
print('Prepared fixed Preboot proof',size,digest.hex(),'and all-mounted-volume normal sync')
