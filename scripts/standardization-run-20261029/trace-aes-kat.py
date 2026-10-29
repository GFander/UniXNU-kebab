from pathlib import Path
r=Path(__file__).parent;d=r/'aes-kat-trace';p=Path('iokit/Kernel/arm/NativeAES.cpp')
s=(r/'software-aes/after'/p).read_text()
for name in ['before','after']:
    f=d/name/p;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(s,newline='\n')
s=s.replace('  key.selector = 2001;', '  IOLog("KEBAB AES KAT: descriptors=%u completed=%u\\n", interface, native_aes_completed);\n  key.selector = 2001;',1)
s=s.replace('  key.selector = 0;\n  interface = interface &&', '  IOLog("KEBAB AES KAT: unsupported=%u count=%u\\n", interface, completion.count);\n  key.selector = 0;\n  interface = interface &&',1)
old='''  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  interface = interface && !provider->performAES(const_cast<unsigned char *>(input), output, kernel_task,
    64, &publicIV, IOAESEncrypt, &key, nullptr, nullptr) && !memcmp(output, expected, 64);
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  interface = interface && !provider->performAES(output, output, kernel_task, 64, &publicIV,
    IOAESDecrypt, &key, nullptr, nullptr) && !memcmp(output, input, 64) && !memcmp(publicIV.bytes, expected + 48, 16);'''
new='''  IOLog("KEBAB AES KAT: bounds=%u count=%u\\n", interface, completion.count);
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  IOReturn rawStatus = kIOReturnNotReady;
  if (interface) rawStatus = provider->performAES(const_cast<unsigned char *>(input), output, kernel_task,
    64, &publicIV, IOAESEncrypt, &key, nullptr, nullptr);
  interface = interface && !rawStatus && !memcmp(output, expected, 64);
  IOLog("KEBAB AES KAT: raw-encrypt=%u status=0x%x first=%02x%02x%02x%02x iv=%02x completed=%u\\n",
        interface, rawStatus, output[0], output[1], output[2], output[3], publicIV.bytes[0], native_aes_completed);
  for (unsigned i = 0; i < 16; ++i) publicIV.bytes[i] = static_cast<unsigned char>(i);
  rawStatus = kIOReturnNotReady;
  if (interface) rawStatus = provider->performAES(output, output, kernel_task, 64, &publicIV,
    IOAESDecrypt, &key, nullptr, nullptr);
  interface = interface && !rawStatus && !memcmp(output, input, 64) && !memcmp(publicIV.bytes, expected + 48, 16);
  IOLog("KEBAB AES KAT: raw-decrypt=%u status=0x%x first=%02x%02x%02x%02x iv=%02x completed=%u\\n",
        interface, rawStatus, output[0], output[1], output[2], output[3], publicIV.bytes[0], native_aes_completed);'''
assert old in s;s=s.replace(old,new,1)
(d/'after'/p).write_text(s,newline='\n')
print('Prepared stage-only KAT diagnostics')
