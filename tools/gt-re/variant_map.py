#!/usr/bin/env python3
"""
variant_map.py - enumerate every RPC ("variant") the Growtopia client knows,
plus the handler address and the string/constant keys each handler parses.

Why: server->client payload keys are usually inlined compares (no strings), so a
missing key is invisible in `strings`. But every variant NAME is a literal in
.rodata and its registration site sits next to `adr xN, #handler`. Once we have
the handler address we can dump the keys it compares against.

Output: JSON + a readable table.

usage:
  variant_map.py <libgrowtopia.so> [--json out.json] [--dump NAME] [--addr 0x...]
"""
import sys, json, struct, re, os, argparse
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.elf.elffile import ELFFile

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = False


class Bin:
    def __init__(self, path):
        self.path = path
        self.f = open(path, 'rb')
        self.elf = ELFFile(self.f)
        self.data = open(path, 'rb').read()
        self.segments = []
        for seg in self.elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD':
                continue
            self.segments.append({
                'vaddr': seg['p_vaddr'],
                'filesz': seg['p_filesz'],
                'memsz': seg['p_memsz'],
                'offset': seg['p_offset'],
                'flags': seg['p_flags'],
            })
        self.text = None
        for s in self.elf.iter_sections():
            if s.name in ('.text',):
                self.text = (s['sh_addr'], s['sh_offset'], s['sh_size'])
        self.str_sections = []
        for s in self.elf.iter_sections():
            if s.name in ('.rodata', '.data.rel.ro', '.data', '.bss'):
                self.str_sections.append((s['sh_addr'], s['sh_size'], s.name))

    def read(self, va, n):
        for seg in self.segments:
            if seg['vaddr'] <= va < seg['vaddr'] + seg['filesz']:
                off = seg['offset'] + (va - seg['vaddr'])
                return self.data[off:off + n]
        return b''

    def is_code(self, va):
        return self.text and self.text[0] <= va < self.text[0] + self.text[2]

    def code_bytes(self):
        va, off, size = self.text
        return va, self.data[off:off + size]

    def cstr(self, va, maxlen=200):
        b = self.read(va, maxlen)
        i = b.find(b'\x00')
        if i >= 0:
            b = b[:i]
        try:
            return b.decode('utf-8')
        except UnicodeDecodeError:
            return b.decode('latin-1')


def find_strings(bin, minlen=3):
    """all printable ascii cstrings inside the data sections, with their VA"""
    out = {}
    for va, size, name in bin.str_sections:
        if name == '.bss':
            continue
        blob = bin.read(va, size)
        for m in re.finditer(rb'[\x20-\x7e]{%d,}\x00' % minlen, blob):
            s = m.group()[:-1].decode('ascii', 'replace')
            out.setdefault(s, []).append(va + m.start())
    return out


def build_string_xrefs(bin):
    """scan .text linearly: adrp+add pairs whose result is a data VA"""
    va, code = bin.code_bytes()
    n = len(code) // 4
    insns = list(md.disasm(code, va))
    xrefs = {}   # target_va -> [insn_addr]
    # map: (addr) -> next adrp reg pending
    pending = {}  # reg -> (page_va, addr)
    for ins in insns:
        if ins.mnemonic == 'adrp':
            # adrp x8, #0x123000
            m = re.match(r'(x\d+), #(0x[0-9a-f]+)', ins.op_str)
            if not m:
                continue
            pending[m.group(1)] = (int(m.group(2), 16), ins.address)
        elif ins.mnemonic == 'add':
            m = re.match(r'(x\d+), (x\d+), #(0x[0-9a-f]+|\d+)', ins.op_str)
            if not m:
                continue
            dst, src, imm = m.group(1), m.group(2), int(m.group(3), 0)
            if src in pending:
                page, pattr = pending[src]
                xrefs.setdefault(page + imm, []).append(ins.address)
    return xrefs, insns


def disasm_at(bin, va, count):
    off = None
    for seg in bin.segments:
        if seg['vaddr'] <= va < seg['vaddr'] + seg['filesz']:
            off = seg['offset'] + (va - seg['vaddr'])
            break
    if off is None:
        return []
    code = bin.data[off:off + count * 4]
    return list(md.disasm(code, va))


def find_adr_before(bin, site, back=60, forward=20):
    """candidate `adr` targets near a registration site"""
    start = site - back * 4
    ins = disasm_at(bin, start, back + forward)
    out = []
    for i in ins:
        if i.mnemonic == 'adr':
            m = re.match(r'(x\d+), #(0x[0-9a-f]+)', i.op_str)
            if m:
                t = int(m.group(2), 16)
                if bin.is_code(t):
                    out.append((t, i.address, site - i.address))
    return out


