#!/usr/bin/env python3
"""Embed runtime_template.c as a C string literal in runtime.c.

The Dino compiler emits this string at the top of every generated program.
Keeping the runtime as a normal .c file makes it readable and editable; run
this script after changing it:

    python3 src/codegen/gen_runtime.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, "runtime_template.c")
OUTPUT = os.path.join(HERE, "runtime.c")


def c_escape(line: str) -> str:
    return line.replace("\\", "\\\\").replace('"', '\\"')


def main() -> None:
    with open(TEMPLATE, "r", encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    # A trailing newline in the template produces one final empty element;
    # drop it so the generated string ends with exactly one newline.
    if lines and lines[-1] == "":
        lines.pop()

    parts = ['/* Generated from runtime_template.c by gen_runtime.py. Do not edit directly. */',
             '#include "runtime.h"',
             '',
             'const char *DINO_RUNTIME_C =']
    for line in lines:
        parts.append('    "%s\\n"' % c_escape(line))
    parts.append("    ;")
    parts.append("")

    with open(OUTPUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(parts))
    print("wrote %s (%d lines)" % (OUTPUT, len(lines)))


if __name__ == "__main__":
    main()
