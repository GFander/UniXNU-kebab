/* Fixed target key objects; deliberately separate from legacy cp_cred_s. */
#ifndef KEBAB_FILE_KEYS_H
#define KEBAB_FILE_KEYS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
#define KEBAB_KEY_STATIC_ASSERT static_assert
#else
#define KEBAB_KEY_STATIC_ASSERT _Static_assert
#endif
struct kebab_aks_credential { uint8_t bytes[80]; };
struct kebab_aks_raw_key {
 void *key;
 uint32_t key_len;
 void *iv_key;
 uint32_t iv_key_len;
 uint32_t flags;
};
struct kebab_aks_wrapped_key { void *key; uint32_t key_len, dp_class; };
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_aks_credential)==80, "target private credential");
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_aks_raw_key)==32 && offsetof(struct kebab_aks_raw_key, iv_key)==16 &&
    offsetof(struct kebab_aks_raw_key, flags)==28, "target raw output ABI");
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_aks_wrapped_key)==16 && offsetof(struct kebab_aks_wrapped_key, dp_class)==12,
    "target wrapped output ABI");
#define KEBAB_KEYBAG_D_BYTES 88u
#define KEBAB_FILE_KEY_BYTES 96u
int kebab_file_keys_prepare(const uint8_t *, unsigned);
int kebab_file_keys_commit(void);
void kebab_file_keys_cancel(void);
bool kebab_file_keys_available(void);
int kebab_device_classbag_create(const uint8_t[16],const uint8_t[16],void *,unsigned);
int kebab_device_classbag_open(const uint8_t[16],const uint8_t[16],const void *,unsigned);
bool kebab_device_classbag_available(void);
/* Backend metadata only. This describes the authenticated factory C/D bank,
 * not Apple SEP, passcode support or availability of other key classes. */
#define KEBAB_DEVICE_BAG_FACTORY_NO_PASSCODE 1u
#define KEBAB_DEVICE_BAG_CLASS_C_D_MASK ((1u << 3) | (1u << 4))
struct kebab_device_classbag_status {
 uint32_t version, policy, available_classes;
};
int kebab_device_classbag_get_status(struct kebab_device_classbag_status *);

/* Original keychain CK/DK/CKU/DKU only; separate from APFS classes. */
int kebab_keychain_key_crypt(int32_t,uint32_t,const void *,unsigned,void *,unsigned,bool);

/* Factory-no-passcode lock state and class-B native raw CPX lifecycle. */
int kebab_device_keybag_bootstrap(void);
int kebab_device_keybag_get_lock_state(uint32_t *);
int kebab_device_keybag_set_locked(bool);
bool kebab_apfs_b_volume_seen(void);
bool kebab_apfs_b_volume_matches(const uint8_t[16]);
bool kebab_apfs_b_raw_io_available(void);
void kebab_apfs_b_note_cpx_destroy(const void *,unsigned,const char *);
int kebab_device_classbag_export(void *,unsigned);
int kebab_device_classbag_cookie(const uint8_t[16],void *,unsigned);
int kebab_keybag_d_create(const uint8_t uuid[16], void *, unsigned);
int kebab_keybag_d_open(const uint8_t uuid[16], const void *, unsigned);
int kebab_file_key_new(const struct kebab_aks_credential *, uint32_t,
    struct kebab_aks_raw_key *, struct kebab_aks_wrapped_key *);
int kebab_file_key_unwrap(const struct kebab_aks_credential *, const struct kebab_aks_wrapped_key *,
    struct kebab_aks_raw_key *);
int kebab_file_key_rewrap(const struct kebab_aks_credential *, uint32_t,
    const struct kebab_aks_wrapped_key *, struct kebab_aks_wrapped_key *);
int kebab_xts128_crypt(const uint8_t key[32], uint64_t unit, const void *, void *, size_t, bool);
int kebab_file_keys_selftest(void);
int kebab_metadata_key_wrap(const struct kebab_aks_credential *, uint32_t,
    const struct kebab_aks_raw_key *, struct kebab_aks_wrapped_key *);
int kebab_metadata_key_unwrap(const struct kebab_aks_credential *, const struct kebab_aks_wrapped_key *,
    struct kebab_aks_raw_key *);
