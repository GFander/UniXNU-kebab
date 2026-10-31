"""Snapshot the resident console ring before its own output can overwrite it."""
from pathlib import Path
import difflib,json,subprocess
r=Path(__file__).parent;d=r/'fwlog-snapshot';name='bsd/dev/kebab_fwlog.c'
p=d/'before'/name
if not p.exists():
    got=json.loads(subprocess.run(['ssh','ubuntualbert','python3 source-reader.py /home/albert/s4/x'],input=json.dumps([name]),text=True,capture_output=True,check=True).stdout)
    p.parent.mkdir(parents=True,exist_ok=True);p.write_text(got[name]['source'],newline='\n')
before=p.read_text();after=before
previous=d/'after'/name
transport_before=previous.read_text() if previous.exists() else None
after=after.replace('#include <sys/systm.h>','#include <sys/systm.h>\n#include <kern/debug.h>',1)
needle='static volatile uint32_t kebab_klog_head;';assert after.count(needle)==1
after=after.replace(needle,needle+'''
/* Fixed resident storage: no allocation in a panic. Snapshot before printing,
 * since every printf below is fed back into the original ring by the sink.
 * Separate normal/panic slots let a panic interrupt a normal dump without
 * waiting for that dump's owner. Same-context reentry is refused.
 * Like the ring writer, this copy is qualified for the current single CPU. */
static struct kebab_klog_dump {
 uint32_t busy;
 char bytes[KEBAB_KLOG_SIZE];
} kebab_klog_dumps[2];
''')
needle='''	kebab_klog_ready = TRUE;
	head = kebab_klog_head;
	avail = head < KEBAB_KLOG_SIZE ? head : KEBAB_KLOG_SIZE;
''';assert after.count(needle)==1
after=after.replace(needle,'''	struct kebab_klog_dump *dump = &kebab_klog_dumps[panic_active() ? 1 : 0];
	if (!__sync_bool_compare_and_swap(&dump->busy, 0, 1)) {
		printf("KEBAB FWLOG: dump already active; retry later\\n");
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
''')
after=after.replace('''		printf("KEBAB FWLOG: ---- empty ----\\n");
		return;''','''		printf("KEBAB FWLOG: ---- empty ----\\n");
		__sync_lock_release(&dump->busy);
		return;''')
needle='\twant = (max_bytes == 0 || max_bytes > avail) ? avail : max_bytes;\n\tstart = head - want;';assert after.count(needle)==2
at=after.index(needle,after.index(needle)+len(needle));after=after[:at]+after[at:].replace(needle,'\tstart = 0;',1)
after=after.replace('chunk[i] = kebab_klog[(start + i) % KEBAB_KLOG_SIZE];','chunk[i] = dump->bytes[start + i];')
needle='\tprintf("\\nKEBAB FWLOG: ---- end ----\\n");';assert after.count(needle)==1
after=after.replace(needle,needle+'\n\t__sync_lock_release(&dump->busy);')
helper='''/* Normal dispatch runs on the USB event thread: drain between chunks so
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

'''
needle='void\nkebab_fwlog_report(uint32_t max_bytes)';assert after.count(needle)==1
after=after.replace(needle,helper+needle)
for needle in ['\t\tprintf("KEBAB FWLOG: ---- empty ----\\n");','\tprintf("KEBAB FWLOG: ---- begin ----\\n");','\t\tprintf("%s", chunk);','\tprintf("\\nKEBAB FWLOG: ---- end ----\\n");']:
    assert after.count(needle)==1
    indent='\t\t' if needle.startswith('\t\t') else '\t'
    after=after.replace(needle,needle+'\n'+indent+'kebab_fwlog_drain();')
if transport_before and 'kebab_fwlog_drain' not in transport_before:
    p=d/'transport-before'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(transport_before,newline='\n')
    (d/'transport-drain.patch').write_text(''.join(difflib.unified_diff(transport_before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)),newline='\n')
p=d/'after'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(after,newline='\n')
(d/'0197-kebab-fwlog-snapshot.patch').write_text(''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+name,tofile='b/'+name)),newline='\n')
print('Prepared fixed-buffer, IRQ-state-preserving FWLOG snapshot; no allocation or driver lock')
