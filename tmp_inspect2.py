import struct, sys

path = sys.argv[1]
with open(path, 'rb') as f:
    header = f.read(0x800)
    file_length, = struct.unpack_from('<i', header, 0x08)
    tag_off, tag_size = struct.unpack_from('<ii', header, 0x10)
    f.seek(tag_off)
    data = f.read(tag_size)
inst, = struct.unpack_from('<I', data, 0)
count, = struct.unpack_from('<i', data, 0x0C)
base = inst - 0x24
print('%s: name=%r build=%r filelen=%#x tdsize=%#x base=%#x count=%d signature=%r' % (
    path.rsplit('\\', 1)[-1], header[0x20:0x40].split(b'\0')[0], header[0x40:0x60].split(b'\0')[0],
    file_length, tag_size, base, count, data[0x20:0x24]))

entries = []
for i in range(count):
    o = 0x24 + i * 0x20
    group = data[o:o+4][::-1].decode('latin-1')
    addr, inres = struct.unpack_from('<II', data, o + 0x14)
    entries.append((i, group, addr, inres))

scenario = entries[0]
print('  scenario', scenario)
end = max(e[2] - base for e in entries if e[3] == 0 and base <= e[2] < base + tag_size)
print('  last tag data offset %#x' % end)
# the structure bsp tags, whose blocks are in the tag data
for e in entries:
    if e[1] != 'scnr':
        continue
    o = e[2] - base
    refs = struct.unpack_from('<III', data, o + 0x5A4)
    print('   scenario bsps: count=%d addr=%#x' % (refs[0], refs[1]))
    r = refs[1] - base
    for b in range(refs[0]):
        fileoff, = struct.unpack_from('<i', data, r + b * 0x20)
        bsize, linked = struct.unpack_from('<Ii', data, r + b * 0x20 + 4)
        idx, = struct.unpack_from('<i', data, r + b * 0x20 + 0x1C)
        print('   bsp file=%#x size=%#x linked=%#x index=%d' % (fileoff, bsize, linked, idx))
        print('   bsp ends at file %#x' % (fileoff + bsize))