def func_strings(bin, entry, maxins=900, xrefs_by_va=None):
    """walk a handler and collect the string literals it references"""
    ins = disasm_at(bin, entry, maxins)
    found = []
    pending = {}
    consts = []
    for i in ins:
        if i.mnemonic == 'adrp':
            m = re.match(r'(x\d+|w\d+), #(0x[0-9a-f]+)', i.op_str)
            if m:
                pending[m.group(1)] = int(m.group(2), 16)
        elif i.mnemonic == 'add':
            m = re.match(r'(x\d+|w\d+), (x\d+|w\d+), #(0x[0-9a-f]+|\d+)', i.op_str)
            if m and m.group(2) in pending:
                va = pending[m.group(2)] + int(m.group(3), 0)
                s = bin.cstr(va, 120)
                if s and all(0x20 <= ord(c) < 0x7f for c in s) and len(s) >= 2:
                    found.append((va, s))
        elif i.mnemonic in ('movz', 'movk'):
            consts.append((i.address, i.mnemonic, i.op_str))
    # dedupe keeping order
    seen, uniq = set(), []
    for va, s in found:
        if va not in seen:
            seen.add(va)
            uniq.append((va, s))
    return uniq, consts, ins


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lib')
    ap.add_argument('--json', default=None)
    ap.add_argument('--dump', default=None, help='dump handler disasm for a variant name')
    ap.add_argument('--addr', default=None, help='dump disasm at an address')
    ap.add_argument('--cache', default=os.path.expanduser('~/opt/re-tools/cache'))
    args = ap.parse_args()

    bin = Bin(args.lib)
    os.makedirs(args.cache, exist_ok=True)
    xcache = os.path.join(args.cache, 'strxrefs.json')
    scache = os.path.join(args.cache, 'strings.json')

    if os.path.exists(scache):
        strings = json.load(open(scache))
        print(f'[i] loaded {len(strings)} strings from cache', file=sys.stderr)
    else:
        strings = find_strings(bin)
        json.dump(strings, open(scache, 'w'))
        print(f'[i] indexed {len(strings)} strings', file=sys.stderr)

    if os.path.exists(xcache):
        xr = json.load(open(xcache))
        xrefs = {int(k): v for k, v in xr.items()}
        print(f'[i] loaded {len(xrefs)} string xrefs from cache', file=sys.stderr)
    else:
        print('[i] scanning .text for adrp+add (a few minutes)...', file=sys.stderr)
        xrefs, _ = build_string_xrefs(bin)
        json.dump({str(k): v for k, v in xrefs.items()}, open(xcache, 'w'))
        print(f'[i] {len(xrefs)} string xrefs', file=sys.stderr)

    if args.addr:
        a = int(args.addr, 16)
        for i in disasm_at(bin, a, 200):
            print(f'{i.address:#x}: {i.mnemonic}\t{i.op_str}')
        return

    if args.dump:
        cands = []
        for va in strings.get(args.dump, []):
            for site in xrefs.get(va, []):
                cands += find_adr_before(bin, site)
        print(f'# {args.dump}: {len(cands)} candidate handlers', file=sys.stderr)
        for t, site, d in sorted(set(cands), key=lambda x: -x[2])[:4]:
            print(f'\n=== candidate handler {t:#x} (reg site {site:#x}, dist {d}) ===')
            ss, consts, ins = func_strings(bin, t, 600)
            for va, s in ss:
                print(f'   str {va:#x}  {s!r}')
            for i in ins[:220]:
                print(f'   {i.address:#x}: {i.mnemonic}\t{i.op_str}')
        return

    # default: enumerate every On* name
    names = sorted(k for k in strings if re.fullmatch(r'On[A-Z][A-Za-z0-9]*', k))
    report = {}
    for name in names:
        cands = []
        for va in strings[name]:
            for site in xrefs.get(va, []):
                cands += find_adr_before(bin, site)
        cands = sorted(set(cands), key=lambda x: -x[2])
        entry = cands[0][0] if cands else None
        rec = {'string_va': [hex(v) for v in strings[name]], 'xrefs': [hex(s) for va in strings[name] for s in xrefs.get(va, [])], 'handler': hex(entry) if entry else None, 'candidates': [hex(c[0]) for c in cands[:4]], 'strings': []}
        if entry:
            ss, consts, ins = func_strings(bin, entry, 500)
            rec['strings'] = [[hex(v), s] for v, s in ss]
        report[name] = rec
        print(f'{name:42s} handler={rec["handler"] or "-":>12s}  keys={len(rec["strings"])}')

    if args.json:
        json.dump(report, open(args.json, 'w'), indent=1)
        print(f'[i] wrote {args.json}', file=sys.stderr)


if __name__ == '__main__':
    main()
