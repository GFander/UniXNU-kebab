from pathlib import Path
r=Path(__file__).parent;p=Path('bsd/kern/kern_newsysctl.c');s=(r/'before'/p).read_text()
at=s.index('\nsysctl_root(boolean_t from_kernel, boolean_t string_is_canonical,')+1
end=s.index('\n}\n',at)+3
original=s[at:end];body=original
anchor='\tboolean_t namestring_started = FALSE;'
assert body.count(anchor)==1
body=body.replace(anchor,anchor+'''
#if defined(KEBAB)
\tuint32_t kebab_trace_arg = 0;
\tboolean_t kebab_trace = FALSE;
#endif''')
anchor='\tif ((oid->oid_kind & CTLTYPE) == CTLTYPE_NODE) {\n\t\ti = oid_handler'
assert body.count(anchor)==1
trace='''#if defined(KEBAB)
\t/* Default-off, PID1-only normal trace. OID is referenced and geometry is
\t * unlocked here; observe the existing call without changing its result. */
\tkebab_trace = !from_kernel && req->p != PROC_NULL && proc_pid(req->p) == 1 &&
\t    PE_parse_boot_argn("kebab_sysctl_trace", &kebab_trace_arg, sizeof(kebab_trace_arg)) && kebab_trace_arg != 0;
\tif (kebab_trace) {
\t\tprintf("KEBAB SYSCTL: enter %.*s leaf=%s handler=0x%llx oldlen=%llu newlen=%llu\\n",
\t\t    192, namestring == NULL ? oid->oid_name : namestring, oid->oid_name,
\t\t    (unsigned long long)(uintptr_t)oid_handler,
\t\t    (unsigned long long)req->oldlen, (unsigned long long)req->newlen);
\t}
#endif
'''
body=body.replace(anchor,trace+anchor)
anchor='\terror = i;'
assert body.count(anchor)==1
body=body.replace(anchor,anchor+'''
#if defined(KEBAB)
\tif (kebab_trace) {
\t\tprintf("KEBAB SYSCTL: leave %.*s error=%d copied=%llu\\n", 192,
\t\t    namestring == NULL ? oid->oid_name : namestring, i, (unsigned long long)req->oldidx);
\t}
#endif''')
(r/'after'/p).write_text(s[:at]+body+s[end:],newline='\n')
