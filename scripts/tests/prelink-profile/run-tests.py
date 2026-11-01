from pathlib import Path
import hashlib,json,re,shutil,subprocess,sys,tempfile
w=Path(__file__).resolve().parent;names=('native_prelink.py','emit_prelink.py','prelink_profile.py');inputs={n:(w.parents[1]/'p1-run/prelink-fix'/n).read_text() for n in names}
for n,s in inputs.items():compile(s,n,'exec')
test=w/'test_profile.py'
result=subprocess.run([sys.executable,str(test)],capture_output=True,text=True);assert result.returncode==0,result.stdout+result.stderr
assert 'Ran 20 tests' in result.stderr;print('PASS20 production profile/inventory test groups; synthetic bytes/mappings, no driver behavior model',flush=True)
mutants=[
 ('drop-donor-pin','prelink_profile.py',"hashlib.sha256(original_data).hexdigest() != document['donor_sha256']",'False'),
 ('allow-runtime-claim','prelink_profile.py',"document['runtime_qualified'] is not False",'False'),
 ('allow-zero-addend','prelink_profile.py','not 0 < addend < 8','not 0 <= addend < 8'),
 ('drop-alias-pin','prelink_profile.py',"set(original_symbols.get(base, ())) != set(item['original_symbol_names'])",'False'),
 ('ignore-exact-symbol','prelink_profile.py'," or original_symbols.get(target)",''),
 ('drop-word-digest','prelink_profile.py',"hashlib.sha256(struct.pack('<I', word)).hexdigest() != item['word_sha256']",'False'),
 ('allow-fallthrough','prelink_profile.py','previous & 0xfffffc1f != 0xd61f0000 and previous & 0xfc000000 != 0x14000000','False'),
 ('ignore-unused-bindings','prelink_profile.py','self.used_bindings != self.bindings.keys()','False'),
 ('ignore-unused-words','prelink_profile.py','self.used_words != self.words.keys()','False'),
 ('ignore-pointer-entry','prelink_profile.py',"any(item['target'] in self.words for item in bindings)",'False'),
 ('disable-strict-default','emit_prelink.py','if not internal:','if False:'),
 ('ignore-static-entry','emit_prelink.py','profile and not page and target in profile.words','False'),
]
for name,file,old,new in mutants:
    assert inputs[file].count(old)==1,(name,inputs[file].count(old))
    altered=inputs[file].replace(old,new);compile(altered,file,'exec')
    with tempfile.TemporaryDirectory(prefix='profile-mutant-',dir=w) as directory:
        temp=Path(directory).resolve();assert temp.parent==w.resolve(),'Cleanup must stay in exact test workspace'
        (temp/'candidate').mkdir();shutil.copyfile(test,temp/test.name)
        for n,s in inputs.items():(temp/'candidate'/n).write_text(altered if n==file else s,newline='\n')
        result=subprocess.run([sys.executable,str(temp/test.name)],capture_output=True,text=True)
        assert result.returncode!=0 and 'FAILED (' in result.stderr,(name,'incorrect behavior survived or did not execute tests',result.stderr)
    print(name,'syntax-valid Python behavioral mutant rejected',flush=True)
receipt=dict(passed=True,groups=20,python_behavioral_mutants=len(mutants),syntax_checked=True,synthetic_bytes_and_mappings=True,native_or_device_test=False,source_sha256={n:hashlib.sha256((w.parents[1]/'p1-run/prelink-fix'/n).read_bytes()).hexdigest() for n in names},test_sha256=hashlib.sha256(test.read_bytes()).hexdigest())
(w/'profile-test-result-0480.json').write_text(json.dumps(receipt,indent=2)+'\n',newline='\n')
