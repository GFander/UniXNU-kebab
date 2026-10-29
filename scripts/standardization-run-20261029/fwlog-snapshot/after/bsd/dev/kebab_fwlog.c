/*
 * KEBAB FWLOG: a kernel-side console ring, dumped by the FWLOG? command.
 *
 * The original implementation read a ring the firmware was supposed to place at
 * a fixed physical address.  On this platform that page reads as all ones, so
 * every dump after a crash reported "no KFLG magic" and the boot log was lost.
 *
 * The ring now lives in the kernel.  kebab_usb_log() - the single sink every
 * console character passes through - feeds it, so it captures printf output from
 * the moment the console exists.  Reading it needs no mapping, no allocation and
 * no lock, which is why it also works while the kernel is panicking: the panic
 * dump itself overwrites the small firmware buffer, but this one it cannot reach.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <kern/debug.h>
#include <machine/machine_routines.h>

#define KEBAB_KLOG_SIZE      (64u * 1024u)
#define KEBAB_KLOG_CHUNK     1024u

/* Written by the console sink, read by the dump.  Single writer, single CPU. */
static char              kebab_klog[KEBAB_KLOG_SIZE];
static volatile uint32_t kebab_klog_head;
/* Fixed resident storage: no allocation in a panic. Snapshot before printing,
 * since every printf below is fed back into the original ring by the sink.
 * Separate normal/panic slots let a panic interrupt a normal dump without
 * waiting for that dump's owner. Same-context reentry is refused.
 * Like the ring writer, this copy is qualified for the current single CPU. */
static struct kebab_klog_dump {
 uint32_t busy;
 char bytes[KEBAB_KLOG_SIZE];
} kebab_klog_dumps[2];

/* Starts enabled: the first console character must already be captured. */
static boolean_t         kebab_klog_ready = TRUE;

void
kebab_klog_put(const char *buf, size_t len)
{
	uint32_t head;
	size_t   i;

	if (!kebab_klog_ready) {
		return;
	}
	head = kebab_klog_head;
	for (i = 0; i < len; i++) {
		kebab_klog[head % KEBAB_KLOG_SIZE] = buf[i];
		head++;
	}
	kebab_klog_head = head;
}

void
kebab_fwlog_attach(void)
{
	/* Nothing to map any more; kept so the existing call sites still link. */
}

void
kebab_fwlog_attach_at_init(void)
{
	kebab_klog_ready = TRUE;
}

void
kebab_fwlog_boot_summary(void)
{
	kebab_klog_ready = TRUE;
}

/* Normal dispatch runs on the USB event thread: drain between chunks so
 * a 64KiB dump cannot fill the 16KiB TX ring while that thread is printing.
 * Panic logging already drains synchronously in kebab_usb_log(). */
static void
kebab_fwlog_drain(void)
{
#if defined(KEBAB)
 extern void kebab_usb_log_flush(void);
 if (!panic_active()) kebab_usb_log_flush();
#endif
}

void
kebab_fwlog_report(uint32_t max_bytes)
{
	uint32_t head, avail, want, start, i, used;
	char     chunk[KEBAB_KLOG_CHUNK + 1];

	struct kebab_klog_dump *dump = &kebab_klog_dumps[panic_active() ? 1 : 0];
	if (!__sync_bool_compare_and_swap(&dump->busy, 0, 1)) {
		printf("KEBAB FWLOG: dump already active; retry later\n");
		return;
	}
	kebab_klog_ready = TRUE;
	/* Prevent a normal-context writer from preempting this single-CPU copy.
	 * Restore the caller's exact IRQ state before any console output. */
	boolean_t irq = ml_set_interrupts_enabled(FALSE);
	head = kebab_klog_head;
	avail = head < KEBAB_KLOG_SIZE ? head : KEBAB_KLOG_SIZE;
	want = (max_bytes == 0 || max_bytes > avail) ? avail : max_bytes;
	start = head - want;
	for (i = 0; i < want; i++) {
		dump->bytes[i] = kebab_klog[(start + i) % KEBAB_KLOG_SIZE];
	}
	ml_set_interrupts_enabled(irq);
	if (avail == 0) {
		printf("KEBAB FWLOG: ring=%u written=%u dump=0\n", KEBAB_KLOG_SIZE, head);
		printf("KEBAB FWLOG: ---- empty ----\n");
		kebab_fwlog_drain();
		__sync_lock_release(&dump->busy);
		return;
	}
	start = 0;
	printf("KEBAB FWLOG: ring=%u written=%u dump=%u%s\n", KEBAB_KLOG_SIZE, head, want,
	    want < avail ? " (tail)" : "");
	printf("KEBAB FWLOG: ---- begin ----\n");
	kebab_fwlog_drain();

	while (want) {
		used = want > KEBAB_KLOG_CHUNK ? KEBAB_KLOG_CHUNK : want;
		for (i = 0; i < used; i++) {
			chunk[i] = dump->bytes[start + i];
		}
		chunk[used] = 0;
		printf("%s", chunk);
		kebab_fwlog_drain();
		start += used;
		want -= used;
	}
	printf("\nKEBAB FWLOG: ---- end ----\n");
	kebab_fwlog_drain();
	__sync_lock_release(&dump->busy);
}
