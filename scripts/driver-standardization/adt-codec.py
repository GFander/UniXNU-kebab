"""Canonical ADT codec reused verbatim from the qualified owned-memory packer."""
import struct

def parse(data, at=0):
    count, children = struct.unpack_from('<II', data, at)
    at += 8
    properties, nodes = [], []
    for _ in range(count):
        key, size = struct.unpack_from('<32sI', data, at)
        at += 36
        assert at + size <= len(data)
        properties.append((key.rstrip(b'\0').decode('ascii'), data[at:at + size]))
        at += (size + 3) & ~3
    assert len(dict(properties)) == len(properties), 'duplicate ADT property'
    for _ in range(children):
        node, at = parse(data, at)
        nodes.append(node)
    return (properties, nodes), at


def encode(node):
    properties, nodes = node
    result = struct.pack('<II', len(properties), len(nodes))
    for key, value in properties:
        result += struct.pack('<32sI', key.encode('ascii'), len(value))
        result += value + bytes(-len(value) % 4)
    return result + b''.join(encode(child) for child in nodes)
