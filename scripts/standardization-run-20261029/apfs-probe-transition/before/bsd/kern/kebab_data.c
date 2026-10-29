/* Opt-in APFS Data qualification. System remains read-only. No raw writes. */
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
#include "kebab_data.h"

int kebab_data_prepare(proc_t, user_addr_t);
int
kebab_data_prepare(proc_t p, user_addr_t scratch)
{
 uint32_t mode = 0, rw = 0;
 if (!PE_parse_boot_argn("kebab_data_mount", &mode, sizeof(mode)) || !mode) return 0;
 if (mode != 1 || !scratch) return EINVAL;
 PE_parse_boot_argn("ufsrw", &rw, sizeof(rw));
 if (rw) return EINVAL; /* first mounted-volume test must be read-only throughout */
 mount_t original_root = vnode_mount(rootvnode);
 if (!(vfs_flags(original_root) & MNT_RDONLY) || strcmp(vfs_statfs(original_root)->f_fstypename, "apfs")) return EINVAL;
 struct kebab_volume_identity system, data;
 int error = kebab_lookup_apfs_data(&system, &data);
 if (error) { printf("KEBAB DATA: identity lookup error=%d\n", error); return error; }
 vnode_t root_device = vfs_devvp(original_root);
 dev_t root_dev = root_device ? vnode_specrdev(root_device) : NODEV;
 if (root_device) vnode_put(root_device);
 if (root_dev != makedev(system.major_number, system.minor_number)) return EINVAL;
 char path[64];
 snprintf(path, sizeof(path), "/dev/%s", data.bsd_name);
 printf("KEBAB DATA: target uuid=4C56203C-CFE8-4DFF-BF8E-BB48D852C936 path=%s dev=%u/%u backing=%llx System=%u/%u readonly=1\n",
     path, data.major_number, data.minor_number, data.backing_id, system.major_number, system.minor_number);
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
     .flags = MNT_RDONLY, .data = scratch + 0x200 };
 int32_t result = 0;
 if ((error = copyout("apfs", request.type, 5)) ||
     (error = copyout("/private/var", request.path, 13)) ||
     (error = copyout(path, spec, strlen(path) + 1)) ||
     (error = copyout(payload, request.data, sizeof(payload)))) return error;
 error = mount(p, &request, &result);
 printf("KEBAB DATA: mount syscall error=%d\n", error);
 if (error) return error;
 vnode_t data_root = NULLVP;
 error = vnode_lookup("/private/var", 0, &data_root, vfs_context_current());
 if (error) return error;
 mount_t mounted = vnode_mount(data_root);
 vnode_t device = vfs_devvp(mounted);
 dev_t actual = device ? vnode_specrdev(device) : NODEV;
 if (device) vnode_put(device);
 if (actual != makedev(data.major_number, data.minor_number) || mounted == original_root ||
     !(vfs_flags(mounted) & MNT_RDONLY) || strcmp(vfs_statfs(mounted)->f_fstypename, "apfs") ||
     vnode_mount(rootvnode) != original_root || !(vfs_flags(original_root) & MNT_RDONLY)) error = EINVAL;
 printf("KEBAB DATA: mounted dev=%u/%u readonly=%u System-readonly=%u identity=%s\n",
     major(actual), minor(actual), (vfs_flags(mounted) & MNT_RDONLY) != 0,
     (vfs_flags(original_root) & MNT_RDONLY) != 0, error ? "FAIL" : "PASS");
 vnode_put(data_root);
 return error;
}
#endif
