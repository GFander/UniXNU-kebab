"""Opt-in, donor-pinned relocation metadata. No image or code is changed here.

Hash-pinned words after unconditional transfers are preserved as unclassified
words, never declared unreachable. Indirect control/data flow is unqualified.
"""
import hashlib
import json
import re
import struct


class RelocationProfile:
    def __init__(self, document, original_data):
        fields = {'schema_version', 'module', 'donor_sha256',
                  'original_text_sha256', 'original_text_va', 'original_text_bytes',
                  'symbol_relative_bindings', 'preserved_post_transfer_words',
                  'control_flow_or_register_dataflow_qualified',
                  'runtime_qualified', 'no_executable_bytes_may_change'}
        if not isinstance(document, dict) or set(document) != fields:
            raise ValueError('Invalid relocation profile fields')
        if document['schema_version'] != 1 or type(document['schema_version']) is not int:
            raise ValueError('Unsupported relocation profile version')
        if (document['control_flow_or_register_dataflow_qualified'] is not False or
                document['runtime_qualified'] is not False or
                document['no_executable_bytes_may_change'] is not True):
            raise ValueError('Profile must preserve code and make no runtime/control-flow claim')
        self.module = document['module']
        if not isinstance(self.module, str) or not re.fullmatch(r'[A-Za-z0-9_.-]{1,255}', self.module):
            raise ValueError('Invalid profile module')
        for name in ('donor_sha256', 'original_text_sha256'):
            if not isinstance(document[name], str) or not re.fullmatch(r'[0-9a-f]{64}', document[name]):
                raise ValueError('Invalid profile digest')
        if (not self._address(document['original_text_va']) or
                type(document['original_text_bytes']) is not int or
                not 0 < document['original_text_bytes'] <= len(original_data) or
                document['original_text_bytes'] % 4 or
                document['original_text_va'] + document['original_text_bytes'] > 1 << 64):
            raise ValueError('Invalid profile executable extent')
        if hashlib.sha256(original_data).hexdigest() != document['donor_sha256']:
            raise ValueError('Profile donor digest mismatch')
        self.document = document
        self.digest = hashlib.sha256(json.dumps(document, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.bindings = self._index(document['symbol_relative_bindings'],
                                    {'site', 'target', 'original_symbol_va', 'original_symbol_names', 'addend'})
        self.words = self._index(document['preserved_post_transfer_words'],
                                 {'site', 'word_sha256', 'previous_word_sha256'})
        self.used_bindings, self.used_words = set(), set()
        self.module_checked = False
        for item in self.bindings.values():
            base, target, addend = item['original_symbol_va'], item['target'], item['addend']
            if (not self._address(base) or not self._address(target) or
                    type(addend) is not int or not 0 < addend < 8 or base + addend != target):
                raise ValueError('Invalid explicit symbol-relative addend')
            names = item['original_symbol_names']
            if (not isinstance(names, list) or not names or
                    not all(isinstance(n, str) and 0 < len(n) <= 512 and n.isascii() for n in names) or
                    len(set(names)) != len(names)):
                raise ValueError('Invalid original symbol aliases')
        for item in self.words.values():
            if item['site'] % 4 or any(not isinstance(item[k], str) or
                    not re.fullmatch(r'[0-9a-f]{64}', item[k])
                    for k in ('word_sha256', 'previous_word_sha256')):
                raise ValueError('Invalid preserved word metadata')

    @staticmethod
    def _address(value):
        return type(value) is int and 0 <= value < 1 << 64

    @classmethod
    def _index(cls, items, fields):
        if not isinstance(items, list) or not items or len(items) > 4096:
            raise ValueError('Profile entries must be nonempty and bounded')
        result = {}
        for item in items:
            if not isinstance(item, dict) or set(item) != fields or not cls._address(item['site']):
                raise ValueError('Invalid profile entry')
            if item['site'] in result:
                raise ValueError('Duplicate profile site')
            result[item['site']] = item
        return result

    def verify_module(self, original_data, parent, module, backed_offset):
        text = [s for s in module['segments'] if s['name'] == '__TEXT_EXEC' and s['filesize']]
        if len(text) != 1:
            raise ValueError('Profile requires one executable segment')
        text = text[0]
        if (text['va'], text['filesize']) != (self.document['original_text_va'], self.document['original_text_bytes']):
            raise ValueError('Profile executable extent mismatch')
        at = backed_offset(parent, text['va'], text['filesize'], len(original_data))
        if hashlib.sha256(original_data[at:at + text['filesize']]).hexdigest() != self.document['original_text_sha256']:
            raise ValueError('Profile executable digest mismatch')
        for site in self.words:
            if not text['va'] + 4 <= site <= text['va'] + text['filesize'] - 4:
                raise ValueError('Preserved word outside executable segment')
        # All section/absolute symbols are checked, not only exported names.
        tables = [at for cmd, at, size in module['commands'] if cmd == 2 and size == 24]
        if len(tables) != 1:
            raise ValueError('Profile module lacks unique symbol table')
        off, count, strings, length = struct.unpack_from('<4I', original_data, tables[0] + 8)
        if off + count * 16 > len(original_data) or strings + length > len(original_data):
            raise ValueError('Profile symbol table outside image')
        for i in range(count):
            _, kind, _, _, address = struct.unpack_from('<IBBHQ', original_data, off + 16 * i)
            if not kind & 0xe0 and kind & 0x0e and address in self.words:
                raise ValueError('Preserved word is a symbol entry')
        self.module_checked = True

    def resolve_binding(self, site, target, identifier, original_symbols, native_symbols, resolver):
        if not self.module_checked or identifier != self.module:
            raise ValueError('Symbol-relative binding outside checked module')
        if site in self.used_bindings:
            raise ValueError('Repeated symbol-relative profile site')
        item = self.bindings[site]
        if target != item['target'] or original_symbols.get(target):
            raise ValueError('Symbol-relative target changed or has an exact symbol')
        base = item['original_symbol_va']
        if set(original_symbols.get(base, ())) != set(item['original_symbol_names']):
            raise ValueError('Original symbol aliases changed')
        # Reuse the ordinary exact-symbol resolver. No nearest-symbol search.
        replacement = resolver(base, original_symbols, native_symbols) + item['addend']
        if not self._address(replacement):
            raise ValueError('Native symbol-relative address overflow')
        self.used_bindings.add(site)
        return replacement

    def preserve_word(self, identifier, site, word, previous):
        if not self.module_checked or identifier != self.module:
            raise ValueError('Preserved word outside checked module')
        if site in self.used_words:
            raise ValueError('Repeated preserved-word profile site')
        item = self.words[site]
        if (hashlib.sha256(struct.pack('<I', word)).hexdigest() != item['word_sha256'] or
                hashlib.sha256(struct.pack('<I', previous)).hexdigest() != item['previous_word_sha256']):
            raise ValueError('Preserved word or previous instruction changed')
        if previous & 0xfffffc1f != 0xd61f0000 and previous & 0xfc000000 != 0x14000000:
            raise ValueError('Preserved word is not after an unconditional BR/B')
        self.used_words.add(site)

    def verify_pointer_entries(self, bindings):
        if any(item['target'] in self.words for item in bindings):
            raise ValueError('Recorded pointer targets a preserved word')

    def require_bindings_consumed(self):
        if self.used_bindings != self.bindings.keys():
            raise ValueError('Unused symbol-relative profile entries')

    def require_words_consumed(self):
        if self.used_words != self.words.keys():
            raise ValueError('Unused preserved-word profile entries')

    def summary(self):
        return {'module': self.module, 'profile_sha256': self.digest,
                'original_text_sha256': self.document['original_text_sha256'],
                'symbol_relative_sites': len(self.used_bindings),
                'preserved_post_transfer_words': len(self.used_words),
                'control_flow_or_register_dataflow_qualified': False,
                'runtime_qualified': False, 'no_executable_bytes_may_change': True}
