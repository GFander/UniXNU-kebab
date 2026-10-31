/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * /dev/kebab_keys -- the PM8150 power and volume keys as data.
 *
 * This is the third P2 input item.  There is no HID stack on this port
 * (options NO_KERNEL_HID, no kxld), so a key cannot become an IOHIDEvent and
 * nothing here pretends it can: a kernel thread reads the PMIC key registers,
 * the host-tested state machine in package/KebabKeys.c debounces them, and
 * every accepted transition becomes one 24-byte record in a 64-slot ring that
 * read(2) drains.  A userland consumer is a later item.
 *
 * WHERE THE KEYS COME FROM (see KebabKeys.h for the full evidence)
 *
 *   power        PM8150 PON, SID 0, RT_STS 0x0810, bit 0 KPDPWR_N_SET
 *   volume up    PM8150 GPIO6, page 0x0c500, RT_STS 0x0c510, bit 0, active low
 *   volume down  PM8150 GPIO7, page 0x0c600, RT_STS 0x0c610, bit 0, active low
 *                and PON RESIN, RT_STS bit 1, which the vendor device tree
 *                also maps to KEY_VOLUMEDOWN
 *
 * The four bytes read per sample are the three status registers plus the
 * PM8150 GPIO6 type register at 0x0c504 (expected 0x10).  The type byte is
 * what separates "both active-low volume keys are held" (possible) from "the
 * bus returned four zeroes" (not a key state), and it is why an all-zero read
 * cannot turn into every key pressed.
 *
 * Off unless boot-arg kebab_keys=1.  kebab_keys_ms=N changes the poll period
 * (default 16 ms, clamped to 4..1000) and kebab_keys_raw=1 prints a raw line
 * whenever any of the four bytes changes.  No PMIC register is written, by
 * this driver or by the SPMI read path it calls.
 */
#if defined(KEBAB)

#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#define KEBAB_KEYS_KERNEL 1
#include "kebab_keys_core.h"

/*
 * bsd/dev/kebab_spmi.c: read 0x0810, 0x0c510, 0x0c610 and 0x0c504 through the
 * observer channel.  Returns 0 only when all four reads completed, and fills
 * raw[] only then.  The SPMI file owns the arbiter and the addresses; this
 * driver owns none of them.
 */
extern int kebab_spmi_keys_sample(uint8_t raw[4]);
extern int kebab_spmi_keys_config(uint8_t gpio6[3], uint8_t gpio7[3]);

/* bsd/kern/bsd_init.c calls this; the polling thread never returns. */
int kebab_keys_start(void);
void kebab_keys_publish(void);
bool kebab_keys_node_selftest(void);
static void kk_thread(void *arg, wait_result_t result) __dead2;

/* ---------------------------------------------------------------- record */

/*
 * The record /dev/kebab_keys returns.  magic lets a reader reject a buffer
 * that was never filled; seq is the record's position in the event stream;
 * timestamp_ns is nanoseconds since boot; the four raw bytes are the exact
 * registers that produced the transition, so a wrong bit layout is visible in
 * the record and not only in the kernel log.
 */
#define KK_EVENT_MAGIC          0x4b4b4556u     /* 'KKEV' */
#define KK_QUEUE_LEN            64

struct kk_record {
	uint32_t magic;
	uint32_t seq;
	uint64_t timestamp_ns;
	uint8_t  key;                   /* KEBAB_KEYS_KEY_* */
	uint8_t  pressed;               /* 1 = press, 0 = release */
	uint8_t  flags;                 /* KEBAB_KEYS_EVENT_RESIN */
	uint8_t  raw_pon;
	uint8_t  raw_gpio_up;
	uint8_t  raw_gpio_down;
	uint8_t  raw_ident;
	uint8_t  pad;
};
_Static_assert(sizeof(struct kk_record) == 24, "kebab key record layout");

/* ---------------------------------------------------------------- state */

static struct {
	uint32_t    poll_ms;
	bool        verbose;            /* kebab_keys_raw=1 */
	bool        dev_ready;
	bool        polling;           /* startup succeeded; publish after devfs init */
	uint32_t    errors;
	uint32_t    last_error_report;
	uint32_t    zero_count;
	uint32_t    ident_count;
	uint8_t     last_raw[4];
	bool        have_raw;
	bool        stuck_said[KEBAB_KEYS_KEY_COUNT];
	int         cmajor;
	kebab_keys_state keys;
} kk;

