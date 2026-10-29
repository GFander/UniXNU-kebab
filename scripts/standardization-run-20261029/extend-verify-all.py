"""Wire actual patched-source fault tests into the existing offline verifier."""
from pathlib import Path
r=Path(__file__).parent;s=(r/'verify-all-before.sh').read_text()
helper='''# Host fault tests consume the just-replayed production functions, not snapshots.
check_patch_hosttest() {
 local name=$1 script=$2 rc
 shift 2
 if [ ! -d "$tmp/xnu" ]; then
  skip "$name" "no patched xnu tree (run without --fast)"
  return
 fi
 if ! command -v clang >/dev/null 2>&1; then
  skip "$name" "host clang is unavailable"
  return
 fi
 run "$name" "$python" "$repo/scripts/$script" "$@"
 rc=$?
 if [ $rc -ne 0 ]; then
  fail "$name" "$(why "$logdir/$name.log" "$rc")"
 else
  pass "$name" "production-source fault tests passed"
 fi
}

'''
needle='check_panel() {';assert s.count(needle)==1;s=s.replace(needle,helper+needle)
host_helper='''check_host_patches() {
 local name=host-patches p count=0
 if [ ! -d "$tmp/xnu" ]; then
  skip "$name" "no patched xnu tree (run without --fast)"
  return
 fi
 for p in "$repo/patches/hosts/macos-dxb-13.5/"*.patch; do
  if ! git -C "$tmp/xnu" apply --check --whitespace=nowarn "$p" >"$logdir/$name.log" 2>&1 ||
     ! git -C "$tmp/xnu" apply --whitespace=nowarn "$p" >>"$logdir/$name.log" 2>&1; then
   fail "$name" "cannot apply ${p##*/} after the common series"
   return
  fi
  count=$((count + 1))
 done
 pass "$name" "$count host patches apply after the common series"
}

'''
s=s.replace('check_panel() {',host_helper+'check_panel() {',1)
needle='\ncheck_console_path\n';assert s.count(needle)==1
calls='''check_patch_hosttest panic-reset test-panic-fastboot.py "$tmp/xnu/bsd/dev/kebab_usb.c" "$tmp/xnu/osfmk/arm64/machine_routines.c" "$tmp/xnu/bsd/dev/kebab_reboot.c"
check_patch_hosttest scm-query standardization-run-20261029/scm-probe/test-probe.py "$tmp/xnu/bsd/kern/kebab_scm_probe.c"
check_patch_hosttest apfs-proof standardization-run-20261029/apfs-write-proof/test-proof.py "$tmp/xnu/bsd/kern/kebab_apfs_probe.c"
check_patch_hosttest apfs-gates standardization-run-20261029/apfs-write-proof/test-mount-gates.py "$tmp/xnu/bsd/kern/kebab_apfs_probe.c"
check_patch_hosttest reset-sync standardization-run-20261029/apfs-write-proof/test-sync.py "$tmp/xnu/bsd/dev/kebab_install.c"
check_patch_hosttest static-upl standardization-run-20261029/static-upl/test-static-upl.py "$tmp/xnu/iokit/Kernel/IOMemoryDescriptor.cpp"
check_patch_hosttest host-validator standardization-run-20261029/series-repair/test-validator.py "$tmp/xnu/tools/lldbmacros/core/syntax_checker.py"
check_patch_hosttest fwlog-snapshot standardization-run-20261029/fwlog-snapshot/test-snapshot.py "$tmp/xnu/bsd/dev/kebab_fwlog.c"
'''
s=s.replace(needle,'\ncheck_host_patches'+needle+calls)
(r/'verify-all.sh').write_text(s,newline='\n')
print('Added eight production-source host checks plus host patch replay')
