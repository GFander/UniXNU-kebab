from pathlib import Path
import json,hashlib,re,sys,subprocess
r=Path(__file__).resolve().parent;b=r.parent/'p1-run/prelink-fix';v=sys.argv[1]
assert v in ('0372','0373','0376','0378','0380','0382','0383','0384','0386','0387','0388','0389','0390','0391','0393','0394','0395','0396','0397','0399','0400','0401','0402','0403','0405','0406','0407','0408','0409','0410','0411','0412','0413','0414','0415','0417','0418','0419','0420','0421','0422')
raw=(r/(v+'-media-boot.log')).read_bytes();s=raw.decode();digest=hashlib.sha256(raw).hexdigest()
assert json.loads((r/(v+'-capture-provenance.json')).read_text())['sha256']==digest
for needle in ['KEBAB NONCE ROOT: restoring opaque keyblob; generation skipped',
 'KEBAB NONCE ROOT: restored consistency witness PASS',
 'KEBAB DATA ROOT: restoring opaque keyblob; generation skipped',
 'KEBAB DATA ROOT: restored hardware consistency witness PASS',
 'KEBAB AES NONCE: NativeAES registered with hardware-root selector 2212',
 'KEBAB LIVE ROOTS: existing nonce+Data hardware restore PASS; generation disabled',
 'KEBAB NVRAM: existing native generation-zero proxy adoption PASS',
 'KEBAB NVRAM: native IODTNVRAM/controller published after durable Data banks',
 'driver start leave AppleImage4 result=1','probe leave busy=',
 'KEBAB PMU: primary PM8150 PON observed subtype=4',
 'offset reused error=0']:
 assert needle in s,needle
assert 'busy timeout[0], (60s): \'defaults\'' not in s
events={int(i):value for i,value in re.findall(r'KEBAB DEFAULTS(?: LIVE)?\[(\d+)\]: ([^\n]+)',s)}
balance=0;busy=[]
for i,value in sorted(events.items()):
 m=re.match(r'busy before=(\d+) delta=(-?\d+) origin=(\S+)',value)
 if m:
  before,delta,origin=m.groups();assert int(before)==balance,(i,value,balance)
  balance+=int(delta);busy.append((i,int(before),int(delta),origin))
assert balance==0 and len(busy)>=4,busy
assert 'KEBAB QSEE: key generation route=' not in s
assert 'WATCHDOG CRITICAL:' not in s and not re.search(r'watchdogd\[\d+\] exited',s)
assert s.count('KEBAB WATCHDOG: original user-client accepted type=1')==1
beats=list(map(float,re.findall(r'\[\s*(\d+\.\d+)\] KEBAB WATCHDOG METHOD: selector=1 scalar=0/0 struct=0/0 result=0x0',s)))
live=[(float(t),int(p),int(n)) for t,c,p,n,e,left in re.findall(r'\[\s*(\d+\.\d+)\] KEBAB WATCHDOG: clients=(\d+) polls=(\d+) beats=(\d+) enabled=(\d+) remaining=(-?\d+)',s) if int(c)==1 and int(e)==1 and int(left)>0]
assert len(beats)>=9 and beats[-1]-beats[0]>=80
assert len(live)>=3 and live[-1][2]>live[0][2] and live[-1][0]-live[0][0]>=60
assert live[-1][0]>=180,'must observe beyond previous 146-second defaults panic'
subprocess.run([sys.executable,str(r/'analyse-systembag.py'),v],check=True,stdout=subprocess.DEVNULL)
nvram=re.findall(r'KEBAB NVRAM: encrypted Data banks cold restored error=0 valid=1/1 current=([01]) generation=(\d+) root0600 classD',s);assert len(nvram)==1
assert int(nvram[0][1])>=2 and 'banks durably initialized' not in s
q=dict(version=v,defaults_matching_complete=True,actual_log_sha256=digest,native_sha256=hashlib.sha256((b/('kernel-'+v+'.kebab')).read_bytes()).hexdigest(),real_apple_image4_start_succeeded=True,actual_busy_reference_released=True,busy_lifecycle=busy,final_busy_state=balance,actual_waits=['IOAESAccelerator','IOResources/IONVRAM','IODTNVRAM'],hardware_nonce_and_data_restored_without_generation=True,native_nvram_existing_proxy_adopted=True,nvram_encrypted_data_banks_cold_restored=True,nvram_restored_bank=int(nvram[0][0]),nvram_restored_generation=int(nvram[0][1]),nvram_hardware_or_rollback_claim=False,nvram_durable_write_calls=len(re.findall('KEBAB NVRAM: durable bank=',s)),watchdog_enabled=True,original_iokit_monitoring_preserved=True,watchdog_heartbeat_times=beats,original_locked_observations=live,systembag_and_hardware_c_d_preserved=True,rtc_record_reused=True,panic=re.findall(r'panic\(cpu[^\n]+',s)[:1],full_ios_or_springboard=False,gpu_display_input_started=False)
(r/(v+'-defaults-qualified.json')).write_text(json.dumps(q,indent=2)+'\n');print(json.dumps(q,indent=2))
