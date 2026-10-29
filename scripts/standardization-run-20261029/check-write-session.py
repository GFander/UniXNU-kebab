from pathlib import Path
import json,re,subprocess,sys,time
r=Path('D:/LOS/HiOS/p1-run/prelink-fix');v=sys.argv[1]
def send(command,label,seconds=3):
    path=r/(v+'-'+label+'.log')
    if not (label.startswith('write-check-') and path.exists()):
        subprocess.run([sys.executable,str(r.parent/'console-cap.py'),'--send',command,'--log',str(path),'--ack-seconds',str(seconds)],capture_output=True,text=True,check=True)
    print(command,'captured',path.stat().st_size,'bytes',flush=True)
    value=path.read_text(errors='replace')
    return value,re.sub(r'\s+','',re.sub(r'^\[[^\]]+\]\s*','',value,flags=re.M))
proof=json.loads((r.parents[1]/'standardization-run-20261029/apfs-write-proof/proof.json').read_text())
for path,size,sha in [('/sbin/launchd',541600,'12370e5ee35bf28ae479b6c4b4455f4f4693ab803e37c5eb3032537c18cac61c'),
    ('/usr/lib/dyld',922912,'5806834564fe6462ae981d0915d612b1eb2f6f168822b5d278bee56254933757')]:
    raw,flat=send('INST CHECK '+path,'write-check-'+path.rsplit('/',1)[-1])
    assert re.sub(r'\s+','',f'INST CHECK {path} {size} {sha}') in flat
raw,flat=send('INST CHECK '+proof['path'],'write-check-proof')
external_proof_hash=re.sub(r'\s+','',f'INST CHECK {proof["path"]} {proof["size"]} {proof["sha256"]}') in flat
if not external_proof_hash:
    assert 'refused' in raw # Preserve the installer's existing destination scope.
    boot=(r/(v+'-preboot-write-boot.log')).read_text(errors='replace')
    boot=re.sub(r'\s+','',re.sub(r'^\[[^\]]+\]\s*','',boot,flags=re.M))
    assert f'write/readbytes={proof["size"]}sha256={proof["sha256"]}PASS' in boot
    assert 'durableflushfilesystem=0UFS=0' in boot
send('DRV?','drivers')
reset,flat=send('FASTBOOT','write-normal-fastboot')
assert all(x in reset for x in ['VFS_SYNC + SYNCHRONIZE CACHE ok','restart reason verified: read back 0x04','IMEM read back 0x77665500 (MATCH)'])
for _ in range(40):
    devices=subprocess.check_output(['D:/TOOL/platform-tools/fastboot.exe','devices'],text=True)
    if re.search(r'c073a488\s+fastboot',devices):break
    time.sleep(.5)
else:raise SystemExit('Normal reset did not return a fastboot device')
(r/(v+'-write-session.json')).write_text(json.dumps({'version':v,'system_file_hashes_unchanged':True,'proof':proof,
    'proof_kernel_full_hash_verified':True,'external_inst_check_proof_hash':external_proof_hash,
    'normal_sync_reset_verified':True,'device':devices.strip()},indent=2)+'\n')
print('PASS: System files unchanged, proof full SHA, actual driver registry and normal sync/reset')
