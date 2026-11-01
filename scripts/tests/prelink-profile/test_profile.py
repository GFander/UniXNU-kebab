"""Production profile/inventory tests on synthetic bytes and modeled mappings.

No donor code or firmware. Real-input full emission/legacy identity is separate.
"""
from pathlib import Path
import copy,hashlib,json,struct,sys,unittest
_here=Path(__file__).resolve().parent
_production=_here/'candidate' if (_here/'candidate').is_dir() else _here.parents[1]/'p1-run/prelink-fix'
sys.path.insert(0,str(_production))
from prelink_profile import RelocationProfile
from native_prelink import backed_offset,unique_kernel_binding
from emit_prelink import instruction_inventory

def fixture():
    raw=bytearray(1024)
    struct.pack_into('<4I',raw,8,0x300,0,0x300,0)
    struct.pack_into('<II',raw,0x100,0xd61f0100,0x14010000)
    parent=[dict(name='__TEXT_EXEC',va=0x1000,size=256,fileoff=0x100,filesize=256)]
    module=dict(segments=copy.deepcopy(parent),commands=[(2,0,24)])
    doc=dict(schema_version=1,module='test.Driver',donor_sha256=hashlib.sha256(raw).hexdigest(),original_text_sha256=hashlib.sha256(raw[0x100:0x200]).hexdigest(),original_text_va=0x1000,original_text_bytes=256,
             symbol_relative_bindings=[dict(site=0x1100,target=0x2003,original_symbol_va=0x2000,original_symbol_names=['_test_function'],addend=3)],
             preserved_post_transfer_words=[dict(site=0x1004,word_sha256=hashlib.sha256(raw[0x104:0x108]).hexdigest(),previous_word_sha256=hashlib.sha256(raw[0x100:0x104]).hexdigest())],
             control_flow_or_register_dataflow_qualified=False,runtime_qualified=False,no_executable_bytes_may_change=True)
    return raw,parent,module,doc

def pin(raw,doc):
    doc['donor_sha256']=hashlib.sha256(raw).hexdigest();doc['original_text_sha256']=hashlib.sha256(raw[0x100:0x200]).hexdigest()

