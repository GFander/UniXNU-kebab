from pathlib import Path
r=Path(__file__).parent
p=Path('iokit/Kernel/arm/KebabPlatformExpert.cpp');s=(r/'before'/p).read_text()
old='return "(\'chosen\', \'defaults\', \'cpus\', \'options\')";'
assert s.count(old)==1
s=s.replace(old,'''/* The donor AppleImage4 personality matches the software defaults nub.
\t * It finalizes nonce-manager handoff in start(); filtering this node left
\t * the real ignition_blob handler waiting forever. */
\treturn "('chosen', 'cpus', 'options')";''')
(r/'after'/p).write_text(s,newline='\n')
p=Path('iokit/Kernel/arm/KebabDriverManifest.h');s=(r/'before'/p).read_text()
anchor=' X("platform", "KebabPlatformExpert", "ADT platform/resources") \\\n'
assert s.count(anchor)==1
s=s.replace(anchor,anchor+''' X("image4", "AppleImage4", "original defaults-nub service; startup qualification separate") \\
 X("aes", "IOAESAccelerator", "original crypto-family identity; backend qualification separate") \\
''')
(r/'after'/p).write_text(s,newline='\n')
