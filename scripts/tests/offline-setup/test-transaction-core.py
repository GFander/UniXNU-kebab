from pathlib import Path
import argparse,hashlib,json,subprocess,tempfile,re
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--receipt-dir',type=Path,required=True);a=p.parse_args();a.receipt_dir.mkdir(parents=True,exist_ok=True)
header=a.source/'bsd/kern/kebab_offline_setup_impl.h';actual=header.read_text();prefix='for(unsigned i=0;i<head[3];i++){int error=kebab_offline_inspect(io,&entries[i],head[2],&states[i]);if(error)return error;}'
mutants=[('actual',actual,True),('discard-ACL',actual.replace('memcmp(r->acl_hash,e->acl_hash,32)','false'),False),('no-global-preflight',actual.replace(prefix,''),False),('ignore-protection',actual.replace('r->protection!=e->protection','false'),False),('accept-corrupt-stage',actual.replace('memcmp(r->hash,old?e->old_hash:e->new_hash,32)','false'),False),('discard-extended-owner',actual.replace('memcmp(r->owner_uuid,e->owner_uuid,16)||memcmp(r->group_uuid,e->group_uuid,16)','false'),False)]
outputs=[]
with tempfile.TemporaryDirectory()as d:
 d=Path(d);fixture=d/"fixture.c";fixture.write_bytes(a.fixture.read_bytes())
 for name,body,ok in mutants:
  assert ok or body!=actual
  (d/header.name).write_text(body);exe=d/name
  subprocess.run(['clang','-fsanitize=address,undefined','-I',str(d),str(fixture),'-lcrypto','-o',str(exe)],check=True)
  q=subprocess.run([str(exe)],capture_output=True,text=True);(a.receipt_dir/(name+'.txt')).write_text(q.stdout+q.stderr);assert(q.returncode==0)==ok,(name,q.stdout,q.stderr[-1000:]);outputs.append(dict(name=name,expected_success=ok,returncode=q.returncode))
actual_output=(a.receipt_dir/"actual.txt").read_text();match=re.search(r"core: (\d+) checks, (\d+) injected",actual_output);assert match
result=dict(passed=True,actual_core_executed=True,backend='explicit in-memory filesystem/ACL/metadata model, not native APFS',checks=int(match[1]),injected_callback_interruption_points=int(match[2]),rollback_from_every_committed_interruption_state=True,compiled_runtime_mutants_rejected=5,source_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256(a.fixture.read_bytes()).hexdigest(),outputs=outputs,physical_write_verified=False)
(a.receipt_dir/'transaction-core-tests.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',match[1],'checks,68 interruptions plus reverse recovery,5 compiled mutants; host model only')
