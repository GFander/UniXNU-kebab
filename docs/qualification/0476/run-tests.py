from pathlib import Path
import hashlib,json,re,subprocess,sys
w=Path(sys.argv[1]);source=w/'ipc-service-name-production-test.c';body=source.read_text()
model=w/'ipc';model.mkdir(exist_ok=True);(model/'ipc_service_port.h').write_text('/* Explicit host IPC service label model supplied by the fixture. */\n')
def run(name,text):
 p=w/(name+'.c');p.write_text(text);exe=w/name
 c=subprocess.run(['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-format','-I'+str(w),str(p),'-o',str(exe)],capture_output=True,text=True)
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
mutants += [
 ('wrong-label-gate','if(port->ip_service_port&&port->ip_splabel!=NULL){','if(port->ip_splabel!=NULL){'),
 ('wrong-launchd-name','state->launchd_name=label->ispl_launchd_name','state->launchd_name=0'),
 ('wrong-name-copy',"state->service_name[i]=(c>=0x21&&c<=0x7e)?(char)c:'?';","state->service_name[i]='x';"),
 ('drop-truncation','state->name_truncated=name[i]!=0;','state->name_truncated=0;'),
 ('drop-sanitization','if(c<0x21||c>0x7e)state->name_changed=1;','if(false)state->name_changed=1;'),
 ('oversized-service-line','KEBAB %s IPC-SERVICE:', 'X'*160+'KEBAB %s IPC-SERVICE:'),
 ('drop-service-line','KEBAB %s IPC-SERVICE:', 'KEBAB %s IPC-OMITTED:'),
]
no_info=run('config-no-info',body.replace('#define CONFIG_SERVICE_PORT_INFO 1','#define CONFIG_SERVICE_PORT_INFO 0'))
assert no_info.returncode==0,no_info.stderr;print('CONFIG_SERVICE_PORT_INFO=0',no_info.stdout.strip(),flush=True)
for label,old,new in mutants:
 assert body.count(old)==1,(label,body.count(old))
 result_mut=run(label,body.replace(old,new));assert result_mut.returncode!=0,(label,'incorrect behavior survived')
 print(label,'compiled behavioral mutant rejected',flush=True)
receipt=dict(passed=True,version='0476',cases=int(re.search(r'PASS (\d+)',result.stdout)[1]),compiled_behavioral_mutants=len(mutants),config_service_port_info_0_cases=int(re.search(r'PASS (\d+)',no_info.stdout)[1]),fixture_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),physical_test=False,native_IPC_task_scheduler_reference_model=True)
(w/'test-ipc-service-name-result-0476.json').write_text(json.dumps(receipt,indent=2)+'\n')
