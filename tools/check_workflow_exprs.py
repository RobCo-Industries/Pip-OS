#!/usr/bin/env python3
"""Fail when a workflow run: script holds any ${{ ... }} expression.

Actions replaces ${{ ... }} in a run: script before the shell reads it, so
a tag, a branch name or a pull request title such as $(id) would run as a
command. The rule does not chase forms: a run: script holds no ${{ at all.
Every value reaches a script through env: and the script reads "$NAME".

The host Python has no YAML parser, so this is a careful text parser. It
checks each run: value as one whole text, not line by line, so an
expression split over two lines of a folded block is still seen.

Forms it reads:
- a block mapping key run: at any depth, after "- " or not;
- a plain, single-quoted or double-quoted scalar that starts on the key's
  line, with any continuation lines indented deeper than the key;
- a block scalar, | or >, with any chomping indicator and indentation
  indicator, then every line indented deeper than the key;
- run: with no value followed by a mapping, as in defaults.run, which
  holds shell settings and no script: it is skipped.

Forms it refuses, with a line that says so, so the check fails:
- a flow mapping that holds run, such as - {run: echo hi};
- a quoted key, "run": or 'run':;
- an anchor, an alias or a merge key anywhere outside a run: script;
- an anchor or alias as a run: value.

What Actions does with comments: a YAML comment is not part of any value,
so ${{ in a comment line or after a plain value is ignored here. A shell
comment inside a run: script is script text, and Actions interpolates it
like the rest, so ${{ there fails.

What it still cannot see: a value passed through env: and then used
unquoted, or inside eval or bash -c; a reusable workflow or an action in
another repository; inputs and outputs relayed between jobs and then used
safely or not by the next script.

Usage: check_workflow_exprs.py FILE...
Exit 0 when clean, 1 with one line per hit, 2 on a usage error.
"""

import re
import sys

RUN_KEY = re.compile(r"^(\s*)(-\s+)?run:(?:\s+(.*))?$")
QUOTED_KEY = re.compile(r"^\s*(?:-\s+)?[\"']run[\"']\s*:")
FLOW_RUN = re.compile(r"[{,]\s*[\"']?run[\"']?\s*:")
ANCHOR = re.compile(r"(?:^\s*-\s+|:\s+|^\s*)[&*][^\s]|<<\s*:")
BLOCK = re.compile(r"^[|>](?:[+-]?[1-9]?|[1-9]?[+-]?)\s*(?:#.*)?$")
MAP_LINE = re.compile(r"^[\w.-]+:(?:\s|$)")
EXPR = re.compile(r"\$\{\{.*?(?:\}\}|$)", re.S)


def indent_of(line):
    return len(line) - len(line.lstrip(" "))


def strip_comment(text):
    """Drop a YAML comment from a plain scalar line: # after a space."""
    m = re.search(r"(^|\s)#", text)
    return text[: m.start()] if m else text


def findings(text):
    """Yield (line number, message) for each hit or refused form."""
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            i += 1
            continue
        code = strip_comment(line)
        if QUOTED_KEY.match(line):
            yield i + 1, "refused: quoted run key, write run: plain"
            i += 1
            continue
        m = RUN_KEY.match(line)
        if not m:
            if FLOW_RUN.search(code):
                yield i + 1, "refused: run inside a flow mapping, use block style"
            elif ANCHOR.search(code):
                yield i + 1, "refused: YAML anchor, alias or merge key"
            i += 1
            continue
        key_indent = len(m.group(1))
        rest = (m.group(3) or "").rstrip()
        j = i + 1
        body = []
        while j < len(lines):
            nxt = lines[j]
            if nxt.strip() and indent_of(nxt) <= key_indent:
                break
            body.append(nxt)
            j += 1
        while body and not body[-1].strip():
            body.pop()
        if rest[:1] in ("&", "*"):
            yield i + 1, "refused: anchor or alias as a run: value"
            i = j
            continue
        if BLOCK.match(rest):
            script = "\n".join(body)
        elif rest[:1] in ("'", '"'):
            script = "\n".join([rest] + body)
        elif rest:
            script = "\n".join(strip_comment(t) for t in [rest] + body)
        else:
            content = [b for b in body if b.strip() and not b.strip().startswith("#")]
            if content and MAP_LINE.match(content[0].strip()):
                # defaults.run: a mapping of shell settings, not a script.
                i += 1
                continue
            script = "\n".join(strip_comment(t) for t in body)
        for expr in EXPR.findall(script):
            shown = " ".join(expr.split())
            yield i + 1, f"run: script holds {shown}, pass it through env:"
        i = j


def main(argv):
    if len(argv) < 2:
        print("usage: check_workflow_exprs.py FILE...", file=sys.stderr)
        return 2
    bad = 0
    for path in argv[1:]:
        with open(path, encoding="utf-8") as f:
            for n, msg in findings(f.read()):
                print(f"{path}:{n}: {msg}")
                bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
