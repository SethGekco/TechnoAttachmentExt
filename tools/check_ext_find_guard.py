#!/usr/bin/env python3
"""
Flag Container::Find() results that are dereferenced without a null test.

WHY THIS EXISTS
---------------
Phobos' Container has two lookups that differ in a way the names do not advertise:

    TryFind(key)  -> null-checks the KEY, then looks up
    Find(key)     -> does NOT null-check, and returns null for any key MISSING
                     from the map

So Find() is nullable twice over, and the second case is the dangerous one: a
perfectly valid, non-null object that was simply never registered.

That shipped a crash. TechnoClass::KillCargo recurses into each passenger it
detaches, and a cargo list can hold any ObjectClass -- a TerrainClass (a tree)
in a building's passenger list reached a hook seated inside KillCargo, was
blind-cast to TechnoClass*, and was looked up in TechnoExt::ExtMap. Trees are
never in that map. Find() returned null, the caller added the ChildAttachments
offset and read address 0x10. See HANDOFF-crash-20261019-224522.md.

Note that TryFind() would NOT have prevented it: the key was valid, just absent.
Only a null test on the RESULT works, which is what this checks for.

WHAT IT FLAGS
-------------
  1. Immediate dereference:   Ext::ExtMap.Find(x)->Member
  2. Assign then dereference: auto p = Ext::ExtMap.Find(x);  ...  p->Member
     with no null test on p in between.

(2) scans forward only to the end of the enclosing block, and accepts any of the
usual guard shapes: `if (!p)`, `if (p)`, `p &&`, `p ?`, `assert(p)`, or a
ternary/short-circuit that mentions p before the arrow.

KNOWN LIMITS: brace-depth tracking is textual, so a brace inside a string or
comment can end a scope early and hide a real site. It errs toward silence
rather than noise -- a missed finding, never a false alarm, is the intended
failure direction for a checker that runs on every commit.
"""
import re, sys, pathlib

FIND = re.compile(r'\b(\w+(?:::\w+)*)\.(?:ExtMap|Map)?\.?Find\s*\(')
IMMEDIATE = re.compile(r'\b\w+(?:::\w+)*\s*\.\s*Find\s*\([^;]*?\)\s*->')
ASSIGN = re.compile(r'^\s*(?:auto|[\w:<>*&\s]+?)\s*(?:const\s*)?[*&]?\s*(\w+)\s*=\s*[^;]*\bFind\s*\(')

def guarded(name, text):
    """Does `text` contain a null test on `name`?"""
    n = re.escape(name)
    return any(re.search(p, text) for p in (
        rf'if\s*\(\s*!\s*{n}\s*[)&|]',      # if (!p)  / if (!p ||
        rf'if\s*\(\s*{n}\s*[)&]',           # if (p)   / if (p &&
        rf'!\s*{n}\s*(?:\|\||\))',          # !p || / !p)
        rf'\b{n}\s*&&',                     # p &&
        rf'\b{n}\s*\?',                     # p ?
        rf'assert\s*\(\s*{n}\b',
        rf'\breturn\s+{n}\s*;',             # handed to a caller to check
    ))

def check(path):
    out, lines = [], path.read_text(errors='replace').split('\n')
    for i, line in enumerate(lines):
        code = line.split('//')[0]
        if IMMEDIATE.search(code):
            out.append((i + 1, line.strip()[:88], 'dereferenced immediately'))
            continue
        m = ASSIGN.match(code)
        if not m:
            continue
        name = m.group(1)
        if name in ('if', 'return', 'while', 'for'):
            continue
        # Walk forward to the end of the enclosing block.
        depth, window = 0, []
        for nxt in lines[i + 1:]:
            c = nxt.split('//')[0]   # comments stripped: a guard and its arrow
            depth += c.count('{') - c.count('}')
            window.append(c)         # can be separated by a trailing comment
            if depth < 0:
                break
        body = '\n'.join(window)
        arrow = re.search(rf'\b{re.escape(name)}\s*->', body)
        if arrow and not guarded(name, body[:arrow.start()]):
            off = body[:arrow.start()].count('\n')
            out.append((i + 1, line.strip()[:88],
                        f'-> at line {i + 2 + off} with no null test between'))
    return out

def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    files = sorted(list(root.rglob('*.cpp')) + list(root.rglob('*.h')))
    bad = 0
    for f in files:
        for ln, src, why in check(f):
            print(f'{f}:{ln}: Find() result {why}\n    {src}')
            bad += 1
    print(f'check_ext_find_guard: scanned {len(files)} files')
    if bad:
        print(f'check_ext_find_guard: FAILED -- {bad} unguarded Find() result(s).')
        print('  Find() returns null for any key not in the map, even a valid one.')
        return 1
    print('check_ext_find_guard: OK.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
