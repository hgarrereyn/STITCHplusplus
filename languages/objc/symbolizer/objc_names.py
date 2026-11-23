"""Filter for llvm-symbolizer output: GNUstep method symbols as Objective-C method names.

clang names the implementation of -[Class(Category) sel:with:] `_i_Class_Category_sel_with_`
(`_c_` for class methods). Underscores inside class names or selectors are ambiguous: the
selector's leading underscores stay, every other underscore is a colon.
"""
import re
import sys

METHOD = re.compile(r"^_([ic])_([A-Za-z0-9$]+)_([A-Za-z0-9$]*)_(\w*)$")


def objc_name(sym):
    m = METHOD.match(sym)
    if not m:
        return sym
    kind, cls, cat, sel = m.groups()
    lead = len(sel) - len(sel.lstrip("_"))
    sel = sel[:lead] + sel[lead:].replace("_", ":")
    return f"{'-' if kind == 'i' else '+'}[{cls}{f'({cat})' if cat else ''} {sel}]"


if __name__ == "__main__":
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        sys.stdout.write(objc_name(line.rstrip("\n")) + "\n")
        sys.stdout.flush()
