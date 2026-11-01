from pathlib import Path
import hashlib,json,re,subprocess,sys
w=Path(sys.argv[1]);source=w/'ipc-port-state-production-test.c';body=source.read_text()
def run(name,text):
 p=w/(name+'.c');p.write_text(text);exe=w/name
 c=subprocess.run(['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-format',str(p),'-o',str(exe)],capture_output=True,text=True)
 assert c.returncode==0,(name,c.stderr)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 return result
result=run('actual',body);assert result.returncode==0,result.stderr;print(result.stdout.strip(),flush=True)
mutants=[
 ('wrong-trap','site->trap!=-47','site->trap!=-48'),
 ('wrong-port','value.remote=(uint32_t)site->ports','value.remote=(uint32_t)(site->ports>>32)'),
 ('drop-sync','if((send||sync)&&MACH_PORT_VALID(value.remote)){','if(send&&MACH_PORT_VALID(value.remote)){'),
 ('drop-next-reference','if(follow)ip_reference(next);','if(false)ip_reference(next);'),
 ('leak-owned-reference','if(owned)ip_release(port);','if(false)ip_release(port);'),
 ('nested-lock','port=next;owned=true;ip_mq_lock(port);','port=next;owned=true;ip_mq_lock(port);ip_mq_lock(port);'),
 ('wrong-space-kind','ipc_space_kernel)?2:','ipc_space_kernel)?1:'),
 ('drop-dead-check','if(ip_active(port)){','if(true){'),
 ('drop-bound','count<KEBAB_IPC_PATH_MAX;','count<KEBAB_IPC_PATH_MAX+1;'),
 ('drop-visited','next)follow=false;','next)follow=true;'),
 ('wrong-queue','state->queued=port->ip_messages.imq_msgcount','state->queued=port->ip_messages.imq_qlimit'),
 ('drop-state-line','KEBAB %s IPC-STATE:','KEBAB %s IPC-BROKEN:'),
 ('oversized-state-line','KEBAB %s IPC-STATE:', 'X'*180+'KEBAB %s IPC-STATE:'),
]
for label,old,new in mutants:
 assert body.count(old)==1,(label,body.count(old))
 result_mut=run(label,body.replace(old,new));assert result_mut.returncode!=0,(label,'incorrect behavior survived')
 print(label,'compiled behavioral mutant rejected',flush=True)
receipt=dict(passed=True,version='0475',cases=int(re.search(r'PASS (\d+)',result.stdout)[1]),compiled_behavioral_mutants=len(mutants),fixture_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),physical_test=False,native_IPC_task_scheduler_reference_model=True)
(w/'test-ipc-port-state-result-0475.json').write_text(json.dumps(receipt,indent=2)+'\n')
