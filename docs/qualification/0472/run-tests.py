from pathlib import Path
import hashlib,json,subprocess,sys
w=Path(sys.argv[1]).resolve();source=(w/'ipc-receiver-production-test.c').read_text()
def run(label,body,success):
 p=w/(label+'.c');p.write_text(body);exe=w/(label+'.bin')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(exe)],check=True)
 t=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
 (w/(label+'.stdout')).write_text(t.stdout);(w/(label+'.stderr')).write_text(t.stderr)
 assert (t.returncode==0)==success,(label,t.stdout[-900:],t.stderr[-2000:])
 if not success:assert 'Assertion' in t.stderr
 print(label,t.stdout.strip() if success else 'compiled behavioral mutant rejected',flush=True)
 return t.stdout.strip() if success else label
summary=run('actual',source,True)
mutants=[('wrong-trap','site->trap!=-47','site->trap!=-48'),('swap-destination','value.remote=(uint32_t)site->ports;','value.remote=(uint32_t)(site->ports>>32);'),('swap-reply','value.receive=(uint32_t)(site->desc_rcv>>32);','value.receive=(uint32_t)site->desc_rcv;'),('drop-vector-bit','value.options=site->options;','value.options=(uint32_t)site->options;'),('wrong-message-id','value.message_id=(int32_t)(uint32_t)(site->voucher_id>>32);','value.message_id=(int32_t)(uint32_t)site->voucher_id;'),('leak-port-lock','object=(uint64_t)(uintptr_t)port;ip_mq_unlock(port);','object=(uint64_t)(uintptr_t)port;'),('lookup-receive-only','(value.options&MACH64_SEND_MSG)&&MACH_PORT_VALID(value.remote)','MACH_PORT_VALID(value.remote)'),('ignore-lookup-failure','if(status==KERN_SUCCESS){','if(true){'),('wrong-owner','receiver=ipc_port_get_receiver_task_locked(port,NULL);','(void)ipc_port_get_receiver_task_locked(port,NULL);receiver=0;')]
for label,old,new in mutants:
 assert source.count(old)==1,(label,source.count(old));run(label,source.replace(old,new),False)
(w/'test-ipc-receiver-result-0472.json').write_text(json.dumps(dict(passed=True,physical_test=False,production_helper_tested=True,compiled_behavioral_mutants=len(mutants),summary=summary,fixture_sha256=hashlib.sha256((w/'ipc-receiver-production-test.c').read_bytes()).hexdigest(),models=['native IPC space and port lookup','native port lock/receiver helper','task/thread lifetime and register snapshot integration'],atomic_request_reply_pairing_proven=False),indent=2)+'\n')
