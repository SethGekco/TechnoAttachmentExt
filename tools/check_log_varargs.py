#!/usr/bin/env python3
"""
Flag class types handed to a varargs %s.

WHY THIS EXISTS
---------------
A one-character change from `->ID` to `->Name` crashed the game on launch.

    Debug::Log("... %s ...", this->GetType()->Name);

`AbstractTypeClass::ID` is `char[0x18]` -- an array, which decays to a pointer
and is fine. `Enumerable<T>::Name` is `PhobosFixedString<32>`, a CLASS. It does
define `operator const T*()`, but **implicit conversions do not apply to
varargs**: the compiler pushes all 32 bytes of the inline character array and
`%s` reads the first four as a pointer. It also shifts every later argument.

The crash signature is distinctive and worth knowing: the faulting address is
made of printable ASCII (we saw 0x6E6F7244 = "Dron", the first four characters of
the attachment's name) with `type_case_s` / `common_vsprintf` on the stack.

A format-vs-argument COUNT check cannot catch this -- the counts are correct. It
is purely a type mismatch, and MSVC will not warn without SAL annotation on the
format parameter, which these logging helpers do not carry.

Usage:  check_log_varargs.py [src-dir]
"""
import re
import sys
import pathlib

# Argument expressions that are CLASS types, not char arrays or pointers.
# Each entry: (regex, what it actually is, how to pass it safely).
SUSPECT = [
    (re.compile(r'(?<![\w.>])(?:->|\.)Name\b(?!\s*\.)(?!\s*\))'),
     'Enumerable<T>::Name is PhobosFixedString<N>, a class',
     'static_cast<const char*>(x.Name)'),
    (re.compile(r'(?<![\w.>])(?:->|\.)ParentCountry\b'),
     'HouseTypeClass::ParentCountry is a FixedString, a class',
     'static_cast<const char*>(x->ParentCountry)'),
    (re.compile(r'\bstd::string\s+\w+\s*\)'),
     'std::string is a class',
     'x.c_str()'),
]

# Varargs logging entry points in this codebase.
LOG_CALL = re.compile(r'\b(Debug::Log|Debug::INIParseFailed|_snprintf_s|sprintf_s|printf)\s*\(')


def call_text(text, start):
    """Return the full parenthesised argument list beginning at `start`."""
    depth, i, n = 0, start, len(text)
    while i < n:
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return text[start:start + 2000]


def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    findings = []
    scanned = 0

    for path in sorted(root.rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        text = path.read_text(errors='replace')
        scanned += 1

        for m in LOG_CALL.finditer(text):
            args = call_text(text, m.end() - 1)
            # Only a %s can misread a pushed struct as a pointer.
            if '%s' not in args:
                continue
            # Anything already cast or .c_str()'d is fine.
            for rx, what, fix in SUSPECT:
                for hit in rx.finditer(args):
                    window = args[max(0, hit.start() - 40):hit.end() + 4]
                    if 'static_cast<const char*>' in window or 'c_str' in window:
                        continue
                    line = text[:m.start()].count('\n') + 1
                    findings.append((path, line, what, fix))

    print(f'check_log_varargs: scanned {scanned} files')
    for path, line, what, fix in findings:
        print(f'  {path}:{line}: class type passed to a varargs %s -- {what}. Use {fix}')

    if findings:
        print(f'check_log_varargs: {len(findings)} problem(s).')
        return 1

    print('check_log_varargs: OK.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
