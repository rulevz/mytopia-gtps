#!/usr/bin/env python3
"""
codexref.py - find who references an address (function/handler) in an arm64 ELF.

Handles the three ways this lib refers to code:
  adr  xN, #target          (pc-relative, +-1MB)
  adrp xN, #page + add xN,xN,#off   (page-relative data OR code)
  8-byte LE pointer in .data.rel.ro/.data (pointer tables)

usage:
  codexref.py <lib.so> 0x135be50 [--insns 80] [--str]
  codexref.py <lib.so> --str 0x720d9b        # who loads this string
  codexref.py <lib.so> --near 0x135be50 0x400 # disasm window around
"""
import sys, re, struct, argparse
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from elftools.elf.elffile import ELFFile

md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)


class Bin:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        self.elf = ELFFile(open(path, 'rb'))
        self.segments = []
        self.sections = {}
        for seg in self.elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD':
                self.segments.append((seg['p_vaddr'], seg['p_filesz'], seg['p_offset']))
        for s in self.elf.iter_sections():
            self.sections[s.name] = (s['sh_addr'], s['sh_size'], s['sh_offset'])
        self.text_va, self.text_size, self.text_off = self.sections['.text']

    def off(self, va):
        for v, sz, o in self.segments:
            if v <= va < v + sz:
                return o + (va - v)
        return None

    def read(self, va, n):
        o = self.off(va)
        return self.data[o:o + n] if o is not None else b''

    def cstr(self, va, maxlen=160):
        b = self.read(va, maxlen)
        i = b.find(b'\x00')
        return (b[:i] if i >= 0 else b).decode('utf-8', 'replace')


def insns(bin):
    return md.disasm(bin.read(bin.text_va, bin.text_size), bin.text_va)


def find_code_refs(bin, target, adr_range=0x100000):
    """adr + (adrp,add) refs whose computed value == target"""
    out = []
    pending = {}
    for ins in insns(bin):
        if ins.mnemonic == 'adr':
            m = re.match(r'x(\d+), #(0x[0-9a-f]+)', ins.op_str)
            if m and int(m.group(2), 16) == target:
                out.append((ins.address, 'adr'))
        elif ins.mnemonic == 'adrp':
            m = re.match(r'x(\d+), #(0x[0-9a-f]+)', ins.op_str)
            if m:
                pending[m.group(1)] = int(m.group(2), 16)
        elif ins.mnemonic == 'add':
            m = re.match(r'x(\d+), x(\d+), #(0x[0-9a-f]+|\d+)', ins.op_str)
            if m and m.group(2) in pending:
                if pending[m.group(2)] + int(m.group(3), 0) == target:
                    out.append((ins.address, 'adrp+add'))
    return out


def find_ptr_refs(bin, target):
    out = []
    pat = struct.pack('<Q', target)
    for name, (va, size, off) in bin.sections.items():
        if name in ('.text', '.rodata', '.dynstr', '.dynsym', '.gnu.hash', '.hash', '.shstrtab'):
            continue
        if size > 40 * 1024 * 1024:
            continue
        blob = bin.data[off:off + size]
        start = 0
        while True:
            i = blob.find(pat, start)
            if i < 0:
                break
            out.append((va + i, name))
            start = i + 1
    return out


def disasm(bin, va, count):
    return list(md.disasm(bin.read(va, count * 4), va))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('lib')
    ap.add_argument('target', nargs='?')
    ap.add_argument('--str', default=None)
    ap.add_argument('--near', nargs=2, default=None)
    ap.add_argument('--before', type=int, default=24)
    ap.add_argument('--after', type=int, default=24)
    a = ap.parse_args()
    bin = Bin(a.lib)

    if a.near:
        start = int(a.near[0], 16) - a.before * 4
        for i in disasm(bin, start, a.before + a.after):
            print(f'{i.address:#x}: {i.mnemonic}\t{i.op_str}')
        return

    tgt = int(a.str or a.target, 16)
    refs = find_code_refs(bin, tgt) + find_ptr_refs(bin, tgt)
    print(f'# refs to {tgt:#x}: {len(refs)}')
    for va, kind in refs:
        print(f'\n=== {kind} ref @ {va:#x} ===')
        start = va - a.before * 4
        for i in disasm(bin, start, a.before + a.after):
            mark = '>>' if i.address == va else '  '
            print(f'{mark}{i.address:#x}: {i.mnemonic}\t{i.op_str}')


if __name__ == '__main__':
    main()
