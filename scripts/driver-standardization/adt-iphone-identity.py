#!/usr/bin/env python3
"""Apply typed D22AP software identity to an existing runtime ADT only."""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,json,struct

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    loaded=importlib.util.module_from_spec(spec);spec.loader.exec_module(loaded)
    return loaded

codec=module('identity_adt_codec',HERE/'adt-codec.py')
generator=module('identity_adt_generator',ROOT/'scripts/make-kebab-adt.py')

def child(parent,name,create=False):
    found=[n for n in parent[1] if dict(n[0]).get('name')==name.encode()+b'\0']
    if len(found)>1:raise ValueError('Duplicate ADT node '+name)
    if found:return found[0]
    if not create:raise ValueError('Missing ADT node '+name)
    result=([('name',name.encode()+b'\0')],[]);parent[1].append(result);return result

def set_values(node,values):
    present=dict(node[0]);present.update(values);node[0][:]=list(present.items())

def update(data,profile):
    before,end=codec.parse(data)
    if end!=len(data) or codec.encode(before)!=data:raise ValueError('Noncanonical runtime ADT')
    properties,encoded_children=generator.load_iphone_product_node(profile)
    tree=copy.deepcopy(before);set_values(tree,{'model':b'iPhone10,3\0','target-type':b'D22\0'})
    product=child(tree,'product',True);set_values(product,dict(properties))
    for raw in encoded_children:
        node,end=codec.parse(raw)
        if end!=len(raw):raise ValueError('Invalid encoded product child')
        name=dict(node[0])['name'].rstrip(b'\0').decode('ascii');old=child(product,name,True)
        old[0][:]=node[0];old[1][:]=node[1]
    buttons=child(tree,'buttons',True)
    defaults={'compatible':b'buttons\0','device_type':b'buttons\0'}
    set_values(buttons,{k:v for k,v in defaults.items()if k not in dict(buttons[0])})
    set_values(buttons,{'button-names':b'hold\0voldown\0volup\0','home-button-type':struct.pack('<I',2)})
    # No memory, CPU, peripheral, security, UUID, namespace, display or other
    # chosen property may change through an identity-only transformation.
    def unrelated(node):
        result=copy.deepcopy(node);result[0][:]=[(k,v)for k,v in result[0]if k not in ('model','target-type')]
        result[1][:]=[n for n in result[1]if dict(n[0]).get('name')not in(b'product\0',b'buttons\0')]
        return result
    if codec.encode(unrelated(tree))!=codec.encode(unrelated(before)):raise ValueError('Unrelated ADT changed')
    if dict(tree[0]).get('compatible')!=dict(before[0]).get('compatible'):raise ValueError('Board compatibility changed')
    result=codec.encode(tree);after,used=codec.parse(result)
    if used!=len(result)or codec.encode(after)!=result:raise ValueError('ADT roundtrip failed')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iphone-identity',action='store_true',required=True)
    parser.add_argument('--profile',type=Path,default=ROOT/'configs/d22ap-product.json')
    parser.add_argument('source',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args()
    if args.source.resolve()==args.destination.resolve()or args.destination.exists():parser.error('Use a new destination, preserving the original ADT')
    try:data=args.source.read_bytes();result=update(data,args.profile)
    except (OSError,ValueError,KeyError,TypeError,AssertionError,struct.error,SystemExit)as error:parser.error(str(error))
    args.destination.parent.mkdir(parents=True,exist_ok=True);args.destination.write_bytes(result)
    args.destination.with_suffix(args.destination.suffix+'.identity.json').write_text(json.dumps(dict(passed=True,source_sha256=hashlib.sha256(data).hexdigest(),result_sha256=hashlib.sha256(result).hexdigest(),profile_sha256=hashlib.sha256(args.profile.read_bytes()).hexdigest(),only_identity_nodes_changed=True,board_compatible_preserved=True,all_other_nodes_including_chosen_identical=True,owner_manifest_refresh_required=True,runtime_queries_not_yet_verified=True),indent=2)+'\n')
    print('Applied typed iPhone X identity only; owner measurement refresh and runtime verification still required')

if __name__=='__main__':main()