int kebab_metadata_key_create_wrapped(const struct kebab_aks_credential *,uint32_t,
    struct kebab_aks_raw_key *,struct kebab_aks_wrapped_key *);
int kebab_metadata_key_unwrap_wrapped(const struct kebab_aks_credential *,const struct kebab_aks_wrapped_key *,
    struct kebab_aks_raw_key *);
int kebab_apfs_file_key_new(const struct kebab_aks_credential *,uint32_t,
    struct kebab_aks_raw_key *,struct kebab_aks_wrapped_key *);
int kebab_apfs_file_key_unwrap(const struct kebab_aks_credential *,const struct kebab_aks_wrapped_key *,
    struct kebab_aks_raw_key *);
int kebab_apfs_file_key_rewrap(const struct kebab_aks_credential *,uint32_t,
    const struct kebab_aks_wrapped_key *,struct kebab_aks_wrapped_key *);
int kebab_apfs_file_key_is_available(const struct kebab_aks_credential *,uint32_t);
int kebab_metadata_keys_selftest(void);
/* Separate original FileVault descriptor family: lengths are64-bit, not FS32. */
#define KEBAB_RAW_KEY_WRAPPED 1u
/* Audited two-word zero-parameter profile: target providers consume offsets0/8.
 * Caller-local zero initialization of24B is not a24B credential contract. */
struct kebab_fv_credential { uint8_t bytes[16]; };
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_fv_credential)==16,"bounded FV parameter prefix");
struct kebab_fv_wrapped_key { void *key; uint64_t key_len; };
struct kebab_fv_raw_key { void *key; uint64_t key_len; uint32_t flags; };
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_fv_wrapped_key)==16 &&
    offsetof(struct kebab_fv_wrapped_key,key_len)==8,"target FV wrapped descriptor");
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_fv_raw_key)==24 &&
    offsetof(struct kebab_fv_raw_key,key_len)==8 && offsetof(struct kebab_fv_raw_key,flags)==16,"target FV raw descriptor");
int kebab_volume_key_new(const struct kebab_fv_credential *,const void *,const void *,const uint8_t[16],
    struct kebab_fv_wrapped_key *,struct kebab_fv_raw_key *);
int kebab_volume_key_unwrap(const struct kebab_fv_credential *,const void *,const void *,
    const struct kebab_fv_wrapped_key *,struct kebab_fv_raw_key *);
int kebab_volume_key_new_wrapped(const struct kebab_fv_credential *,const void *,const void *,const uint8_t[16],
    struct kebab_fv_wrapped_key *,struct kebab_fv_raw_key *);
int kebab_volume_key_unwrap_wrapped(const struct kebab_fv_credential *,const void *,const void *,
    const struct kebab_fv_wrapped_key *,struct kebab_fv_raw_key *);
/* Original FV state is88B; only authenticated vendor-VEK type/IDs/version
 * are populated. All unsupported optional capability fields remain zero. */
struct kebab_fv_blob_state { uint8_t bytes[88]; };
KEBAB_KEY_STATIC_ASSERT(sizeof(struct kebab_fv_blob_state)==88,"original FV blob-state output");
int kebab_volume_blob_state(const struct kebab_fv_credential *,const struct kebab_fv_wrapped_key *,
    struct kebab_fv_blob_state *);
int kebab_volume_set_protection(const struct kebab_fv_credential *,uint32_t,uint64_t,
    const struct kebab_fv_wrapped_key *,const struct kebab_fv_wrapped_key *,
    struct kebab_fv_wrapped_key *,struct kebab_fv_wrapped_key *);
int kebab_volume_factory_activate(const uint8_t[16]);
bool kebab_volume_class_available(const uint8_t[16],uint32_t);
int kebab_volume_keys_selftest(void);
int kebab_vek_cpx_handle_create(const uint8_t[16],const uint8_t[32],uint8_t[64]);
int kebab_vek_cpx_handle_open(const uint8_t[64],uint8_t[32]);
int kebab_volume_key_length(const struct kebab_fv_credential *,uint32_t,uint64_t *);
#undef KEBAB_KEY_STATIC_ASSERT
#endif
