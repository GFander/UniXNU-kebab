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
#include <machine/machine_routines.h>

#define KEBAB_KLOG_SIZE      (64u * 1024u)
#define KEBAB_KLOG_CHUNK     1024u

/* Written by the console sink, read by the dump.  Single writer, single CPU. */
static char              kebab_klog[KEBAB_KLOG_SIZE];
static volatile uint32_t kebab_klog_head;
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

void
kebab_fwlog_report(uint32_t max_bytes)
{
	uint32_t head, avail, want, start, i, used;
	char     chunk[KEBAB_KLOG_CHUNK + 1];

	kebab_klog_ready = TRUE;
	head = kebab_klog_head;
	avail = head < KEBAB_KLOG_SIZE ? head : KEBAB_KLOG_SIZE;
	if (avail == 0) {
		printf("KEBAB FWLOG: ring=%u written=%u dump=0\n", KEBAB_KLOG_SIZE, head);
		printf("KEBAB FWLOG: ---- empty ----\n");
		return;
	}
	want = (max_bytes == 0 || max_bytes > avail) ? avail : max_bytes;
	start = head - want;
	printf("KEBAB FWLOG: ring=%u written=%u dump=%u%s\n", KEBAB_KLOG_SIZE, head, want,
	    want < avail ? " (tail)" : "");
	printf("KEBAB FWLOG: ---- begin ----\n");

	while (want) {
		used = want > KEBAB_KLOG_CHUNK ? KEBAB_KLOG_CHUNK : want;
		for (i = 0; i < used; i++) {
			chunk[i] = kebab_klog[(start + i) % KEBAB_KLOG_SIZE];
		}
		chunk[used] = 0;
		printf("%s", chunk);
		start += used;
		want -= used;
	}
	printf("\nKEBAB FWLOG: ---- end ----\n");
}
