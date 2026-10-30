import struct, sys

path = sys.argv[1]
with open(path, 'rb') as f:
    header = f.read(0x800)
    tag_off, tag_size = struct.unpack_from('<ii', header, 0x10)
    f.seek(tag_off)
    data = f.read(tag_size)
inst, = struct.unpack_from('<I', data, 0)
count, = struct.unpack_from('<i', data, 0x0C)
base = inst - 0x24
print(path)
print('  name=%r build=%r tdoff=%#x tdsize=%#x instances=%#x count=%d base=%#x' % (
    header[0x20:0x40].split(b'\0')[0], header[0x40:0x60].split(b'\0')[0], tag_off, tag_size, inst, count, base))

def name_of(addr):
    if not base <= addr < base + len(data):
        return None
    p = addr - base
    e = data.index(b'\0', p)
    return data[p:e].decode('latin-1')

entries = []
for i in range(count):
    o = 0x24 + i * 0x20
    group = data[o:o+4][::-1].decode('latin-1')
    parent = data[o+4:o+8][::-1].decode('latin-1')
    h, name, addr, inres = struct.unpack_from('<IIII', data, o + 0x0C)
    entries.append((i, group, parent, name_of(name), addr, inres))

from collections import Counter
print('  groups:', dict(sorted(Counter(e[1] for e in entries).items())))
for e in entries:
    if e[1] in ('srud', 'srac', 'srwd', 'buym', 'm1ed', 'm2ed', 'm3ed', 't1ed', 't2ed', 't3ed', 'bipd', 'matg', 'scnr'):
        print('   [%4d] %s (parent %s) @%#010x inres=%d %s' % (e[0], e[1], e[2], e[4], e[5], e[3]))
