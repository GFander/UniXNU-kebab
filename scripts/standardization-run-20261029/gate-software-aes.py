from pathlib import Path
r=Path(__file__).parent;d=r/'aes-gate';p=Path('iokit/Kernel/arm/NativeAES.cpp')
s=(r/'static-upl/after'/p).read_text()
for f in ['before','after']:
    dest=d/f/p;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(s,newline='\n')
needle='bool NativeAES::start(IOService *provider)\n{'
assert needle in s
s=s.replace(needle,needle+'''
  // CBC qualification does not provide Apple's UID/GID hardware keys.
  // Keep the incomplete nonce-manager integration explicitly opt-in.
  uint32_t enabled = 0;
  if (!PE_parse_boot_argn("kebab_software_aes", &enabled, sizeof(enabled)) || enabled != 1) return false;''',1)
needle='    __atomic_fetch_add(&native_aes_unsupported_key, 1, __ATOMIC_RELAXED);'
assert needle in s
s=s.replace(needle,'''    IOLog("KEBAB AES: rejected selector=%u bits=%u operation=%u bytes=%llu\\n",
          key->selector, key->bits, static_cast<unsigned>(operation), bytes);
'''+needle,1)
(d/'after'/p).write_text(s,newline='\n')
print('Prepared explicit experimental AES gate; selectors remain unsupported')
