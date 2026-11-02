from pathlib import Path
import hashlib,json,re,subprocess,sys
w=Path(sys.argv[1]);source=w/'fairplay-resources-production-test.c';body=source.read_text()
def run(name,text):
 p=w/(name+'.c');p.write_text(text);exe=w/name
 compile=subprocess.run(['clang','-std=gnu11','-g','-O1','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread',str(p),'-o',str(exe)],capture_output=True,text=True)
 assert compile.returncode==0,(name,compile.stderr)
 return subprocess.run([str(exe)],capture_output=True,text=True)
result=run('actual',body);assert result.returncode==0,result.stderr;print(result.stdout.strip(),flush=True)
mutants=[
 ('wrong-identity','memcmp(uuid,expected,sizeof(uuid))==0','memcmp(uuid,expected,sizeof(uuid))!=0'),
 ('ignore-uid','proc_getuid(p)!=501','proc_getuid(p)!=0'),
 ('trace-other-process','p!=current_proc()||','false||'),
 ('wrong-errno','.error=error,','.error=0,'),
 ('leak-argument','.request=code==SYS_ptrace?request:-1','.request=request'),
 ('wrong-protection','(p->p_lflag&P_LNOATTACH)?2u:0u','(p->p_lflag&P_LNOATTACH)?0u:0u'),
 ('wrong-debugger','PE_i_can_has_debugger(NULL)!=0','PE_i_can_has_debugger(NULL)==0'),
 ('wrong-ring-slot','(event.sequence-1)%KEBAB_FP_CALLS','event.sequence%KEBAB_FP_CALLS'),
 ('replace-first-deny','request==31&&kebab_fp_first_deny.sequence==0)','request==31&&true)'),
 ('ignore-selected-pid','kebab_fp_pid!=pid||kebab_fp_sealed||kebab_fp_total','false||kebab_fp_sealed||kebab_fp_total'),
 ('ignore-seal','kebab_fp_pid!=pid||kebab_fp_sealed||','kebab_fp_pid!=pid||false||'),
 ('wrong-ring-order','(first+i-1)%KEBAB_FP_CALLS','(first+i)%KEBAB_FP_CALLS'),
 ('drop-state-seal','kebab_fp_sealed=true;','kebab_fp_sealed=false;'),
 ('wrong-exit-status','WEXITSTATUS(rv)','0'),
 ('drop-saturation','||kebab_fp_total==~(uint64_t)0',''),
 ('oversized-output','KEBAB FP-%s:', 'X'*160+'KEBAB FP-%s:'),
]
mutants += [
 ('resource-no-budget','kebab_fp_resource_count<32','kebab_fp_resource_count<320'),
 ('resource-ignore-seal','kebab_fp_pid==pid&&!kebab_fp_sealed&&','kebab_fp_pid==pid&&true&&'),
 ('resource-ignore-owner','kebab_fp_pid==pid&&!','true&&!'),
 ('resource-open-success','error!=ENOENT||','error==ENOENT||'),
 ('resource-partial-leak','else name[0]=0;','else {name[15]=0;length=15;}'),
 ('resource-drop-sanitize',"name[i]='?';changed=1;","name[i]='?';changed=0;"),
 ('resource-wrong-dirfd','code==SYS_openat?dirfd:0','0'),
 ('resource-wrong-found','result,found!=0,snapshot','result,0,snapshot'),
 ('resource-missing-trunc','length==bound||','false||'),
]
for name,old,new in mutants:
 assert body.count(old)==1,(name,body.count(old))
 mutation=run(name,body.replace(old,new));assert mutation.returncode!=0,(name,'incorrect behavior survived')
 print(name,'compiled behavioral mutant rejected',flush=True)
receipt=dict(passed=True,version='0478',cases=int(re.search(r'PASS (\d+)',result.stdout)[1]),compiled_behavioral_mutants=len(mutants),fixture_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),kernel_proc_UUID_scheduler_lifecycle_locks_modeled=True,physical_test=False)
(w/'test-fairplay-resources-result-0478.json').write_text(json.dumps(receipt,indent=2)+'\n')
