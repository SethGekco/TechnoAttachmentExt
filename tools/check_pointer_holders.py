#!/usr/bin/env python3
"""
Flag writes to a pointer-walk field that are not IMMEDIATELY preceded by
`<same object>->MarkPointerHolder();`.

WHY THIS EXISTS
---------------
The AnnounceInvalidPointer hook (0x7258D0) used to walk EVERY techno's
ExtData on every death. When a house is defeated that is hundreds of walks
over thousands of technos in one frame, so it now walks only
TechnoExt::PointerHolders: the technos that may hold a pointer
InvalidatePointer clears. Every techno outside the list must hold nothing.

That only stays true if every write into one of FIELDS registers the techno
first. A missed MarkPointerHolder is not slow, it is a DANGLING POINTER: the
object it names dies, the walk never visits this techno, and the next
attachment tick calls a virtual on freed memory. The compiler cannot see the
omission, so it is checked here.

"Immediately" is literal: the previous line of code must be the mark. A game
call in between (CreateObject, Unlimbo, OnCreated...) can kill something, and
that death's walk prunes any listed techno that holds nothing yet.

WHAT IT FLAGS
-------------
  X->Field = value             (assigning nullptr is a removal and is allowed)
  X->Field.push_back(...)      (also emplace_back, insert, emplace, resize, assign, swap)
  X->Field[...] = value        (a vector element write)
  X->MapField[...]             (std::map operator[] inserts, read or not)
  alias = value, alias[...] = value, alias.push_back(...)
                               where `auto& alias = X->Field;` or
                               `for (auto& alias : X->Field)` came first: the
                               mark goes before each such write, as above

Usage:  check_pointer_holders.py [src-dir]
"""
import re
import sys
import pathlib

# The ExtData fields TechnoExt::ExtData::InvalidatePointer clears, and which
# of them are std::maps (their operator[] inserts even on a read).
FIELDS = ('ChildAttachments', 'DormantAttachments', 'InstantSpawnLive')
MAP_FIELDS = ('DormantAttachments',)

F = '|'.join(FIELDS)
WRITES = [
    re.compile(r'(\w+)->(' + F + r')\b\s*=(?!=)(?!\s*nullptr\b)'),
    re.compile(r'(\w+)->(' + F + r')\b\s*\.\s*(?:push_back|emplace_back|insert|emplace|resize|assign|swap)\s*\('),
    re.compile(r'(\w+)->(' + F + r')\b\s*\[[^\]]*\]\s*=(?!=)(?!\s*nullptr\b)'),
]
if MAP_FIELDS:
    WRITES.append(re.compile(r'(\w+)->(' + '|'.join(MAP_FIELDS) + r')\b\s*\['))
ALIASES = [
    re.compile(r'auto\s*&\s*(\w+)\s*=\s*(\w+)->(?:' + F + r')\b'),
    re.compile(r'for\s*\(\s*auto\s*&\s*(\w+)\s*:\s*(\w+)->(?:' + F + r')\b'),
]
MARK = re.compile(r'^\s*(\w+)->MarkPointerHolder\(\);\s*$')


def code_lines(text):
    """(line number, code) with // comments stripped, blank lines dropped."""
    for n, line in enumerate(text.splitlines(), 1):
        code = line.split('//', 1)[0].rstrip()
        if code.strip():
            yield n, code


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    findings = []
    checked = 0

    for path in sorted(root.rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue

        lines = list(code_lines(path.read_text(errors='replace')))
        aliases = {}  # alias name -> object it refers into

        for i, (n, code) in enumerate(lines):
            if code.strip().startswith('#'):
                continue

            sites = []
            for rx in WRITES:
                sites += [w.group(1) for w in rx.finditer(code)]
            for alias, obj in aliases.items():
                if re.search(r'(?<![\w>.])' + alias + r'\s*(?:\[[^\]]*\])?\s*=(?!=)(?!\s*nullptr\b)'
                             r'|(?<![\w>.])' + alias + r'\s*\.\s*(?:push_back|emplace_back|insert|emplace|assign|swap)\s*\(', code):
                    sites.append(obj)
            for rx in ALIASES:
                for m in rx.finditer(code):
                    aliases[m.group(1)] = m.group(2)

            for obj in sites:
                checked += 1
                prev = MARK.match(lines[i - 1][1]) if i > 0 else None
                if prev and prev.group(1) == obj:
                    continue
                findings.append((path, n, obj, code.strip()))

    print(f'check_pointer_holders: checked {checked} write(s) to {", ".join(FIELDS)}')
    for path, n, obj, code in findings:
        print(f'  {path}:{n}: no `{obj}->MarkPointerHolder();` on the line before: {code}')

    if findings:
        print(f'check_pointer_holders: {len(findings)} problem(s). An unregistered '
              f'holder keeps a dangling pointer after the object dies.')
        return 1

    print('check_pointer_holders: OK.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
