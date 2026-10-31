/* Opt-in Preboot qualification: 1 read-only mount, 2 exclusive file write,
 * 3 read-only reboot verification. System always remains read-only. */
#include <pexpert/arm64/board_config.h>
#if defined(KEBAB)
#include <pexpert/pexpert.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/proc_internal.h>
#include <sys/mount_internal.h>
#include <sys/vnode_internal.h>
#include <sys/sysproto.h>
#include <libkern/libkern.h>
#include <sys/fcntl.h>
#include <kern/kalloc.h>
#include <sys/uio.h>
#include <libkern/crypto/sha2.h>
#include "kebab_apfs_probe.h"
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
  static const uint8_t expected[SHA256_DIGEST_LENGTH] = {0x3f,0x2f,0x47,0xb4,0x94,0x55,0xf2,0xd5,0xcc,0xe7,0xd7,0x45,0x82,0x94,0x40,0xcc,0xf0,0xe8,0xf1,0xbe,0xda,0x6c,0x51,0x20,0x64,0x1c,0x25,0x21,0x3f,0x78,0xc9,0x57};
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



int kebab_apfs_probe_prepare(proc_t, user_addr_t);
int
kebab_apfs_probe_prepare(proc_t p, user_addr_t scratch)
{
 uint32_t mode = 0, rw = 0;
 if (!PE_parse_boot_argn("kebab_apfs_probe", &mode, sizeof(mode)) || !mode) return 0;
 if (mode > 3 || !scratch) return EINVAL;
 bool write_file = (mode == 2);
 PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));
 if ((write_file && rw != 1) || (!write_file && rw)) return EINVAL;
 mount_t original_root = vnode_mount(rootvnode);
 if (!(vfs_flags(original_root) & MNT_RDONLY) || strcmp(vfs_statfs(original_root)->f_fstypename, "apfs")) return EINVAL;
 struct kebab_volume_identity system, data;
 int error = kebab_lookup_apfs_preboot(&system, &data);
 if (error) { printf("KEBAB APFS PROBE: identity lookup error=%d\n", error); return error; }
 vnode_t root_device = vfs_devvp(original_root);
 dev_t root_dev = root_device ? vnode_specrdev(root_device) : NODEV;
 if (root_device) vnode_put(root_device);
 if (root_dev != makedev(system.major_number, system.minor_number)) return EINVAL;
 char path[64];
 snprintf(path, sizeof(path), "/dev/%s", data.bsd_name);
 printf("KEBAB APFS PROBE: target uuid=EF45F98E-9F6C-43DB-B7C6-F418CA77429B path=%s dev=%u/%u backing=%llx System=%u/%u readonly=%u\n",
     path, data.major_number, data.minor_number, data.backing_id, system.major_number, system.minor_number, !write_file);
 /* Structure offsets recovered from this target's original mount_apfs.
  * The descriptor is copied to the current init task's scratch page;
  * APFS's normal mount syscall consumes user pointers from that page. */
 uint8_t payload[0x150] = {0};
 user64_addr_t spec = scratch + 0x160;
 uint16_t normal_mount = 1;
 uint32_t anonymous_owner = UINT32_MAX;
 memcpy(payload, &spec, sizeof(spec));
 memcpy(payload + 16, &normal_mount, sizeof(normal_mount));
 memcpy(payload + 0x144, &anonymous_owner, sizeof(anonymous_owner));
 memcpy(payload + 0x148, &anonymous_owner, sizeof(anonymous_owner));
 struct mount_args request = { .type = scratch + 0x100, .path = scratch + 0x120,
     .flags = write_file ? 0 : MNT_RDONLY, .data = scratch + 0x200 };
 int32_t result = 0;
 if ((error = copyout("apfs", request.type, 5)) ||
     (error = copyout("/private/preboot", request.path, 17)) ||
     (error = copyout(path, spec, strlen(path) + 1)) ||
     (error = copyout(payload, request.data, sizeof(payload)))) return error;
 error = mount(p, &request, &result);
 printf("KEBAB APFS PROBE: mount syscall error=%d\n", error);
 if (error) return error;
 vnode_t data_root = NULLVP;
 error = vnode_lookup("/private/preboot", 0, &data_root, vfs_context_current());
 if (error) return error;
 mount_t mounted = vnode_mount(data_root);
 vnode_t device = vfs_devvp(mounted);
 dev_t actual = device ? vnode_specrdev(device) : NODEV;
 if (device) vnode_put(device);
 if (actual != makedev(data.major_number, data.minor_number) || mounted == original_root ||
     ((vfs_flags(mounted) & MNT_RDONLY) != 0) == write_file || strcmp(vfs_statfs(mounted)->f_fstypename, "apfs") ||
     vnode_mount(rootvnode) != original_root || !(vfs_flags(original_root) & MNT_RDONLY)) error = EINVAL;
 printf("KEBAB APFS PROBE: mounted dev=%u/%u readonly=%u System-readonly=%u identity=%s\n",
     major(actual), minor(actual), (vfs_flags(mounted) & MNT_RDONLY) != 0,
     (vfs_flags(original_root) & MNT_RDONLY) != 0, error ? "FAIL" : "PASS");
 if (!error && mode != 1) error = kebab_apfs_proof_file(mounted, write_file);
 if (write_file) {
  /* Flush even after a failed file operation, before a diagnostic panic. */
  int filesystem_error = VFS_SYNC(mounted, MNT_WAIT, vfs_context_current());
  int device_error = kebab_ufs_sync();
  printf("KEBAB APFS PROOF: durable flush filesystem=%d UFS=%d\n", filesystem_error, device_error);
  if (!error) error = filesystem_error ? filesystem_error : device_error;
 }
 vnode_put(data_root);
 return error;
}
#endif
