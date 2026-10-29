from pathlib import Path
r=Path(__file__).parent
s=(r/'before/make-kebab-adt.py').read_text()
anchor="    tree = node('device-tree', ["
assert s.count(anchor)==1
# Keep the formal generator's current security/nonce/owner code intact.
addition='''    # Board-native normal-service resource contracts. These root entries use
    # absolute 64-bit little-endian PA/size pairs (not arm-io-relative cells).
    # Backend validation precedes publishing scoped child nubs.
    normal_buses = [
        node('kebab-spmi', [
            ('compatible', b'kebab,sm8250-spmi\\0qcom,spmi-pmic-arb\\0'),
            ('reg', struct.pack('<8Q', 0x0c440000, 0x1100, 0x0e600000, 0x100000,
                                0x0c40a000, 0x26000, 0x0c600000, 0x2000000)),
            ('qcom,ee', struct.pack('<I', 0)),
            ('kebab,pon', struct.pack('<2I', 0, 8)),
            ('kebab,pmic-keys', struct.pack('<4I', 0, 8, 0xc5, 0xc6)),
            ('kebab,rpmh-rsc', struct.pack('<2Q', 0x18220000, 0x10000)),
            ('kebab,rpmh-cmd-db', struct.pack('<2Q', 0x80860000, 0x20000))]),
        node('kebab-geni13', [
            ('compatible', b'kebab,sm8250-geni13\\0qcom,i2c-geni\\0'),
            ('reg', struct.pack('<6Q', 0xa94000, 0x4000, 0xac0000, 0x2000, 0xf500000, 0x40000)),
            ('kebab,touch-address', struct.pack('<I', 0x4b))]),
    ]

'''
s=s.replace(anchor,addition+anchor)
anchor='    ])\n\n    args.output.parent.mkdir'
assert s.count(anchor)==1
s=s.replace(anchor,'    ] + normal_buses)\n\n    args.output.parent.mkdir')
(r/'after/make-kebab-adt.py').write_text(s,newline='\n')
