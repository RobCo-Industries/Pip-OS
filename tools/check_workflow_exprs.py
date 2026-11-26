#!/usr/bin/env python3
"""Fail when a workflow line holds a ${{ ... }} expression in an unsafe shape.

Actions pastes the value of ${{ ... }} into a run: script before the shell
reads it, so a tag or a pull request title such as $(id) would run as a
command. Every value must reach a script through env: and the script reads
"$NAME".

This check does not read YAML. It judges every line that holds ${{,
comments included, and fails closed. A line passes only in this shape:
optional indentation, an optional "- ", a key of letters, digits, _ or -,
then ": ", then a value that holds each expression whole, ${{ and }} on
that line. The key must not name code: run, script, shell, args or
entrypoint, in any case. Every other shape fails: no key, as in a script
line; a space before the colon; a quoted key; a flow mapping; an
expression split over two lines; a comment, since a shell comment inside a
script is still filled in and the check cannot tell the two kinds apart.

One structural rule closes the case of a script line that looks like
KEY: value. A line that ends in | or > after a key and a colon, with
optional chomping and indentation indicators, opens a block scalar. Every
following line indented deeper than that key, blank lines included, is
block text, and any ${{ there fails, whatever the key.

What it cannot see: a value that reaches a script through env: and is then
used unquoted or in eval; a reusable workflow or an action in another
repository; values relayed through outputs and used by a later script.

Usage: check_workflow_exprs.py FILE...
Exit 0 when clean, 1 with one line per hit, 2 on a usage error.
"""

import re
import sys

SAFE = re.compile(r"^[ \t]*(?:- )?([A-Za-z0-9_-]+): (.*)$")
BLOCK = re.compile(r"^([ \t]*(?:- )?)[A-Za-z0-9_\"'-][^:]*:[ \t]*[|>][-+0-9]*[ \t]*(?:#.*)?$")
CODE_KEYS = {"run", "script", "shell", "args", "entrypoint"}


def whole_exprs(value):
    """True when every ${{ in value has a }} after it on the same line."""
    pos = 0
    while True:
        start = value.find("${{", pos)
        if start < 0:
            return True
        end = value.find("}}", start + 3)
        if end < 0:
            return False
        pos = end + 2


def findings(text):
    """Yield (line number, reason) for each unsafe line."""
    block_indent = None
    for num, line in enumerate(text.splitlines(), 1):
        indent = len(line) - len(line.lstrip(" \t"))
        if block_indent is not None:
            if not line.strip() or indent > block_indent:
                if "${{" in line:
                    yield num, "${{ inside a block scalar"
                continue
            block_indent = None
        block = BLOCK.match(line)
        if block:
            block_indent = len(block.group(1))
        if "${{" not in line:
            continue
        match = SAFE.match(line)
        if not match:
            yield num, "${{ in a line that is not KEY: value"
        elif match.group(1).lower() in CODE_KEYS:
            yield num, "${{ in a %s: value" % match.group(1)
        elif not whole_exprs(match.group(2)):
            yield num, "${{ not closed on its line"


def main(argv):
    if not argv:
        print("usage: check_workflow_exprs.py FILE...", file=sys.stderr)
        return 2
    bad = 0
    for path in argv:
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        for num, reason in findings(text):
            print("%s:%d: %s; pass the value through env:" % (path, num, reason))
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
