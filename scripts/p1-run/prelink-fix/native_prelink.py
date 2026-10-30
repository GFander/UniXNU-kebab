#!/usr/bin/env python3
"""Plan ordinary ARM64 prelink pointer rebinding; never changes input images."""
import argparse
import base64
from bisect import bisect_right
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


def macho(data, at=0):
    if at < 0 or at + 32 > len(data):
        raise ValueError('Mach-O header outside image')
    magic, cpu, subtype, kind, count, size = struct.unpack_from('<6I', data, at)
    if (magic, cpu, subtype) != (0xfeedfacf, 0x100000c, 0):
        raise ValueError('Only ordinary little-endian ARM64 is supported')
    end = at + 32 + size
    if end > len(data):
        raise ValueError('Load commands outside image')
    offset = at + 32
    commands, segments = [], []
    for _ in range(count):
        if offset + 8 > end:
            raise ValueError('Truncated load command')
        command, length = struct.unpack_from('<II', data, offset)
        if length < 8 or length % 8 or offset + length > end:
            raise ValueError('Invalid load command extent')
        commands.append((command, offset, length))
        if command == 0x19:
            if length < 72:
                raise ValueError('Truncated segment')
            name, va, size, fileoff, filesize, maxprot, initprot, nsects, flags = struct.unpack_from('<16s4Q4I', data, offset + 8)
            if length != 72 + 80 * nsects or filesize > size or va + size > 1 << 64:
                raise ValueError('Invalid segment extent')
            segments.append({'name': name.rstrip(b'\0').decode(), 'va': va, 'size': size,
                             'fileoff': fileoff, 'filesize': filesize, 'command_offset': offset,
                             'maxprot': maxprot, 'initprot': initprot, 'sections': []})
            for index in range(nsects):
                section_at = offset + 72 + index * 80
                section_name, _, address, extent, section_offset = struct.unpack_from('<16s16sQQI', data, section_at)
                if extent and not va <= address <= address + extent <= va + size:
                    raise ValueError('Section outside segment')
                segments[-1]['sections'].append({'name': section_name.rstrip(b'\0').decode(),
                    'va': address, 'size': extent, 'fileoff': section_offset, 'command_offset': section_at})
        offset += length
    if offset != end:
        raise ValueError('Load command size mismatch')
    return {'kind': kind, 'segments': segments, 'commands': commands}


def backed_offset(segments, address, length, data_length):
    matches = [s['fileoff'] + address - s['va'] for s in segments
               if s['va'] <= address and address + length <= s['va'] + s['filesize']]
    if len(matches) != 1 or matches[0] + length > data_length:
        raise ValueError(f'Ambiguous or unbacked address {address:#x}+{length:#x}')
    return matches[0]


def exports(data, image):
    result = {}
    tables = [(offset, length) for command, offset, length in image['commands'] if command == 2]
    if len(tables) != 1 or tables[0][1] != 24:
        raise ValueError('Expected one symbol table')
    symoff, count, stroff, strsize = struct.unpack_from('<4I', data, tables[0][0] + 8)
    if symoff + count * 16 > len(data) or stroff + strsize > len(data):
        raise ValueError('Symbol table outside image')
    strings = data[stroff:stroff + strsize]
    for index in range(count):
        strx, kind, _, _, address = struct.unpack_from('<IBBHQ', data, symoff + index * 16)
        if kind & 0xe0 or not kind & 1 or not kind & 0x0e:
            continue
        end = strings.find(b'\0', strx)
        if strx >= strsize or end < 0:
            raise ValueError('Unterminated symbol name')
        name = strings[strx:end].decode()
        result.setdefault(address, []).append(name)
    return result


