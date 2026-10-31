#ifndef KEBAB_GENI_H
#define KEBAB_GENI_H
#include <stdbool.h>
#include <stdint.h>

#define KT_SE13_PA_DEFAULT 0x00a94000u
/* Native-only cached metadata. Physical resources, never mapped VAs. */
struct kebab_geni_info {
	uint64_t resources[6];
	bool mapped, bitbang, initialized, dead;
};
#ifdef __cplusplus
extern "C" {
#endif
/* Normal transactions serialize a whole TCM command or header+payload frame,
 * rather than just each FIFO operation. Early/panic recovery does not use GENI. */
void kebab_geni_transaction_lock(void);
void kebab_geni_transaction_unlock(void);
struct kebab_geni_status {
	struct kebab_geni_info info;
	uint32_t protocol, sequencer, master_irq;
};
struct kebab_touch_status {
	uint32_t max_x, max_y, max_objects;
	bool identified, decoding, queue_ok, device_node, unavailable;
};
int kebab_geni_read_status(struct kebab_geni_status *status);
void kebab_touch_get_status(struct kebab_touch_status *status);
void kebab_geni_trace(const char *fmt, ...);
void kebab_geni_sleep_ms(uint32_t ms);
int kebab_geni_map(uint32_t se_pa, bool bitbang, uint32_t pins, uint32_t reset, bool raw);
bool kebab_geni_map_default(void);
void kebab_geni_get_info(struct kebab_geni_info *info);
/* Original shared error counter also counts failed polling iterations. */
uint32_t kebab_geni_record_poll_error(void);
void kebab_geni_probe_hardware(void);
bool kebab_geni_manual_init(void);
int kebab_geni_boot_prepare(uint32_t se_pa, uint32_t hz, uint32_t pins, uint32_t reset, bool raw);
int kebab_geni_i2c_write(const uint8_t *buf, uint32_t len);
int kebab_geni_i2c_read(uint8_t *buf, uint32_t len);
/* Retained console entry points implemented by the board adapter. */
void kebab_touch_se(void);
void kebab_touch_ping(void);
void kebab_touch_clock_enable(void);
#ifdef __cplusplus
}
#endif
#endif
