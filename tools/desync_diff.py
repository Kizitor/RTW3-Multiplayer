"""Compares the two PCs' DESYNC reports of one live-battle DESYNC (RTW3MP data folder,
ls_desync_<A|B>_<battle>_<epoch>_<minute>.txt; each PC writes its own when A finds the DESYNC).

Usage: python desync_diff.py <A report> <B report> [--words N]

Prints, per kept minute, the first shared draw whose call site differs (the routine that drew on one PC only; rva =
address - image base, 0x830000 + rva in the 1.01.44 analysis image), then, for the raw object bytes of each kept
boundary (the end of a minute) and of the start of the minute that parted (after this PC's display ran between the
minutes), the fields that differ: by RTTI name; references (classes, strings, methods, pointers, dynamic arrays) and
the unset third dword of 12-byte records (TPointLL) are counted apart. Then the hashed state lines that differ."""
import re
import sys

KIND = {'M': 'TfrmMain', 'F': 'TForce', 'D': 'TDivision', 'S': 'TFightingShip', 'A': 'TAirFormation', 'T': 'TTorpedo',
        'U': 'TSubmarine'}
# TTypeKind values that hold references: tkString 5, tkClass 7, tkMethod 8, tkLString 10, tkWString 11, tkVariant 12,
# tkInterface 15, tkDynArray 17, tkUString 18, tkClassRef 19, tkPointer 20, tkProcedure 21
REF_KINDS = {5, 7, 8, 10, 11, 12, 15, 17, 18, 19, 20, 21}
TK_RECORD = 14


def parse(path):
    rep = {'head': [], 'draws': {}, 'fields': {}, 'raw': {}, 'state': []}
    cur = None
    obj = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.rstrip('\r\n')
            m = re.match(r'draws minute (-?\d+) count (\d+)$', line)
            if m:
                cur = rep['draws'].setdefault(int(m[1]), [])
                obj = None
                continue
            m = re.match(r'fields (\w+):(.*)$', line)
            if m:
                fs = {}
                for o, n in re.findall(r' ([0-9a-f]+)=(\S+)', m[2]):
                    name, _, kind = n.partition(':')
                    fs[int(o, 16)] = (name, int(kind) if kind.lstrip('-').isdigit() else None)
                rep['fields'][m[1]] = fs
                cur = None
                continue
            m = re.match(r'raw (boundary|start) (-?\d+)$', line)
            if m:
                cur = rep['raw'].setdefault((m[1], int(m[2])), {})
                obj = None
                continue
            if line == 'state':
                cur = rep['state']
                obj = None
                continue
            if isinstance(cur, dict):
                m = re.match(r'obj (.*) size=(\d+)$', line)
                if m:
                    obj = cur.setdefault(m[1], bytearray())
                    continue
                m = re.match(r'  \+([0-9a-f]+) ([0-9a-f]*)$', line)
                if m and obj is not None:
                    obj.extend(bytes.fromhex(m[2]))
                continue
            if isinstance(cur, list) and cur is not rep['state']:
                if re.fullmatch(r'[0-9a-f]+', line):
                    cur.append(int(line, 16))
                continue
            if cur is rep['state']:
                rep['state'].append(line)
                continue
            rep['head'].append(line)
    return rep


def field_at(fields, cls, off):
    fs = fields.get(cls, {})
    best = None
    for o in fs:
        if o <= off and (best is None or o > best):
            best = o
    return best, (fs[best] if best is not None else (None, None))


def pointer_like(v):
    return 0x10000 <= v < 0x80000000 and v % 4 == 0


def object_diffs(fields, cls, xa, xb):
    """Differing fields (RTTI offsets; the classes are packed): [(label, offset, A hex, B hex)], references count."""
    fs = fields.get(cls, {})
    offs = sorted(fs)
    n = min(len(xa), len(xb))
    out, refs, seen = [], 0, set()
    for off in range(n):
        if xa[off] == xb[off]:
            continue
        start, (name, kind) = field_at(fields, cls, off)
        nxt = next((o for o in offs if o > (start if start is not None else off)), n)
        if start is None or off >= nxt or off - start >= 16:  # no RTTI field here: the 4 bytes around it
            start, name, kind, nxt = off - off % 4, None, None, off - off % 4 + 4
        if start in seen:
            continue
        seen.add(start)
        end = min(nxt, start + 16, n)
        va, vb = bytes(xa[start:end]), bytes(xb[start:end])
        if kind in REF_KINDS:
            refs += 1
            continue
        if kind == TK_RECORD and end - start == 12 and va[:8] == vb[:8]:  # a TPointLL: its third dword is often unset
            refs += 1
            continue
        if kind is None and end - start == 4 and pointer_like(int.from_bytes(va, 'little')) \
                and pointer_like(int.from_bytes(vb, 'little')):
            refs += 1
            continue
        out.append((f'{name or "?"} (+{start:#x}, {end - start} bytes)', start, va.hex(), vb.hex()))
    return out, refs


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    words = 12
    if '--words' in sys.argv:
        words = int(sys.argv[sys.argv.index('--words') + 1])
    a, b = parse(sys.argv[1]), parse(sys.argv[2])
    for h in a['head'][:3]:
        print('A:', h)
    for h in b['head'][:3]:
        print('B:', h)
    print()
    for mnt in sorted(set(a['draws']) & set(b['draws'])):
        da, db = a['draws'][mnt], b['draws'][mnt]
        i = 0
        while i < len(da) and i < len(db) and da[i] == db[i]:
            i += 1
        if i == len(da) == len(db):
            print(f'minute {mnt}: {len(da)} shared draws, the same call sites on both PCs')
            continue
        print(f'minute {mnt}: A {len(da)} draws, B {len(db)}; the call sites part at draw {i}')
        lo = max(0, i - 8)
        for k in range(lo, i):
            print(f'   {k:6d}  both {da[k]:#08x} (0x{0x830000 + da[k]:x})')
        for k in range(i, i + 10):
            x = f'{da[k]:#08x} (0x{0x830000 + da[k]:x})' if k < len(da) else '-'
            y = f'{db[k]:#08x} (0x{0x830000 + db[k]:x})' if k < len(db) else '-'
            print(f'   {k:6d}  A {x:24s}  B {y}')
    print()
    for key in sorted(set(a['raw']) & set(b['raw']), key=lambda k: (k[1], k[0] == 'boundary')):
        ra, rb = a['raw'][key], b['raw'][key]
        only = sorted(set(ra) ^ set(rb))
        what = f'raw {key[0]} {key[1]}' + (' (the minute\'s start, after the display ran)' if key[0] == 'start' else '')
        print(f'{what}: {len(ra)} / {len(rb)} objects' + (f'; on one PC only: {only[:6]}' if only else ''))
        shown = 0
        for tag in [t for t in ra if t in rb]:
            cls = KIND.get(tag[:1], '?')
            diffs, refs = object_diffs(a['fields'], cls, ra[tag], rb[tag])
            if diffs:
                print(f'  {tag} ({cls}): {len(diffs)} fields differ' + (f', {refs} references apart' if refs else ''))
                for label, off, va, vb in diffs[:words]:
                    print(f'     {label:44s} A {va}  B {vb}')
                shown += 1
        if not shown:
            print('  no difference but references')
    print()
    sa, sb = a['state'], b['state']
    nd = 0
    for la, lb in zip(sa, sb):
        if la != lb:
            nd += 1
            if nd <= 20:
                print('state A:', la[:200])
                print('state B:', lb[:200])
    print(f'state: {nd} lines differ ({len(sa)} / {len(sb)} lines)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