class ProfileTests(unittest.TestCase):
    def setUp(self):self.raw,self.parent,self.module,self.doc=fixture()
    def checked(self):
        p=RelocationProfile(self.doc,self.raw);p.verify_module(self.raw,self.parent,self.module,backed_offset);return p
    def resolve(self,p,**options):
        args=dict(site=0x1100,target=0x2003,identifier='test.Driver',original_symbols={0x2000:['_test_function']},native_symbols={'_test_function':{0x3000}},resolver=unique_kernel_binding);args.update(options);return p.resolve_binding(**args)
    def test_exact_binding_and_word(self):
        p=self.checked();self.assertEqual(self.resolve(p),0x3003);p.preserve_word('test.Driver',0x1004,0x14010000,0xd61f0100);p.require_bindings_consumed();p.require_words_consumed();self.assertFalse(p.summary()['runtime_qualified']);self.assertFalse(p.summary()['control_flow_or_register_dataflow_qualified'])
    def test_strict_default_rejects(self):
        with self.assertRaisesRegex(ValueError,'External PC-relative'):instruction_inventory(self.raw,self.parent,{'test.Driver':self.module})
    def test_explicit_preservation(self):
        q=instruction_inventory(self.raw,self.parent,{'test.Driver':self.module},self.doc);self.assertEqual(len(q['preserved_post_transfer_words']),1);self.assertFalse(q['register_dataflow_qualified'])
    def test_envelope_rejections(self):
        for key,value in [('schema_version',True),('schema_version',2),('module','bad/module'),('donor_sha256','0'*64),('original_text_sha256','bad'),('runtime_qualified',True),('no_executable_bytes_may_change',False),('control_flow_or_register_dataflow_qualified',True),('original_text_va',True),('original_text_bytes',True),('original_text_bytes',1025)]:
            with self.subTest(key=key,value=value):
                doc=copy.deepcopy(self.doc);doc[key]=value
                with self.assertRaises(ValueError):RelocationProfile(doc,self.raw)
        doc=copy.deepcopy(self.doc);doc['unknown']=1
        with self.assertRaises(ValueError):RelocationProfile(doc,self.raw)
    def test_empty_duplicate_and_invalid_entries(self):
        for field in ('symbol_relative_bindings','preserved_post_transfer_words'):
            for how in ('empty','duplicate','unknown','bad-site'):
                with self.subTest(field=field,how=how):
                    doc=copy.deepcopy(self.doc)
                    if how=='empty':doc[field]=[]
                    if how=='duplicate':doc[field]*=2
                    if how=='unknown':doc[field][0]['extra']=1
                    if how=='bad-site':doc[field][0]['site']=True
                    with self.assertRaises(ValueError):RelocationProfile(doc,self.raw)
    def test_bad_addends(self):
        for value in (0,8,-1,True):
            doc=copy.deepcopy(self.doc);doc['symbol_relative_bindings'][0]['addend']=value;doc['symbol_relative_bindings'][0]['target']=0x2000+value
            with self.assertRaises(ValueError):RelocationProfile(doc,self.raw)
    def test_alias_rejections(self):
        for value in ([],['_test_function','_test_function'],[{}]):
            doc=copy.deepcopy(self.doc);doc['symbol_relative_bindings'][0]['original_symbol_names']=value
            with self.assertRaises(ValueError):RelocationProfile(doc,self.raw)
        with self.assertRaisesRegex(ValueError,'aliases changed'):self.resolve(self.checked(),original_symbols={0x2000:['_other']})
        with self.assertRaisesRegex(ValueError,'aliases changed'):self.resolve(self.checked(),original_symbols={0x2000:['_test_function','_other']},native_symbols={'_test_function':{0x3000},'_other':{0x3000}})
    def test_checked_module_required(self):
        with self.assertRaises(ValueError):self.resolve(RelocationProfile(self.doc,self.raw))
        with self.assertRaises(ValueError):self.resolve(self.checked(),identifier='other.Driver')
        with self.assertRaises(ValueError):instruction_inventory(self.raw,self.parent,{'other.Driver':self.module},self.doc)
    def test_changed_target_and_exact_symbol(self):
        with self.assertRaises(ValueError):self.resolve(self.checked(),target=0x2002)
        with self.assertRaises(ValueError):self.resolve(self.checked(),original_symbols={0x2000:['_test_function'],0x2003:['_exact']})
    def test_native_binding_missing_or_ambiguous(self):
        for native in ({},{'_test_function':{0x3000,0x4000}}):
            with self.assertRaises(ValueError):self.resolve(self.checked(),native_symbols=native)
    def test_native_overflow(self):
        with self.assertRaises(ValueError):self.resolve(self.checked(),native_symbols={'_test_function':{(1<<64)-2}})
    def test_donor_text_and_extent_pins(self):
        for what in ('text','extent','word-outside','unaligned'):
            doc=copy.deepcopy(self.doc)
            if what=='text':doc['original_text_sha256']='0'*64
            if what=='extent':doc['original_text_va']+=4
            if what=='word-outside':doc['preserved_post_transfer_words'][0]['site']=0x1000
            if what=='unaligned':doc['preserved_post_transfer_words'][0]['site']=0x1005
            with self.assertRaises(ValueError):
                p=RelocationProfile(doc,self.raw);p.verify_module(self.raw,self.parent,self.module,backed_offset)
    def test_word_and_previous_digests(self):
        for field in ('word_sha256','previous_word_sha256'):
            doc=copy.deepcopy(self.doc);doc['preserved_post_transfer_words'][0][field]='0'*64
            with self.assertRaises(ValueError):instruction_inventory(self.raw,self.parent,{'test.Driver':self.module},doc)
    def test_fallthrough_previous_rejected(self):
        for previous in (0xd503201f,0x94000000,0xd65f03c0):
            raw=bytearray(self.raw);doc=copy.deepcopy(self.doc);struct.pack_into('<I',raw,0x100,previous);pin(raw,doc);doc['preserved_post_transfer_words'][0]['previous_word_sha256']=hashlib.sha256(raw[0x100:0x104]).hexdigest()
            with self.assertRaisesRegex(ValueError,'unconditional'):instruction_inventory(raw,self.parent,{'test.Driver':self.module},doc)
    def test_unconditional_B_previous_allowed(self):
        raw=bytearray(self.raw);doc=copy.deepcopy(self.doc);struct.pack_into('<I',raw,0x100,0x14000008);pin(raw,doc);doc['preserved_post_transfer_words'][0]['previous_word_sha256']=hashlib.sha256(raw[0x100:0x104]).hexdigest();self.assertEqual(len(instruction_inventory(raw,self.parent,{'test.Driver':self.module},doc)['preserved_post_transfer_words']),1)
    def test_exact_direct_entry_rejected(self):
        raw=bytearray(self.raw);doc=copy.deepcopy(self.doc);struct.pack_into('<I',raw,0x120,0x14000000|(((0x1004-0x1020)//4)&0x3ffffff));pin(raw,doc)
        with self.assertRaisesRegex(ValueError,'entry targets'):instruction_inventory(raw,self.parent,{'test.Driver':self.module},doc)
    def test_pointer_entry_rejected(self):
        with self.assertRaises(ValueError):self.checked().verify_pointer_entries([dict(target=0x1004)])
    def test_symbol_entry_rejected(self):
        raw=bytearray(self.raw);doc=copy.deepcopy(self.doc);struct.pack_into('<4I',raw,8,0x300,1,0x310,0);struct.pack_into('<IBBHQ',raw,0x300,0,0xf,1,0,0x1004);pin(raw,doc)
        with self.assertRaisesRegex(ValueError,'symbol entry'):p=RelocationProfile(doc,raw);p.verify_module(raw,self.parent,self.module,backed_offset)
    def test_internal_target_and_nonencoding_rejected(self):
        for word in (0x14000000,0xd503201f):
            raw=bytearray(self.raw);doc=copy.deepcopy(self.doc);struct.pack_into('<I',raw,0x104,word);pin(raw,doc);doc['preserved_post_transfer_words'][0]['word_sha256']=hashlib.sha256(raw[0x104:0x108]).hexdigest()
            with self.assertRaises(ValueError):instruction_inventory(raw,self.parent,{'test.Driver':self.module},doc)
    def test_unused_and_repeated_consumption(self):
        p=self.checked()
        with self.assertRaises(ValueError):p.require_bindings_consumed()
        with self.assertRaises(ValueError):p.require_words_consumed()
        self.resolve(p)
        with self.assertRaises(ValueError):self.resolve(p)
        p.preserve_word('test.Driver',0x1004,0x14010000,0xd61f0100)
        with self.assertRaises(ValueError):p.preserve_word('test.Driver',0x1004,0x14010000,0xd61f0100)

if __name__=='__main__':unittest.main(verbosity=2)
