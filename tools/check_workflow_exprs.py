#!/usr/bin/env python3
"""Fail when a workflow line holds a ${{ ... }} expression in an unsafe shape.

Actions pastes the value of ${{ ... }} into a run: script before the shell
reads it, so a tag or a pull request title such as $(id) would run as a
command. Every value must reach a script through env: and the script reads
"$NAME".

The guard catches an honest slip by a person or a tool that writes
ordinary workflow YAML. It is not a defence against YAML made unusual on
purpose: whoever writes that can change the workflow anyway.

This check does not read YAML. It judges every line that holds ${{,
comments included, and fails closed. A line passes only in this shape:
optional indentation, an optional "- ", a key of letters, digits, _ or -,
then ": ", then a value that holds each expression whole, ${{ and }} on
that line. The key must not name code: run, script, shell, args or
entrypoint, in any case. The value must not start with [ or {, and with
the expressions cut out it holds no " and no '. The nearest line above
that is indented less, counting a list item's key from after its "- ",
must end in a colon and hold no value, so the line is not the tail of a
multi-line value. Every other shape fails, a comment included, since a
shell comment inside a script is still filled in.

A line that ends in | or > after a key and a colon opens a block scalar,
whatever stands between the colon and the indicator. Every following line
indented deeper than that key, blank lines included, is block text, and
any ${{ there fails. An anchor, an alias or a merge key on a structure
line fails the whole file with one line: anchors and aliases are not read.
A structure line is any line that is not block text. The token counts only
where YAML reads one: at the start of a value, right after "KEY: " or "- ",
as in "run: &script |", "uses: *step" or "<<: *base". A * or & further
inside a value, as in "run: ls *.zip", or in block text is not a token.

What stays unseen: a value that reaches a script through env: and is then
used unquoted or in eval; a reusable workflow or an action in another
repository; values relayed through outputs and used by a later script; a
multi-line quoted value whose inner lines look like a mapping, which is read
as structure; and any YAML form made to look like the safe shape on purpose.

Usage: check_workflow_exprs.py FILE...
Exit 0 when clean, 1 with one line per hit, 2 on a usage error.
"""

import re
import sys

SAFE = re.compile(r"^[ \t]*(?:- )?([A-Za-z0-9_-]+): (.*)$")
BLOCK = re.compile(r"^([ \t]*(?:- )?)[A-Za-z0-9_\"'-][^:]*:(?:[ \t]+\S+)*?[ \t]*[|>][-+0-9]*[ \t]*(?:#.*)?$")
ANCHOR = re.compile(
    r"^[ \t]*(?:- )*(?:(?:[^\s#:][^:#]*)?:[ \t]+)?(?:![^\s]*[ \t]+)?[&*][A-Za-z0-9_-]"
    r"|^[ \t]*(?:- )*<<[ \t]*:"
)
EXPR = re.compile(r"\$\{\{.*?\}\}")
LEAD = re.compile(r"^[ \t]*(?:- )*")
OPENER = re.compile(r"^[^#]*:[ \t]*(?:#.*)?$")
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


def key_indent(line):
    """Column of the key, after any list item "- "."""
    return len(LEAD.match(line).group(0))


def under_opener(lines, index):
    """True when the nearest less-indented line above is "KEY:" alone."""
    own = key_indent(lines[index])
    for line in reversed(lines[:index]):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if key_indent(line) < own:
            return bool(OPENER.match(line))
    return True


def block_text_lines(all_lines):
    """Line numbers of block scalar text."""
    found = set()
    block_indent = None
    for num, line in enumerate(all_lines, 1):
        indent = len(line) - len(line.lstrip(" \t"))
        if block_indent is not None:
            if not line.strip() or indent > block_indent:
                found.add(num)
                continue
            block_indent = None
        block = BLOCK.match(line)
        if block:
            block_indent = len(block.group(1))
    return found


def findings(text):
    """Yield (line number, reason) for each unsafe line."""
    all_lines = text.splitlines()
    text_lines = block_text_lines(all_lines)
    for num, line in enumerate(all_lines, 1):
        if num not in text_lines and ANCHOR.match(line):
            yield num, "anchors and aliases are not read"
            return
    block_indent = None
    for num, line in enumerate(all_lines, 1):
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
        elif match.group(2).startswith(("[", "{")):
            yield num, "${{ in a flow collection"
        elif re.search("[\"']", EXPR.sub("", match.group(2))):
            yield num, "${{ in a line with quotes outside the expression"
        elif not under_opener(all_lines, num - 1):
            yield num, "${{ in a continuation line"


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
