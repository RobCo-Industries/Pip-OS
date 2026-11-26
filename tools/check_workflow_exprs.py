#!/usr/bin/env python3
"""Fail when a workflow run: block pastes outside text into its script.

Actions replaces ${{ ... }} in a run: block before the shell reads it, so
a tag or branch name such as pipos-$(id) would run as a command. Such a
value must reach the script through env: and be expanded as "$NAME".

This check reads the files as text, line by line. It finds a run: key and
the lines indented under it. It flags ${{ in those lines when the
expression names github.ref_name, github.head_ref or github.event.

What it cannot see: a run: written as a flow mapping ({run: ...}), a YAML
anchor or alias, a composite action in another file, an unsafe value that
reaches a run: block through another name, such as a step output, an env
value set from github.event and pasted back with ${{ env.X }}, or
inputs.* of a reusable workflow. It does not parse YAML.

Usage: check_workflow_exprs.py FILE...
Exit 0 when clean, 1 with one line per hit, 2 on a usage error.
"""

import re
import sys

RUN_KEY = re.compile(r"^(\s*(?:-\s+)?)run:\s*(.*)$")
EXPR = re.compile(r"\$\{\{(.*?)\}\}")
UNSAFE = re.compile(r"github\.(?:ref_name|head_ref|event\.)")


def hits(text):
    """Yield (line number, line) for each unsafe expression in a run: block."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        m = RUN_KEY.match(lines[i])
        if not m:
            i += 1
            continue
        indent = len(m.group(1))
        block = [(i, m.group(2))]
        j = i + 1
        while j < len(lines):
            line = lines[j]
            if line.strip() and len(line) - len(line.lstrip()) <= indent:
                break
            block.append((j, line))
            j += 1
        for n, line in block:
            for expr in EXPR.findall(line):
                if UNSAFE.search(expr):
                    yield n + 1, line.strip()
                    break
        i = j


def main(argv):
    if len(argv) < 2:
        print("usage: check_workflow_exprs.py FILE...", file=sys.stderr)
        return 2
    bad = 0
    for path in argv[1:]:
        with open(path, encoding="utf-8") as f:
            for n, line in hits(f.read()):
                print(f"{path}:{n}: run: pastes an outside value, use env: {line}")
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
