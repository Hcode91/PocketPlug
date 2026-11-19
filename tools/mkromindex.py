#!/usr/bin/env python3
# PocketPlug - builds the ROM index the file sorter identifies MAME sets with.
# SPDX-License-Identifier: BSD-3-Clause
#
# Reads a MAME source tree (src/mame, src/devices) and writes a compact
# gzipped index of every machine, its ROM chips (CRC32 + size) and its disk
# images (SHA1). Only names, years, makers and checksums are taken: facts,
# no code. Usage: mkromindex.py <mame source dir> <out romindex.bin.gz>
#
# Format (little endian), after the magic "PPRX" and u16 version 2:
#   u32 nSets, then per set: name, description, maker, year as u8-length
#       strings, u8 kind (A arcade, C console / plug-and-play, K computer,
#       S other system, D device / BIOS ROMs), u16 parent (0xffff none),
#       u16 number of ROM chips
#   u32 nRoms, then nRoms x (u32 crc), (u32 size, 0 = unknown), (u16 set),
#       sorted by crc
#   u32 nDisks, then nDisks x (20-byte sha1, u16 set)
#   u16 nLists, then per software list (hash/*.xml with disks): name, description
#   u32 nSoftDisks, then per disk image in those lists: 20-byte sha1, u16 list,
#       the disk's name (Redump-style: "Crash Bandicoot (Europe)") as a u8-length string

import gzip, os, re, struct, sys
import xml.etree.ElementTree as ET

src, out = sys.argv[1], sys.argv[2]

def split_args(s):
    """Top-level comma split of a macro's arguments (quotes and brackets kept)."""
    args, cur, depth, q = [], '', 0, False
    i = 0
    while i < len(s):
        ch = s[i]
        if q:
            cur += ch
            if ch == '\\' and i + 1 < len(s):
                cur += s[i + 1]; i += 1
            elif ch == '"':
                q = False
        elif ch == '"':
            q = True; cur += ch
        elif ch in '([{':
            depth += 1; cur += ch
        elif ch in ')]}':
            depth -= 1; cur += ch
        elif ch == ',' and depth == 0:
            args.append(cur.strip()); cur = ''
        else:
            cur += ch
        i += 1
    if cur.strip():
        args.append(cur.strip())
    return args

def unquote(s):
    s = s.strip()
    if s.startswith('"') and s.endswith('"'):
        s = s[1:-1]
    return s.replace('\\"', '"').replace('\\\\', '\\')

def strip_comments(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)

rom_blocks = {}     # set name -> list of (crc, size)
disk_blocks = {}    # set name -> list of sha1
machines = {}       # set name -> (kind, parent, year, maker, desc)
devices = {}        # device short name -> (desc, ROM block name), from DEFINE_DEVICE_TYPE
used_blocks = set() # ROM blocks a device stands for
aliases = {}        # machine -> machine whose ROM list it uses

re_romstart = re.compile(r'ROM_START\(\s*(\w+)\s*\)(.*?)ROM_END', re.S)
re_load = re.compile(r'"([^"]*)"\s*,\s*([^,]+?)\s*,\s*([^,]+?)\s*,\s*CRC\(\s*([0-9a-fA-F]{8})\s*\)')
re_crc = re.compile(r'CRC\(\s*([0-9a-fA-F]{8})\s*\)')
re_disk = re.compile(r'DISK_IMAGE\w*\(\s*"[^"]*"\s*,\s*[^,]+,\s*SHA1\(\s*([0-9a-fA-F]{40})\s*\)')
re_mach = re.compile(r'^\s*(GAME|GAMEL|CONS|COMP|SYST)\s*\((.*?)\)\s*$', re.M | re.S)

def parse_size(s):
    try:
        return int(eval(s.replace('U', '').replace('u', ''), {}, {}))
    except Exception:
        return 0

