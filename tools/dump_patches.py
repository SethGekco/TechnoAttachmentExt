#!/usr/bin/env python3
"""
Map every runtime patch in a gamemd address range, from a full-memory minidump.

For each byte range where the live image differs from the on-disk exe, report:
which DLL's handler the detour stub calls, where the stub resumes, the original
(displaced) bytes — and whether those bytes contain an IP-relative branch,
which Syringe copies into the stub UNRELOCATED.

Why this exists: the 20260927-203946 EIP=0 crash was pinned to an IntelExt hook
this way. The prior session diffed only the entry bytes of the functions on the
stack, concluded "unpatched", and spent its budget on a wrong suspect. The
lesson is mechanical, so this is the mechanical form of it: diff WHOLE
functions, then read the stubs out of the dump — the handler target names the
owner, and the displaced bytes name the defect class.

Usage:
    dump_patches.py --dmp extcrashdump.dmp --exe gamemd-spawn.exe \
                    --range 0x508C30-0x509100 [--range ...]

Requires the dump to carry full memory (Memory64List), which the Ares/Phobos
`extcrashdump.dmp` does.
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from minidump import MD


def pe_sections(exe_bytes):
    pe = struct.unpack_from('<I', exe_bytes, 0x3C)[0]
    nsec = struct.unpack_from('<H', exe_bytes, pe + 6)[0]
    optsz = struct.unpack_from('<H', exe_bytes, pe + 20)[0]
    base = struct.unpack_from('<I', exe_bytes, pe + 24 + 28)[0]
    out, off = [], pe + 24 + optsz
    for _ in range(nsec):
        vsz, va, rsz, ro = struct.unpack_from('<IIII', exe_bytes, off + 8)
        out.append((base + va, max(vsz, rsz), ro))
        off += 40
    return out


def disk_read(exe_bytes, secs, addr, n):
    for va, sz, ro in secs:
        if va <= addr < va + sz:
            return exe_bytes[ro + (addr - va):ro + (addr - va) + n]
    return None


# IP-relative instructions inside a stub's displaced-bytes copy replay against
# the wrong base. Decode with objdump rather than byte-matching: a displacement
# byte like 0x7B inside `mov [esi+0x577b],cl` is not a Jcc, and the naive scan
# used to flag exactly that.
#
# Caveat on interpretation: this models CLASSIC Syringe (0.7.x — including this
# install's "0.7.2.0 (modified)"), which copies stolen bytes verbatim
# (ReadMem in SyringeDebugger.cpp). SyringeEx — mandated by current Phobos —
# decodes and RELOCATES stolen instructions, so a flag here is only a defect
# under the classic injector.
REL_BRANCH_RE = None  # built on first use to keep import cost nil


def relative_branch_positions(b, base=0):
    import re, tempfile
    global REL_BRANCH_RE
    if REL_BRANCH_RE is None:
        REL_BRANCH_RE = re.compile(
            r'^(call|jmp|j[a-z]{1,3}|loopn?e?|je?cxz)\s+0x[0-9a-f]+$')
    with tempfile.NamedTemporaryFile(suffix='.bin', delete=False) as f:
        f.write(b)
        path = f.name
    try:
        out = subprocess.run(
            ['objdump', '-D', '-b', 'binary', '-m', 'i386', '-M', 'intel',
             f'--adjust-vma={base}', path],
            capture_output=True, text=True).stdout
    finally:
        Path(path).unlink(missing_ok=True)
    found = []
    for line in out.splitlines():
        parts = line.strip().split('\t')
        if len(parts) < 3:
            continue
        try:
            addr = int(parts[0].strip().rstrip(':'), 16)
        except ValueError:
            continue
        text = parts[-1].strip()
        if REL_BRANCH_RE.match(text):
            found.append((addr - base, text.split()[0] + ' rel'))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dmp', required=True)
    ap.add_argument('--exe', required=True)
    ap.add_argument('--range', action='append', required=True,
                    help='VA range, e.g. 0x508C30-0x509100')
    args = ap.parse_args()

    md = MD(args.dmp)
    exe = Path(args.exe).read_bytes()
    secs = pe_sections(exe)
    mods = [(b, s, n.split('\\')[-1]) for b, s, n in md.modules()]

    def sym(a):
        for b, s, n in mods:
            if b <= a < b + s:
                return f'{n}+0x{a - b:X}'
        return f'{a:08X}'

    for spec in args.range:
        lo, hi = (int(x, 16) for x in spec.split('-'))
        live = md.read(lo, hi - lo)
        disk = disk_read(exe, secs, lo, hi - lo)
        if live is None or disk is None:
            print(f'{spec}: unreadable on one side')
            continue
        print(f'== {lo:08X}..{hi:08X} ==')
        i, clean = 0, True
        while i < len(live):
            if live[i] == disk[i]:
                i += 1
                continue
            clean = False
            j = i
            while j < len(live) and live[j] != disk[j]:
                j += 1
            addr = lo + i
            print(f'  PATCH {addr:08X}..{lo + j:08X}  '
                  f'live={live[i:j].hex(" ")}  disk={disk[i:j].hex(" ")}')
            # Follow a Syringe-style `jmp stub`.
            if live[i] == 0xE9:
                stub = (addr + 5 + struct.unpack_from('<i', live, i + 1)[0]) \
                       & 0xFFFFFFFF
                sb = md.read(stub, 0x100)
                if sb:
                    k, owners, resume, disp0 = 0, [], None, None
                    while k < len(sb) - 5:
                        tgt = (stub + k + 5 +
                               struct.unpack_from('<i', sb, k + 1)[0]) \
                              & 0xFFFFFFFF
                        if sb[k] == 0xE8 and any(b <= tgt < b + s
                                                 for b, s, _ in mods):
                            owners.append(sym(tgt))
                        if sb[k:k + 7] == b'\x64\xff\x25\x14\x00\x00\x00':
                            disp0 = k + 7
                        if sb[k] == 0xE9 and 0x400000 <= tgt < 0xB93000:
                            resume = (k, tgt)
                            break
                        k += 1
                    print(f'        stub {stub:08X}  handlers={owners}  '
                          f'resume={resume[1]:08X}' if resume else
                          f'        stub {stub:08X}  handlers={owners}')
                    if disp0 is not None and resume:
                        disp = sb[disp0:resume[0]]
                        if disp:
                            print(f'        displaced: {disp.hex(" ")}')
                            for pos, kind in relative_branch_positions(disp):
                                print(f'        !! {kind} at displaced+{pos}: '
                                      f'replayed from the stub this branch is '
                                      f'NOT relocated')
            i = j
        if clean:
            print('  identical to disk')


if __name__ == '__main__':
    main()
