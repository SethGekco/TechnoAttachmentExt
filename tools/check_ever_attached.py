#!/usr/bin/env python3
"""
Flag a non-null write to ParentAttachment that is not IMMEDIATELY preceded by
`<same object>->MarkEverAttached();`.

WHY THIS EXISTS
---------------
The Y-sort wrapper (Hooks.AttachedRender.cpp) runs for every item
LayerClass::AddObject's insert scan passes: thousands of calls per new anim
or unit. It answers "is this techno attached?" from TechnoExt::EverAttached,
a small set, and skips the ExtMap lookup (a hash probe over every techno) for
every techno outside it. That is only right while every techno whose
ParentAttachment is set is in the set.

A missed MarkEverAttached is not slow, it is WRONG: that child sorts at its
own Y instead of over or under its parent (YSortPosition, YSortAdjust are
ignored), and nothing crashes to show it. The compiler cannot see the
omission, so it is checked here.

"Immediately" is literal: the previous line of code must be the mark.

WHAT IT FLAGS
-------------
  X->ParentAttachment = value     (assigning nullptr is a detach and is allowed)

Usage:  check_ever_attached.py [src-dir]
"""
import re
import sys
import pathlib

WRITE = re.compile(r'(\w+)->ParentAttachment\b\s*=(?!=)(?!\s*nullptr\b)')
MARK = re.compile(r'^\s*(\w+)->MarkEverAttached\(\);\s*$')


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
        for i, (n, code) in enumerate(lines):
            for w in WRITE.finditer(code):
                checked += 1
                prev = MARK.match(lines[i - 1][1]) if i > 0 else None
                if prev and prev.group(1) == w.group(1):
                    continue
                findings.append((path, n, w.group(1), code.strip()))

    print(f'check_ever_attached: checked {checked} non-null write(s) to ParentAttachment')
    for path, n, obj, code in findings:
        print(f'  {path}:{n}: no `{obj}->MarkEverAttached();` on the line before: {code}')

    if findings:
        print(f'check_ever_attached: {len(findings)} problem(s). A child outside '
              f'TechnoExt::EverAttached loses its YSortPosition and YSortAdjust.')
        return 1

    print('check_ever_attached: OK.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
