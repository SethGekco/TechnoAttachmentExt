#!/usr/bin/env python3
"""
Flag `this->Parent->` dereferences with no dominating null test.

WHY THIS EXISTS
---------------
AttachmentClass::Parent is nullable AT ANY TIME, and not as an edge case:

    void AttachmentClass::InvalidatePointer(void* ptr)
    {
        AnnounceInvalidPointer(this->Parent, ptr);   // nulls Parent in place
        AnnounceInvalidPointer(this->Child, ptr);
    }

That clears the field and removes NOTHING from the host's ChildAttachments
vector, so a parentless slot stays in the vector that UnlimboAttachments and
AI() walk every frame. The invariant "a slot in ChildAttachments has a live
Parent" is simply not maintained, which means every reader has to check.

AttachmentClass::Unlimbo() guarded only Child and then used Parent three times.
The first use passed it to TechnoExt::GetFLHAbsoluteCoords, which dereferenced
it on line one, and read address 0. See HANDOFF-crash-20261030-203952.md.

This is NOT the same bug as the one AttachmentClass::LiveSet guards. That one is
a live back-pointer naming a destroyed slot; this one is a live slot whose
Parent has been cleared. LiveSet cannot help -- Parent is a TechnoClass*, not an
AttachmentClass* -- so the two need separate checks.

WHAT COUNTS AS A GUARD
----------------------
Any null test on Parent that dominates the dereference: `if (this->Parent)`,
`if (!this->Parent) return`, `this->Parent &&`, `this->Parent ?`, or a test on a
local the Parent was copied into. Dominance is approximated by walking backward
and ignoring guards inside blocks that had already closed -- a guard in a
sibling block does not protect a later use.

KNOWN LIMITS: textual brace tracking, so a brace in a string can mis-scope. It
is tuned to err toward NOISE rather than silence, because the cost here is a
crash and the cost of a false positive is one redundant null check.
"""
import re, sys, pathlib

DEREF = re.compile(r'\bthis->Parent\s*->')
# A null test on Parent, in any of the shapes this codebase uses.
# Two independent conditions rather than one ordered pattern: a conditional
# context anywhere on the line, AND Parent used as a VALUE rather than
# dereferenced. Ordering them tripped on `this->Parent ? this->Parent->X : y`,
# where the '?' that marks the context comes AFTER the test that guards it.
COND = re.compile(r'\bif\s*\(|\breturn\b|\?|&&|\|\|')
VALUE = re.compile(r'!\s*this->Parent\b|\bthis->Parent\s*(?:\)|&&|\|\||\?|,|;)')

# A negative early-out dominates everything after it in the function.
EARLY_OUT = re.compile(r'!\s*this->Parent\b.*?\b(?:return|continue|break|throw)\b', re.S)

def is_guard(line, at_same_depth=False):
    if not (COND.search(line) and VALUE.search(line)):
        return False
    if EARLY_OUT.search(line):
        return True
    # A positive test whose block OPENS AND CLOSES on this one line guards only
    # that inline block, not the statements after it:
    #     if (this->Parent) { this->Parent->A(); }
    #     this->Parent->B();                        // NOT guarded
    if at_same_depth and '{' in line and line.count('{') == line.count('}'):
        return False
    return True

FUNC = re.compile(r'^\w[\w:<>,*&\s]*\b(\w+)::(\w+)\s*\(')

def functions(lines):
    """Yield (name, start, end) for each top-level member definition."""
    i = 0
    while i < len(lines):
        m = FUNC.match(lines[i])
        if m:
            # find the opening brace, then match to its close
            j, depth, started = i, 0, False
            while j < len(lines):
                c = lines[j].split('//')[0]
                depth += c.count('{') - c.count('}')
                if '{' in c:
                    started = True
                if started and depth <= 0:
                    break
                j += 1
            yield f'{m.group(1)}::{m.group(2)}', i, min(j, len(lines) - 1)
            i = j + 1
        else:
            i += 1

def check(path):
    lines = path.read_text(errors='replace').split('\n')
    out = []
    for fname, start, end in functions(lines):
        for n in range(start, end + 1):
            code = lines[n].split('//')[0]
            if not DEREF.search(code):
                continue
            if is_guard(code):               # guarded on its own line
                continue
            # Walk backward to the function head, skipping closed sibling blocks.
            depth, found = 0, False
            for k in range(n - 1, start - 1, -1):
                c = lines[k].split('//')[0]
                if depth <= 0 and is_guard(c, at_same_depth=(depth == 0)):
                    found = True
                    break
                depth += c.count('}') - c.count('{')
            if not found:
                out.append((n + 1, fname, lines[n].strip()[:84]))
    return out

def main():
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'src')
    files = sorted(root.rglob('*.cpp'))
    bad = 0
    for f in files:
        for ln, fn, src in check(f):
            print(f'{f}:{ln}: unguarded this->Parent-> in {fn}()\n    {src}')
            bad += 1
    print(f'check_parent_guard: scanned {len(files)} files')
    if bad:
        print(f'check_parent_guard: FAILED -- {bad} unguarded this->Parent-> deref(s).')
        print('  Parent is nulled in place by InvalidatePointer; the slot stays in the vector.')
        return 1
    print('check_parent_guard: OK.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
