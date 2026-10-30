"""Compare a raw RomFS image against its source directory (read only): header, file table, then the first,
middle and last 4 KiB of every file. usage: check_romfs_image.py <romfs.bin> <romfs dir> [max files]"""
import os
import struct
import sys

img_path, root = sys.argv[1], sys.argv[2]
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 1 << 30
f = open(img_path, 'rb')
hdr = struct.unpack('<10Q', f.read(0x50))
dir_meta, dir_meta_sz, file_meta, file_meta_sz, data_off = hdr[3], hdr[4], hdr[7], hdr[8], hdr[9]
f.seek(dir_meta)
dirs = f.read(dir_meta_sz)
f.seek(file_meta)
files = f.read(file_meta_sz)


def dir_path(off):
    parts = []
    while off != 0xFFFFFFFF:
        parent, sib, child, fchild, hnext, nlen = struct.unpack_from('<6I', dirs, off)
        name = dirs[off + 24:off + 24 + nlen].decode()
        if name:
            parts.append(name)
        if off == 0:
            break
        off = parent
    return '/'.join(reversed(parts))


bad = checked = missing = 0
# file entry: parent u32, sibling u32, data offset u64, data size u64, hash-next u32, name len u32, name
off = 0
while off + 32 <= len(files) and checked < limit:
    parent, sib, doff, dsize, hnext, nlen = struct.unpack_from('<IIQQII', files, off)
    name = files[off + 32:off + 32 + nlen].decode(errors='replace')
    off += 32 + ((nlen + 3) & ~3)
    path = os.path.join(root, dir_path(parent), name)
    if not os.path.exists(path):
        missing += 1
        continue
    checked += 1
    src_size = os.path.getsize(path)
    if src_size != dsize:
        bad += 1
        print('SIZE', path, src_size, dsize)
        continue
    with open(path, 'rb') as s:
        for pos in (0, max(0, dsize // 2 - 2048), max(0, dsize - 4096)):
            s.seek(pos)
            a = s.read(4096)
            f.seek(data_off + doff + pos)
            if a != f.read(len(a)):
                bad += 1
                print('DATA', path, 'at', pos)
                break
print('checked', checked, 'bad', bad, 'not-in-source', missing)