def unserialize_xml(blob):
    refs = {}

    def read(element):
        if 'IDREF' in element.attrib:
            return refs[element.attrib['IDREF']]
        tag = element.tag
        if tag == 'dict':
            children = list(element)
            if len(children) % 2:
                raise ValueError('Odd XML dictionary')
            value = {}
            for index in range(0, len(children), 2):
                key = read(children[index])
                if key in value:
                    raise ValueError('Duplicate XML dictionary key')
                value[key] = read(children[index + 1])
        elif tag == 'array':
            value = [read(child) for child in element]
        elif tag in ('key', 'string'):
            value = element.text or ''
        elif tag == 'integer':
            value = int(element.text or '0', 0)
        elif tag == 'data':
            value = base64.b64decode(''.join((element.text or '').split()), validate=True)
        elif tag in ('true', 'false'):
            value = tag == 'true'
        else:
            raise ValueError(f'Unsupported prelink XML element {tag}')
        if 'ID' in element.attrib:
            identifier = element.attrib['ID']
            if identifier in refs:
                raise ValueError('Duplicate XML object ID')
            refs[identifier] = value
        return value

    return read(ET.fromstring(blob.split(b'\0', 1)[0]))


def unique_kernel_binding(address, original_exports, native_by_name):
    names = original_exports.get(address, [])
    choices = {new for name in names for new in native_by_name.get(name, set())}
    if len(choices) != 1:
        raise ValueError(f'Kernel target {address:#x} has {len(choices)} native address candidates: {names}')
    return next(iter(choices))


def plan(native_data, original_data, roots, slide):
    if slide % 4096:
        raise ValueError('Rebasing must preserve native 4K alignment')
    native, original = macho(native_data), macho(original_data)
    if native['kind'] != 2 or original['kind'] != 2:
        raise ValueError('Expected MH_EXECUTE kernel images')
    parent = original['segments']
    info = next(s for s in parent if s['name'] == '__PRELINK_INFO')
    at = backed_offset(parent, info['va'], info['filesize'], len(original_data))
    metadata = unserialize_xml(original_data[at:at + info['filesize']])
    entries = {}
    for entry in metadata['_PrelinkInfoDictionary']:
        identifier = entry['CFBundleIdentifier']
        if identifier in entries:
            raise ValueError('Duplicate prelink identifier')
        entries[identifier] = entry
    selected, interfaces = set(), set()
    pending = list(roots)
    while pending:
        identifier = pending.pop()
        if identifier in selected or identifier in interfaces:
            continue
        if identifier not in entries:
            raise ValueError(f'Missing declared dependency {identifier}')
        entry = entries[identifier]
        if entry.get('OSKernelResource') is True:
            interfaces.add(identifier)
            continue
        selected.add(identifier)
        pending.extend(entry.get('OSBundleLibraries') or {})
    owned, modules = [], {}
    for identifier, entry in entries.items():
        if entry.get('OSKernelResource') is True:
            continue
        va = entry.get('_PrelinkExecutableLoadAddr')
        if va is None or not entry.get('_PrelinkExecutableSize'):
            continue
        header = backed_offset(parent, va, 32, len(original_data))
        module = macho(original_data, header)
        if module['kind'] != 11:
            raise ValueError('Expected MH_KEXT_BUNDLE component')
        modules[identifier] = module
        for segment in module['segments']:
            if segment['size'] and segment['name'] != '__LINKEDIT':
                owned.append((segment['va'], segment['va'] + segment['size'], identifier, segment))
    codeless = selected - modules.keys()
    for identifier in codeless:
        entry = entries[identifier]
        executable_keys = ('CFBundleExecutable', '_PrelinkExecutableLoadAddr',
            '_PrelinkExecutableSourceAddr', '_PrelinkExecutableSize', '_PrelinkKmodInfo')
        if any(key in entry for key in executable_keys) or not isinstance(entry.get('IOKitPersonalities'), dict) or not entry['IOKitPersonalities']:
            raise ValueError('Codeless component must contain personalities and no executable fields: ' + identifier)
    owned.sort(key=lambda item: item[:3])
    for left, right in zip(owned, owned[1:]):
        if left[1] > right[0]:
            raise ValueError(f'Overlapping component extents: {left[2]} and {right[2]}')
    starts = [item[0] for item in owned]

    def owner(address):
        index = bisect_right(starts, address) - 1
        return owned[index] if index >= 0 and address < owned[index][1] else None

    native_symbols = {}
    for address, names in exports(native_data, native).items():
        for name in names:
            native_symbols.setdefault(name, set()).add(address)
    original_symbols = exports(original_data, original)
    packed = metadata['_PrelinkLinkKASLROffsets']
    count, = struct.unpack_from('<I', packed)
    if len(packed) != 4 + count * 4:
        raise ValueError('Invalid pointer-table extent')
    offsets = struct.unpack_from(f'<{count}I', packed, 4)
    if len(set(offsets)) != count:
        raise ValueError('Duplicate pointer relocation site')
    base = next(s['va'] for s in parent if s['name'] == '__PRELINK_TEXT')
    bindings = []
    for offset in offsets:
        site = base + offset
        source = owner(site)
        if source is None or source[2] not in selected:
            continue
        if source != owner(site + 7):
            raise ValueError('Pointer spans component extent')
        target, = struct.unpack_from('<Q', original_data, backed_offset(parent, site, 8, len(original_data)))
        destination = owner(target)
        if destination:
            if destination[2] not in selected:
                raise ValueError(f'Undeclared pointer dependency {destination[2]} from {source[2]}')
            replacement = target + slide
            kind = 'component'
        else:
            replacement = unique_kernel_binding(target, original_symbols, native_symbols)
            if not any(s['va'] <= replacement < s['va'] + s['size'] for s in native['segments']):
                raise ValueError('Native export is outside mapped kernel segments')
            kind = 'kernel'
        if not 0 <= site + slide < 1 << 64 or not 0 <= replacement < 1 << 64:
            raise ValueError('Rebased pointer outside 64-bit address space')
        bindings.append({'site': site, 'target': target, 'new_site': site + slide,
                         'new_target': replacement, 'kind': kind, 'module': source[2]})
    extents, original_extents = [], []
    for start, end, identifier, segment in owned:
        if identifier not in selected:
            continue
        if not 0 <= start + slide < end + slide <= 1 << 64:
            raise ValueError('Rebased segment outside 64-bit address space')
        for s in native['segments']:
            if s['size'] and start + slide < s['va'] + s['size'] and s['va'] < end + slide:
                raise ValueError(f'Rebased component collides with native {s["name"]}')
        extents.append({'module': identifier, 'segment': segment['name'],
                        'va': start + slide, 'size': end - start})
        original_extents.append({'module': identifier, 'segment': segment['name'],
                                 'va': start, 'size': end - start})
    return {'native_sha256': hashlib.sha256(native_data).hexdigest(),
            'original_sha256': hashlib.sha256(original_data).hexdigest(),
            'slide': slide, 'components': sorted(selected - codeless),
            'codeless_components': sorted(codeless), 'kernel_interfaces': sorted(interfaces),
            'pointer_counts': dict(Counter(binding['kind'] for binding in bindings)),
            'extents': extents, 'original_extents': original_extents, 'bindings': bindings,
            'images_modified': False,
            'instruction_relocation_complete': False, 'abi_qualified': False, 'loadable': False}


