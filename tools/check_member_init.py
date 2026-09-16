#!/usr/bin/env python3
"""
Flag Phobos-wrapper members that are declared but never initialised in the
constructor's init list.

WHY THIS EXISTS
---------------
Twice now, a scripted edit that inserted a new member did NOT insert the matching
constructor initialiser, because the anchor text it searched for did not match.
str.replace fails silently -- it returns the string unchanged and nothing
complains.

The second time shipped a real bug: five new Spins.Orbit.* fields landed with no
initialisers, so Valueable<int> default-constructed XScale/YScale to 0 instead of
100. The orbit code then did `flh.X = (flh.X * 0) / 100`, which zeroed the offset
-- attachments stopped orbiting AND stopped honouring FLH at all. The compiler is
happy, the value is merely wrong, and it only shows in game.

This codebase's convention is that every Valueable/Nullable/ValueableVector
member gets an explicit initialiser. That convention is what makes the omission
detectable, so it is worth enforcing mechanically rather than by eye.

Members with an in-class default (`int MinDwell = 15;`) are exempt -- they are
already initialised.

Usage:  check_member_init.py [src-dir]
"""
import re
import sys
import pathlib

# The wrapper templates whose defaults are easy to get silently wrong.
MEMBER = re.compile(
    r'^\s*(?:Valueable|ValueableVector|ValueableIdx|Nullable|NullableIdx)\s*<[^;=]*>\s+'
    r'(\w+)\s*;', re.M)

# `, Name {` or `: Name {` in a constructor init list.
INIT = re.compile(r'[,:]\s*(\w+)\s*[{(]')

CLASS = re.compile(r'\b(?:class|struct)\s+(\w+)[^;{]*\{', re.M)


# A constructor init list: `) : Member { ... }` -- the `:` after the parameter
# list. This is what distinguishes a class that OPTED IN to explicit member
# initialisation from an aggregate like AttachmentDataEntry, which has no
# constructor and is initialised at its use site instead.
CTOR_INIT = re.compile(r'\)\s*:\s*(?:\w+(?:<[^>]*>)?\s*[{(])')


def class_bodies(text):
    """
    Yield (name, own_body) for each class/struct, innermost first, with nested
    class bodies REMOVED from the enclosing one.

    Without that removal a nested ExtData's members get attributed to the outer
    TechnoTypeExt, which has no init list of its own -- 152 false positives on
    this codebase, and a checker nobody can run is worse than no checker.
    """
    spans = []
    for m in CLASS.finditer(text):
        start = text.index('{', m.end() - 1)
        depth, i, n = 0, start, len(text)
        while i < n:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    spans.append((m.group(1), start, i))
                    break
            i += 1

    for name, start, end in spans:
        own = []
        cursor = start
        for _, ns, ne in spans:
            if ns > start and ne < end:  # a nested class: cut it out
                own.append(text[cursor:ns])
                cursor = ne
        own.append(text[cursor:end])
        yield name, ''.join(own)


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    findings = []
    checked = 0

    for path in sorted(root.rglob('*.h')):
        text = path.read_text(errors='replace')

        for cname, body in class_bodies(text):
            declared = MEMBER.findall(body)
            if not declared:
                continue

            # Only enforce on classes that HAVE a constructor init list. An
            # aggregate with no constructor (AttachmentDataEntry) is initialised
            # at its use site, and the designated-initialiser check covers that.
            if not CTOR_INIT.search(body):
                continue

            initialised = set(INIT.findall(body))

            checked += len(declared)
            for name in declared:
                if name in initialised:
                    continue
                # Exempt members carrying an in-class default.
                if re.search(r'\b' + re.escape(name) + r'\s*=\s*[^;]+;', body):
                    continue
                findings.append((path, cname, name))

    print(f'check_member_init: checked {checked} wrapper member(s)')
    for path, cname, name in findings:
        print(f'  {path}: {cname}::{name} declared but has no constructor '
              f'initialiser -- it will default-construct, which is usually the '
              f'wrong value (0 for a percentage, etc.)')

    if findings:
        print(f'check_member_init: {len(findings)} problem(s).')
        return 1

    print('check_member_init: OK.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