for root in ('src/mame', 'src/devices'):
    for dp, _, files in os.walk(os.path.join(src, root)):
        for fn in files:
            if not fn.endswith(('.cpp', '.h', '.hxx', '.ipp')):
                continue
            try:
                t = strip_comments(open(os.path.join(dp, fn), encoding='utf-8', errors='replace').read())
            except OSError:
                continue
            for m in re_romstart.finditer(t):
                name, body = m.group(1), m.group(2)
                roms, seen = [], set()
                for l in re_load.finditer(body):
                    crc = int(l.group(4), 16)
                    size = parse_size(l.group(3))
                    if size > 0x7fffffff or size < 0:
                        size = 0
                    if (crc, size) not in seen:
                        seen.add((crc, size)); roms.append((crc, size))
                crcs = {c for c, _ in roms}
                for c in re_crc.finditer(body):           # other macro forms: size unknown
                    v = int(c.group(1), 16)
                    if v not in crcs:
                        crcs.add(v); roms.append((v, 0))
                rom_blocks.setdefault(name, roms)
                disks = [bytes.fromhex(d.group(1)) for d in re_disk.finditer(body)]
                if disks:
                    disk_blocks.setdefault(name, disks)
            # "#define rom_aa4101 rom_aa3000": a machine sharing another's ROM list
            for a in re.finditer(r'#define\s+rom_(\w+)\s+rom_(\w+)', t):
                aliases[a.group(1)] = a.group(2)
            # devices with ROMs (keyboards, BIOS boards): MAME names their zips
            # after the device's short name, not the ROM block
            cls_block = {}
            for r in re.finditer(r'(\w+)::device_rom_region\(\)[^{]*\{[^}]*?ROM_NAME\(\s*(\w+)\s*\)', t, re.S):
                cls_block[r.group(1)] = r.group(2)
            for d in re.finditer(r'DEFINE_DEVICE_TYPE(_PRIVATE)?\(([^;]*?)\)\s*;?', t, re.S):
                a = split_args(d.group(2))
                if d.group(1):
                    if len(a) < 5: continue
                    cls, short, desc = a[2], unquote(a[3]), unquote(a[4])
                else:
                    if len(a) < 4: continue
                    cls, short, desc = a[1], unquote(a[2]), unquote(a[3])
                if cls in cls_block and re.fullmatch(r'\w+', short):
                    devices[short] = (desc, cls_block[cls])
            # machine definitions: one statement may span lines
            for line in re.finditer(r'^\s*(GAME|GAMEL|CONS|COMP|SYST)\s*\(', t, re.M):
                start = line.end()
                depth, i, q = 1, start, False
                while i < len(t) and depth:
                    ch = t[i]
                    if q:
                        if ch == '\\': i += 1
                        elif ch == '"': q = False
                    elif ch == '"': q = True
                    elif ch == '(': depth += 1
                    elif ch == ')': depth -= 1
                    i += 1
                args = split_args(t[start:i - 1])
                kind = line.group(1)
                try:
                    if kind in ('GAME', 'GAMEL'):
                        year, name, parent, company, desc = args[0], args[1], args[2], args[8], args[9]
                        k = 'A'
                    else:
                        year, name, parent, company, desc = args[0], args[1], args[2], args[8], args[9]
                        k = {'CONS': 'C', 'COMP': 'K', 'SYST': 'S'}[kind]
                except IndexError:
                    continue
                if not re.fullmatch(r'\w+', name):
                    continue
                machines[name] = (k, parent.strip(), unquote(year), unquote(company), unquote(desc))

for a, b in aliases.items():
    if a not in rom_blocks and b in rom_blocks:
        rom_blocks[a] = rom_blocks[b]
    if a not in disk_blocks and b in disk_blocks:
        disk_blocks[a] = disk_blocks[b]
for short, (desc, block) in devices.items():
    if short in machines or block not in rom_blocks:
        continue
    if short != block:
        rom_blocks.setdefault(short, rom_blocks[block])
        used_blocks.add(block)
    machines[short] = ('D', '0', '', '', desc)
for b in used_blocks:
    if b not in machines and b not in devices:
        rom_blocks.pop(b, None)

names = sorted(set(machines) | set(rom_blocks))
idx = {n: i for i, n in enumerate(names)}
assert len(names) < 0xffff, len(names)

def s8(s):
    b = s.encode('utf-8')[:255]
    return bytes([len(b)]) + b

body = bytearray(b'PPRX' + struct.pack('<H', 2))
body += struct.pack('<I', len(names))
for n in names:
    k, parent, year, maker, desc = machines.get(n, ('D', '0', '', '', ''))
    p = idx.get(parent, 0xffff)
    body += s8(n) + s8(desc) + s8(maker) + s8(year)
    body += struct.pack('<cHH', k.encode(), p, min(len(rom_blocks.get(n, [])), 0xffff))

rows = []
for n, roms in rom_blocks.items():
    for crc, size in roms:
        rows.append((crc, size, idx[n]))
rows.sort()
body += struct.pack('<I', len(rows))
for crc, size, s in rows:
    body += struct.pack('<IIH', crc, size, s)

drows = [(sha, idx[n]) for n, l in disk_blocks.items() for sha in l]
body += struct.pack('<I', len(drows))
for sha, s in drows:
    body += sha + struct.pack('<H', s)

# software lists: the CD / DVD / hard disk images MAME knows by SHA1
lists, soft = [], []
hashdir = os.path.join(src, 'hash')
for fn in sorted(os.listdir(hashdir)) if os.path.isdir(hashdir) else []:
    if not fn.endswith('.xml'):
        continue
    path = os.path.join(hashdir, fn)
    if '<disk ' not in open(path, encoding='utf-8', errors='replace').read():
        continue
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        continue
    li = len(lists)
    lists.append((root.get('name', fn[:-4]), root.get('description', '')))
    for sw in root.iter('software'):
        for d in sw.iter('disk'):
            sha = d.get('sha1')
            if sha and len(sha) == 40 and d.get('status') != 'nodump':
                soft.append((bytes.fromhex(sha), li, d.get('name') or sw.findtext('description') or sw.get('name')))
body += struct.pack('<H', len(lists))
for n, desc in lists:
    body += s8(n) + s8(desc)
body += struct.pack('<I', len(soft))
for sha, li, n in soft:
    body += sha + struct.pack('<H', li) + s8(n)

with gzip.open(out, 'wb', compresslevel=9) as f:
    f.write(body)
print('lists', len(lists), 'soft disks', len(soft))
print('sets', len(names), 'machines', len(machines), 'roms', len(rows), 'disks', len(drows),
      'bytes', len(body), '->', os.path.getsize(out))