def _signed(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def _adrp_target(word, pc):
    if word & 0x9f000000 != 0x90000000:
        raise ValueError('Not an ADRP instruction')
    immediate = ((word >> 5) & 0x7ffff) << 2 | ((word >> 29) & 3)
    return (pc & ~4095) + (_signed(immediate, 21) << 12)


def _encode_adrp(word, pc, target):
    delta = (target & ~4095) - (pc & ~4095)
    if delta % 4096 or not -(1 << 32) <= delta < (1 << 32):
        raise ValueError(f'ADRP target {target:#x} is not page-reachable from {pc:#x}')
    immediate = delta >> 12
    if not -(1 << 20) <= immediate < (1 << 20):
        raise ValueError(f'ADRP target {target:#x} is outside the signed 21-bit range')
    return (word & 0x9f00001f) | (((immediate & 3) << 29) | (((immediate >> 2) & 0x7ffff) << 5))


def _encode_add(word, target):
    offset = target & 0xfff
    return (word & ~(0xfff << 10)) | (offset << 10)


def _encode_ldr(word, target):
    if target % 8:
        raise ValueError(f'LDR target {target:#x} is not 8-byte aligned')
    offset = target & 0xfff
    if offset % 8 or offset > 32760:
        raise ValueError(f'LDR target {target:#x} is outside the unsigned 12-bit scaled range')
    return (word & ~(0xfff << 10)) | ((offset // 8) << 10)


def _bl_target(word, pc):
    if word & 0x7c000000 not in (0x14000000, 0x94000000):
        raise ValueError('Not a B/BL instruction')
    return pc + (_signed(word & 0x3ffffff, 26) << 2)


def _encode_bl(word, pc, target):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError(f'Branch target {target:#x} is outside the signed 26-bit (+/-128 MiB) range from {pc:#x}')
    return (word & 0xfc000000) | ((delta >> 2) & 0x3ffffff)


def _segment_containing(image, address, length=1):
    matches = [s for s in image['segments']
               if s['va'] <= address and address + length <= s['va'] + s['size']]
    if len(matches) != 1:
        raise ValueError(f'Address {address:#x}+{length:#x} is not in exactly one segment')
    return matches[0]


def find_kernel_import_sites(data, image, address):
    """Find every native reference the linker could emit for one symbol.

    A static arm64 kernel has no dyld: a resolved external data symbol appears
    as ADRP+ADD (address materialisation), ADRP+LDR (load through the symbol
    address), or an absolute 8-byte slot (vtable/GOT/literal). A resolved
    external function appears as B/BL or, when its address is taken, as data
    in a vtable/GOT slot. The scanner is deliberately conservative: it only
    reports encodings whose decoded target is exactly ``address``.
    """
    sites = []
    for segment in image['segments']:
        if not segment['filesize']:
            continue
        blob = data[segment['fileoff']:segment['fileoff'] + segment['filesize']]
        if segment['initprot'] & 4:
            offset = 0
            while offset + 4 <= len(blob):
                word, = struct.unpack_from('<I', blob, offset)
                pc = segment['va'] + offset
                if word & 0x9f000000 == 0x90000000 and offset + 8 <= len(blob):
                    page = _adrp_target(word, pc)
                    following, = struct.unpack_from('<I', blob, offset + 4)
                    if (following & 0x7f000000 == 0x11000000
                            and (following >> 22) & 3 == 0
                            and (following >> 5) & 31 == word & 31
                            and (following & 31) == word & 31):
                        if page + ((following >> 10) & 0xfff) == address:
                            sites.append({'kind': 'adrp_add', 'va': pc, 'word0': word,
                                          'word1': following})
                    elif (following & 0xffc00000 == 0xf9400000
                          and (following >> 5) & 31 == word & 31):
                        if page + (((following >> 10) & 0xfff) << 3) == address:
                            sites.append({'kind': 'adrp_ldr', 'va': pc, 'word0': word,
                                          'word1': following})
                if word & 0x7c000000 in (0x14000000, 0x94000000):
                    if _bl_target(word, pc) == address:
                        sites.append({'kind': 'b' if word & 0x80000000 == 0 else 'bl',
                                      'va': pc, 'word': word})
                offset += 4
        elif segment['name'] not in ('__LINKEDIT', '__LINKINFO', '__PRELINK_INFO', '__CTF'):
            offset = (8 - segment['va'] % 8) % 8
            while offset + 8 <= len(blob):
                value, = struct.unpack_from('<Q', blob, offset)
                if value == address:
                    sites.append({'kind': 'slot', 'va': segment['va'] + offset,
                                  'word': value})
                offset += 8
    sites.sort(key=lambda site: (site['va'], site['kind']))
    unique, seen = [], set()
    for site in sites:
        key = (site['va'], site['kind'])
        if key not in seen:
            unique.append(site)
            seen.add(key)
    return unique


def patch_kernel_import_sites(output, image, sites, address):
    patches = []
    for site in sites:
        segment = _segment_containing(image, site['va'], 8 if site['kind'] == 'slot' else 4)
        at = segment['fileoff'] + site['va'] - segment['va']
        if site['kind'] in ('b', 'bl'):
            word = _encode_bl(site['word'], site['va'], address)
            expected = struct.pack('<I', site['word'])
            if output[at:at + 4] != expected:
                raise ValueError(f'Native bytes changed at import site {site["va"]:#x}')
            struct.pack_into('<I', output, at, word)
            patches.append({**site, 'new_word': word})
        elif site['kind'] == 'adrp_add':
            word0 = _encode_adrp(site['word0'], site['va'], address)
            word1 = _encode_add(site['word1'], address)
            expected = struct.pack('<II', site['word0'], site['word1'])
            if output[at:at + 8] != expected:
                raise ValueError(f'Native bytes changed at import site {site["va"]:#x}')
            struct.pack_into('<II', output, at, word0, word1)
            patches.append({**site, 'new_word0': word0, 'new_word1': word1})
        elif site['kind'] == 'adrp_ldr':
            word0 = _encode_adrp(site['word0'], site['va'], address)
            word1 = _encode_ldr(site['word1'], address)
            expected = struct.pack('<II', site['word0'], site['word1'])
            if output[at:at + 8] != expected:
                raise ValueError(f'Native bytes changed at import site {site["va"]:#x}')
            struct.pack_into('<II', output, at, word0, word1)
            patches.append({**site, 'new_word0': word0, 'new_word1': word1})
        elif site['kind'] == 'slot':
            expected = struct.pack('<Q', site['word'])
            if output[at:at + 8] != expected:
                raise ValueError(f'Native bytes changed at import slot {site["va"]:#x}')
            struct.pack_into('<Q', output, at, address)
            patches.append({**site, 'new_word': address})
        else:
            raise ValueError(f'Unsupported import site kind {site["kind"]}')
    return patches


def resolve_kernel_imports(output, native_data, native_image, report, requests):
    """Resolve nominated kernel-side imports to final prelinked VAs.

    ``requests`` entries are ``{'symbol', 'original_va', 'native_va'}``. The
    native address is normally looked up in the kernel symbol table; a caller
    may pass it explicitly for a probe or an author-provided import veneer.
    """
    native_by_name = {}
    for address, names in exports(native_data, native_image).items():
        for name in names:
            native_by_name.setdefault(name, set()).add(address)
    results = []
    for request in requests:
        symbol, original = request['symbol'], request['original_va']
        native_va = request.get('native_va')
        if native_va is None:
            candidates = native_by_name.get(symbol)
            if not candidates or len(candidates) != 1:
                raise ValueError(f'Kernel import {symbol} has '
                                 f'{0 if not candidates else len(candidates)} native definitions')
            native_va = next(iter(candidates))
        if not 0 <= original < 1 << 64 or not 0 <= native_va < 1 << 64:
            raise ValueError(f'Import {symbol} address outside 64-bit space')
        final = (original + report['slide']) & ((1 << 64) - 1)
        source = [e for e in report['original_extents']
                  if e['va'] <= original < e['va'] + e['size']]
        destination = [e for e in report['extents']
                       if e['va'] <= final < e['va'] + e['size']]
        if len(source) != 1:
            raise ValueError(f'Import {symbol} original address {original:#x} is outside the selected closure')
        if len(destination) != 1:
            raise ValueError(f'Import {symbol} final address {final:#x} is outside the emitted closure')
        if source[0]['module'] != destination[0]['module']:
            raise ValueError(f'Import {symbol} changed component across the slide')
        if symbol in native_by_name and native_va not in native_by_name[symbol]:
            raise ValueError(f'Import {symbol} native address {native_va:#x} is not its symbol value')
        sites = find_kernel_import_sites(native_data, native_image, native_va)
        if not sites:
            raise ValueError(f'No instruction or slot reference to kernel import {symbol} '
                             f'at {native_va:#x}; refusing to emit a candidate whose import '
                             'could not be bound')
        patches = patch_kernel_import_sites(output, native_image, sites, final)
        results.append({'symbol': symbol, 'original_va': original, 'native_va': native_va,
                        'final_va': final, 'module': source[0]['module'],
                        'segment': source[0]['segment'], 'sites': patches,
                        'site_count': len(patches)})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', required=True, type=Path)
    parser.add_argument('--prelinked', required=True, type=Path)
    parser.add_argument('--root', required=True, action='append')
    parser.add_argument('--slide', required=True, type=lambda value: int(value, 0))
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    report = plan(args.kernel.read_bytes(), args.prelinked.read_bytes(), args.root, args.slide)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('components', 'kernel_interfaces', 'pointer_counts', 'loadable')}, indent=2))


if __name__ == '__main__':
    main()
