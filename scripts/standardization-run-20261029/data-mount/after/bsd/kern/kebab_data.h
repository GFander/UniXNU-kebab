/* Private normal-boot volume identity, no IOKit types in BSD. */
#ifndef KEBAB_DATA_H
#define KEBAB_DATA_H
#include <sys/types.h>
struct kebab_volume_identity {
 uint32_t major_number, minor_number;
 uint64_t backing_id;
 char bsd_name[32];
};
#ifdef __cplusplus
extern "C" {
#endif
int kebab_lookup_apfs_data(struct kebab_volume_identity *, struct kebab_volume_identity *);
#ifdef __cplusplus
}
#endif
#endif