/*
 * Single-producer/single-consumer ring, the same shape as /dev/kebab_touch:
 * the poll thread is the only producer, read(2) is the consumer, both take the
 * mutex (which also gives msleep() a lock to drop), an absent reader loses the
 * oldest record instead of allocating, and the loss is counted.
 */
static struct {
	struct kk_record rec[KK_QUEUE_LEN];
	uint32_t head;
	uint32_t tail;
	uint32_t count;
	uint32_t opens;
	uint64_t pushed;
	uint64_t dropped;
	lck_grp_t *lock_group;
	lck_mtx_t *lock;
} kkq;

/* --------------------------------------------------------------- helpers */

static uint64_t
kk_now_ns(void)
{
	uint64_t ns;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

static void
kkq_push(const struct kk_record *rec)
{
	lck_mtx_lock(kkq.lock);
	if (kkq.count == KK_QUEUE_LEN) {
		kkq.tail = (kkq.tail + 1) % KK_QUEUE_LEN;
		kkq.count--;
		kkq.dropped++;
	}
	kkq.rec[kkq.head] = *rec;
	kkq.head = (kkq.head + 1) % KK_QUEUE_LEN;
	kkq.count++;
	kkq.pushed++;
	wakeup(&kkq);
	lck_mtx_unlock(kkq.lock);
}

static int
kkq_pop(struct kk_record *rec)
{
	int got = 0;

	lck_mtx_lock(kkq.lock);
	if (kkq.count != 0) {
		*rec = kkq.rec[kkq.tail];
		kkq.tail = (kkq.tail + 1) % KK_QUEUE_LEN;
		kkq.count--;
		got = 1;
	}
	lck_mtx_unlock(kkq.lock);
	return got;
}

/*
 * Fill the ring three times with nobody reading, then drain it.  This proves
 * the two properties the interface depends on before the device node exists:
 * the ring never grows past KK_QUEUE_LEN and a missing reader loses the oldest
 * records (counted) while the storage stays a static array.
 */
static bool
kkq_selftest(void)
{
	struct kk_record rec;
	struct kk_record out;
	uint32_t i;
	uint32_t drained = 0;
	bool ok = true;

	bzero(&rec, sizeof(rec));
	rec.magic = KK_EVENT_MAGIC;
	for (i = 0; i < KK_QUEUE_LEN * 3; i++) {
		rec.seq = i;
		kkq_push(&rec);
	}
	if (kkq.count != KK_QUEUE_LEN ||
	    kkq.dropped != 2 * (uint64_t)KK_QUEUE_LEN) {
		ok = false;
	}
	for (i = 0; i < KK_QUEUE_LEN; i++) {
		if (!kkq_pop(&out) || out.seq != 2 * KK_QUEUE_LEN + i) {
			ok = false;
			break;
		}
		drained++;
	}
	if (kkq_pop(&out)) {
		ok = false;
	}
	if (kkq.count != 0) {
		ok = false;
	}

	printf("KEBAB KEYS: queue selftest: %u slots, %u pushed, %llu dropped "
	    "without a reader, %u drained: %s\n", (unsigned)KK_QUEUE_LEN,
	    (unsigned)(KK_QUEUE_LEN * 3), (unsigned long long)kkq.dropped,
	    drained, ok ? "ok" : "FAILED");

	kkq.head = 0;
	kkq.tail = 0;
	kkq.count = 0;
	kkq.opens = 0;
	kkq.pushed = 0;
	kkq.dropped = 0;
	return ok;
}

static void
kk_post(const kebab_keys_event *event)
{
	struct kk_record rec;

	bzero(&rec, sizeof(rec));
	rec.magic = KK_EVENT_MAGIC;
	rec.seq = event->seq;
	rec.timestamp_ns = event->timestamp_ns;
	rec.key = event->key;
	rec.pressed = event->pressed;
	rec.flags = event->flags;
	rec.raw_pon = event->raw_pon;
	rec.raw_gpio_up = event->raw_gpio_up;
	rec.raw_gpio_down = event->raw_gpio_down;
	rec.raw_ident = event->raw_ident;
	kkq_push(&rec);
}

/* ---------------------------------------------------------------- cdev */

static int
kebab_keys_open(__unused dev_t dev, __unused int flags,
    __unused int devtype, __unused struct proc *p)
{
	bool first;

	lck_mtx_lock(kkq.lock);
	first = (kkq.opens++ == 0);
	lck_mtx_unlock(kkq.lock);
	if (first) {
		printf("KEBAB KEYS: /dev/kebab_keys opened\n");
	}
	return 0;
}

static int
kebab_keys_close(__unused dev_t dev, __unused int flags,
    __unused int mode, __unused struct proc *p)
{
	lck_mtx_lock(kkq.lock);
	if (kkq.opens != 0) {
		kkq.opens--;
	}
	lck_mtx_unlock(kkq.lock);
	return 0;
}

/*
 * read(2) returns whole 24-byte records, oldest first.  It blocks until one is
 * available unless O_NONBLOCK is set.  Several readers split the stream; there
 * is one tail, not one per open, exactly as /dev/kebab_touch does it.
 */
static int
kebab_keys_read(__unused dev_t dev, struct uio *uio, int ioflag)
{
	int error;

	while (uio_resid(uio) >= (int)sizeof(struct kk_record)) {
		struct kk_record rec;

		lck_mtx_lock(kkq.lock);
		while (kkq.count == 0) {
			if (ioflag & IO_NDELAY) {
				lck_mtx_unlock(kkq.lock);
				return EAGAIN;
			}
			error = msleep(&kkq, kkq.lock, PRIBIO | PCATCH,
			    "kebabkeys", NULL);
			if (error != 0) {
				lck_mtx_unlock(kkq.lock);
				return error;
			}
		}
		rec = kkq.rec[kkq.tail];
		kkq.tail = (kkq.tail + 1) % KK_QUEUE_LEN;
		kkq.count--;
		lck_mtx_unlock(kkq.lock);

		error = uiomove((const char *)&rec, (int)sizeof(rec), uio);
		if (error != 0) {
			return error;
		}
	}
	return 0;
}

static const struct cdevsw kebab_keys_cdevsw = {
	.d_open     = kebab_keys_open,
	.d_close    = kebab_keys_close,
	.d_read     = kebab_keys_read,
	.d_write    = eno_rdwrt,
	.d_ioctl    = eno_ioctl,
	.d_stop     = eno_stop,
	.d_reset    = eno_reset,
	.d_select   = eno_select,
	.d_mmap     = eno_mmap,
	.d_strategy = eno_strat,
	.d_type     = 0,        /* a record stream, not a tty or a disk */
};

static void
kk_dev_register(void)
{
	void *node;

	kk.cmajor = cdevsw_add(-1, &kebab_keys_cdevsw);
	if (kk.cmajor < 0) {
		printf("KEBAB KEYS: no cdevsw slot for /dev/kebab_keys\n");
		return;
	}
	node = devfs_make_node(makedev(kk.cmajor, 0), DEVFS_CHAR, UID_ROOT,
	    GID_OPERATOR, 0440, "kebab_keys");
	if (node == NULL) {
		printf("KEBAB KEYS: cdev major %d, but no /dev node\n", kk.cmajor);
		cdevsw_remove(kk.cmajor, &kebab_keys_cdevsw);
		kk.cmajor = -1;
		return;
	}
	kk.dev_ready = true;
	printf("KEBAB KEYS: /dev/kebab_keys major %d, %u-byte records, "
	    "%u slots\n", kk.cmajor, (unsigned)sizeof(struct kk_record),
	    (unsigned)KK_QUEUE_LEN);
}

/* ---------------------------------------------------------------- poll */

/*
 * One look at the four registers.  A failed transfer is counted and fed as an
 * invalid sample, which the state machine ignores: it is "no observation", not
 * a release.  All-zero and wrong-identity samples are also fed as invalid; the
 * warning is rate-limited because a silent bus would otherwise be 60 lines a
 * second on the only readable console this port has.
 */
static void
kk_poll(void)
{
	uint8_t raw[4] = { 0, 0, 0, 0 };
	kebab_keys_sample sample;
	kebab_keys_sample_status status;
	kebab_keys_event events[KEBAB_KEYS_MAX_EVENTS];
	uint64_t now_ns;
	size_t count;
	size_t i;
	unsigned k;

	if (kebab_spmi_keys_sample(raw) != 0) {
		kk.errors++;
		if (kk.errors == 1 ||
		    (kk.errors - kk.last_error_report) >= 250) {
			kk.last_error_report = kk.errors;
			printf("KEBAB KEYS: SPMI read error %u (0x0810/0x0c510/"
			    "0x0c610/0x0c504); sample discarded, no key state "
			    "changed\n", kk.errors);
		}
		bzero(&sample, sizeof(sample));
		sample.read_ok = 0;
		(void)kebab_keys_feed(&kk.keys, &sample, kk_now_ns(), events);
		return;
	}

	sample.pon_rt = raw[0];
	sample.gpio_up_rt = raw[1];
	sample.gpio_down_rt = raw[2];
	sample.gpio_ident = raw[3];
	sample.read_ok = 1;
	status = kebab_keys_classify(&sample);
	if (status == KEBAB_KEYS_SAMPLE_ZERO) {
		kk.zero_count++;
		if (kk.zero_count == 1 || (kk.zero_count % 250) == 0) {
			printf("KEBAB KEYS: four zero bytes read (count %u): "
			    "treated as no key, not as every key held\n",
			    kk.zero_count);
		}
	} else {
		kk.zero_count = 0;
	}
	if (status == KEBAB_KEYS_SAMPLE_BAD_IDENTITY) {
		kk.ident_count++;
		if (kk.ident_count == 1 || (kk.ident_count % 250) == 0) {
			printf("KEBAB KEYS: GPIO type 0x%02x at 0x0c504 is not "
			    "0x10 (count %u); sample ignored\n", raw[3],
			    kk.ident_count);
		}
	} else {
		kk.ident_count = 0;
	}
	if (kk.verbose && (!kk.have_raw || memcmp(raw, kk.last_raw, 4) != 0)) {
		printf("KEBAB KEYS: raw pon=0x%02x gpio6=0x%02x gpio7=0x%02x "
		    "ident=0x%02x\n", raw[0], raw[1], raw[2], raw[3]);
		memcpy(kk.last_raw, raw, sizeof(raw));
		kk.have_raw = true;
	}

	now_ns = kk_now_ns();
	count = kebab_keys_feed(&kk.keys, &sample, now_ns, events);

	for (i = 0; i < count; i++) {
		printf("KEBAB KEYS: %s %s raw pon=0x%02x gpio6=0x%02x "
		    "gpio7=0x%02x ident=0x%02x%s\n",
		    kebab_keys_name(events[i].key),
		    events[i].pressed ? "PRESSED" : "released",
		    events[i].raw_pon, events[i].raw_gpio_up,
		    events[i].raw_gpio_down, events[i].raw_ident,
		    (events[i].flags & KEBAB_KEYS_EVENT_RESIN) != 0 ?
		    " (via PON RESIN)" : "");
		kk_post(&events[i]);
	}

	/* Ten seconds of a continuous hold is reported once, not every poll. */
	for (k = 0; k < KEBAB_KEYS_KEY_COUNT; k++) {
		if (kk.keys.key[k].stuck && !kk.stuck_said[k]) {
			kk.stuck_said[k] = true;
			printf("KEBAB KEYS: %s STUCK held for 10 s; no repeat "
			    "press is reported until it is released\n",
			    kebab_keys_name(k));
		} else if (!kk.keys.key[k].stuck) {
			kk.stuck_said[k] = false;
		}
	}
}

static void
kk_thread(void *arg, wait_result_t result)
{
	(void)arg;
	(void)result;
	for (;;) {
		kk_poll();
		delay_for_interval(kk.poll_ms, NSEC_PER_MSEC);
	}
}

/* --------------------------------------------------------------- start */

/*
 * Off unless kebab_keys=1.  The first sample is read and checked before the
 * thread or the device node exists, so a missing arbiter mapping, a silent bus
 * or a page that is not a PM8150 GPIO is a single clear failure at boot rather
 * than a stream of empty records.  No PMIC write is made at any point.
 */
int
kebab_keys_start(void)
{
	kebab_keys_sample first;
	kebab_keys_sample_status status;
	uint32_t enabled = 0;
	uint32_t value = 0;
	uint8_t raw[4] = { 0, 0, 0, 0 };
	uint8_t gpio6[3] = { 0, 0, 0 };
	uint8_t gpio7[3] = { 0, 0, 0 };
	thread_t thread;
	int power, up, down;
	int up_ok = 1;
	int down_ok = 1;
	const char *up_text;
	const char *down_text;

	if (!PE_parse_boot_argn("kebab_keys", &enabled, sizeof(enabled)) ||
	    enabled == 0) {
		return 0;
	}
	kk.poll_ms = 16;
	if (PE_parse_boot_argn("kebab_keys_ms", &value, sizeof(value)) &&
	    value != 0) {
		kk.poll_ms = value;
		if (kk.poll_ms < 4) {
			kk.poll_ms = 4;
		}
		if (kk.poll_ms > 1000) {
			kk.poll_ms = 1000;
		}
	}
	kk.verbose = PE_parse_boot_argn("kebab_keys_raw", &value,
	    sizeof(value)) && value != 0;

	if (kebab_spmi_keys_sample(raw) != 0) {
		printf("KEBAB KEYS: the PM8150 key registers could not be read "
		    "(0x0810 / 0x0c510 / 0x0c610 / 0x0c504); no keys. "
		    "kebab_pon_verbose=1 shows the arbiter traffic\n");
		return -1;
	}
	first.pon_rt = raw[0];
	first.gpio_up_rt = raw[1];
	first.gpio_down_rt = raw[2];
	first.gpio_ident = raw[3];
	first.read_ok = 1;
	status = kebab_keys_classify(&first);
	if (status != KEBAB_KEYS_SAMPLE_OK) {
		printf("KEBAB KEYS: first sample pon=0x%02x gpio6=0x%02x "
		    "gpio7=0x%02x ident=0x%02x is %s; no keys\n",
		    raw[0], raw[1], raw[2], raw[3],
		    status == KEBAB_KEYS_SAMPLE_ZERO ? "all zero (silent bus)" :
		    status == KEBAB_KEYS_SAMPLE_BAD_IDENTITY ?
		    "not a PM8150 GPIO page" : "unreadable");
		return -1;
	}

	/*
	 * The vendor pinctrl state is an enabled input with a pull-up.  Reading
	 * it is a diagnosis, not a repair: this driver never writes the PMIC.
	 * A key whose pin is not configured that way is withheld, because an
	 * inactive input that reads low would otherwise become a permanent
	 * press of an active-low key.
	 */
	if (kebab_spmi_keys_config(gpio6, gpio7) == 0) {
		up_ok = (gpio6[0] & 0x03) == 0 && (gpio6[1] & 0x07) < 4 &&
		    (gpio6[2] & 0x80) != 0;
		down_ok = (gpio7[0] & 0x03) == 0 && (gpio7[1] & 0x07) < 4 &&
		    (gpio7[2] & 0x80) != 0;
		printf("KEBAB KEYS: GPIO6 mode=0x%02x pull=0x%02x en=0x%02x -> "
		    "%s; GPIO7 mode=0x%02x pull=0x%02x en=0x%02x -> %s\n",
		    gpio6[0], gpio6[1], gpio6[2],
		    up_ok ? "enabled input with pull-up" :
		    "NOT an enabled input with pull-up",
		    gpio7[0], gpio7[1], gpio7[2],
		    down_ok ? "enabled input with pull-up" :
		    "NOT an enabled input with pull-up");
		if (!up_ok) {
			printf("KEBAB KEYS: volume-up withheld: PM8150 GPIO6 is "
			    "not an enabled input with a pull-up, and this "
			    "driver does not write the PMIC\n");
		}
		if (!down_ok) {
			printf("KEBAB KEYS: volume-down withheld: PM8150 GPIO7 "
			    "is not an enabled input with a pull-up, and this "
			    "driver does not write the PMIC\n");
		}
	} else {
		printf("KEBAB KEYS: could not read the GPIO6/GPIO7 pin setup; "
		    "leaving both volume keys enabled\n");
	}

	power = (raw[0] & KEBAB_KEYS_PON_KPDPWR) != 0;
	up = (raw[1] & KEBAB_KEYS_GPIO_LEVEL) == 0;
	down = ((raw[2] & KEBAB_KEYS_GPIO_LEVEL) == 0) ||
	    (raw[0] & KEBAB_KEYS_PON_RESIN) != 0;
	up_text = !up_ok ? "withheld" : up ? "HELD" : "released";
	down_text = !down_ok ? "withheld" : down ? "HELD" : "released";
	printf("KEBAB KEYS: sources: PON RT_STS 0x0810, PM8150 GPIO6 RT_STS "
	    "0x0c510, GPIO7 RT_STS 0x0c610, GPIO type 0x0c504 (expected 0x10)\n");
	printf("KEBAB KEYS: first sample pon=0x%02x gpio6=0x%02x gpio7=0x%02x "
	    "ident=0x%02x -> power %s, volume-up %s, volume-down %s%s\n",
	    raw[0], raw[1], raw[2], raw[3],
	    power ? "HELD" : "released", up_text, down_text,
	    (down_ok && (raw[0] & KEBAB_KEYS_PON_RESIN) != 0) ?
	    " (PON RESIN)" : "");

	kkq.lock_group = lck_grp_alloc_init("kebabkeys", LCK_GRP_ATTR_NULL);
	kkq.lock = lck_mtx_alloc_init(kkq.lock_group, LCK_ATTR_NULL);
	if (kkq.lock == NULL) {
		printf("KEBAB KEYS: no mutex for the record queue\n");
		return -1;
	}
	if (!kkq_selftest()) {
		printf("KEBAB KEYS: queue selftest failed; no /dev/kebab_keys\n");
		return -1;
	}

	kebab_keys_reset(&kk.keys);
	kebab_keys_enable(&kk.keys, KEBAB_KEYS_KEY_VOLUME_UP, up_ok);
	kebab_keys_enable(&kk.keys, KEBAB_KEYS_KEY_VOLUME_DOWN, down_ok);
	if (kk.verbose) {
		memcpy(kk.last_raw, raw, sizeof(raw));
		kk.have_raw = true;
	}

	if (kernel_thread_start((thread_continue_t)kk_thread, NULL,
	    &thread) != KERN_SUCCESS) {
		printf("KEBAB KEYS: no polling thread\n");
		return -1;
	}
	thread_deallocate(thread);

	/*
	 * Publish the node only after the thread exists: the other order would
	 * leave a device that can be opened and then never receives a record.
	 */
	kk.polling = true;
	printf("KEBAB KEYS: node publication deferred until devfs is ready\n");
	printf("KEBAB KEYS: polling every %u ms%s\n", kk.poll_ms,
	    kk.verbose ? ", raw changes shown" : "");
	return 0;
}

/* Polling is boot-owned and permanent. Publishing the character device is a
 * separate, idempotent normal phase after devfs_kernel_mount(). No sample,
 * debounce or record layout is changed by this timing correction. */
static LCK_GRP_DECLARE(kk_publish_group, "kebabkeys-publish");
static LCK_MTX_DECLARE(kk_publish_lock, &kk_publish_group);
void
kebab_keys_publish(void)
{
	lck_mtx_lock(&kk_publish_lock);
	if (kk.polling && !kk.dev_ready) { kk_dev_register(); }
	lck_mtx_unlock(&kk_publish_lock);
}

bool
kebab_keys_node_selftest(void)
{
	if (!kk.polling) {
		printf("KEBAB DRIVER TEST: keys devfs SKIP (polling not enabled)\n");
		return true;
	}
	vnode_t node = NULL;
	int error = vnode_lookup("/dev/kebab_keys", 0, &node, vfs_context_kernel());
	bool passed = kk.dev_ready && error == 0 && node != NULL &&
	    vnode_vtype(node) == VCHR && vnode_specrdev(node) == makedev(kk.cmajor, 0);
	if (node != NULL) { vnode_put(node); }
	printf("KEBAB DRIVER TEST: keys devfs %s (lookup=%d major=%d record=%u)\n",
	    passed ? "PASS" : "FAIL", error, kk.cmajor, (unsigned)sizeof(struct kk_record));
	return passed;
}

#endif /* KEBAB */
