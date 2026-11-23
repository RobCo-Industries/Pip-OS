#!/usr/bin/env python3
"""Stages and checks the card tree of make sdimage and make bootdir.

    card.py check-tapes [--rom ROM] [TAPE...]
    card.py stage-card-dir STAGE CARD_DIR
    card.py check-stage STAGE [CARD_DIR]
    card.py sync-bootdir STAGE DIR [--clean]

Standard library only. It runs on Python 3.9, the oldest Python the build
meets (macOS /usr/bin/python3; the CI container has 3.12).

FAT ignores case, so every name comparison below folds case: fold() maps
each character to its upper case (NFC first), the way FAT matches names.

check-tapes
  T1. Two TAPE files whose names fold to the same name: error.
  T2. A TAPE name that does not end in .HTP: error (holo_enumerate would
      never list it).
  T3. A NAME.HTP that is not a plain 8.3 name: warning, on stderr.
  T4. A TAPE file or the ROM that is a symbolic link, a device, a FIFO or
      a socket: error. A file that does not exist yet is left to make.
  Errors go to stdout on one line, joined by "; ", for the Makefile's
  $(error). The exit status is always 0.

stage-card-dir
  C1. CARD_DIR must be a folder. CARD_DIR itself may be a link to a
      folder; nothing inside it may be a link, a device, a FIFO or a
      socket.
  C2. A CARD_DIR path that folds to a staged path, where either one is a
      file: "clashes with the standard ...". Nothing is copied.
  C3. A CARD_DIR folder that folds to a staged folder spelled otherwise:
      "differs only in case from the standard ...". Nothing is copied.
  C4. A CARD_DIR path at the card root whose name folds to BOOTDIR.LST:
      error, see R1. Nothing is copied.
  C5. Otherwise the tree is copied into the stage; folders merge.

check-stage
  S1. The stage holds regular files and folders only.
  S2. No two staged paths fold to the same path. Each pair is listed once,
      only the outermost clashing component. Error.
  S3. A staged path at the card root whose name folds to BOOTDIR.LST:
      error, see R1.
  S4. A CARD_DIR path with a component that is not 8.3 (case ignored):
      warning, on stderr.

Reserved names
  R1. BOOTDIR.LST at the card root is reserved for the manifest of
      sync-bootdir (P3), so sdimage, run and bootdir all refuse a staged
      file or folder there. Only the root: sync-bootdir writes and reads
      the manifest only there, so DATA/BOOTDIR.LST is an ordinary file.
  R2. PIPBOY/PIPOS.CFG is a standard file of the stage, so a CARD_DIR
      copy of it stops at C2 in sdimage and bootdir alike.
  R3. The temporary names of A1 (.bootdir-XXXXXXXX) are not reserved:
      mkstemp creates each one with O_EXCL, so it never takes the name
      of a file already there, staged or not, and A1 removes it on error.

sync-bootdir: plan, check, apply. DIR belongs to the person.
  Plan
  P1. Folders to create: every staged folder. Files to write: every staged
      file except BOOTDIR.LST and PIPBOY/PIPOS.CFG.
  P2. PIPBOY/PIPOS.CFG is written only when DIR has nothing at that path.
      It is never in the manifest and never removed.
  P3. DIR/BOOTDIR.LST, the manifest, names the files the last run wrote.
  P4. Each entry of DIR that is not a folder, that this run does not
      write, is stale (the manifest names it), unknown (at a managed path:
      firmware, PIPOS.IMG, config.txt, PIPBOY/ROM1.ROM, PIPBOY/HOLO/*.HTP)
      or other. With --clean, stale entries are removed.
  P5. A staged BOOTDIR.LST stops the run (R1), before DIR is read.
  P6. DIR may be a link to a folder: it is resolved once and the real
      folder is used. A DIR that exists and is not a folder stops the run.
  Check: reads DIR only; it lists every problem, then stops with
  "Nothing was changed." when there is one.
  K1. A symbolic link at any path the plan touches, or at any folder on
      the way to one inside DIR: planned files, planned folders, the
      manifest, PIPBOY/PIPOS.CFG when written, stale files to remove.
      Links are never followed (lstat everywhere).
  K2. A manifest line that is not a clean relative path (absolute, with
      an empty, "." or ".." component, a trailing slash, a NUL). Blank
      lines are skipped and a CR before the line end is dropped.
  K3. A path in DIR spelled with other case than the path this run
      writes (the manifest included), even with --clean. Only the
      outermost one is listed.
  K4. A file where the plan needs a folder (unless --clean removes it),
      a folder where it writes a file, or a special file where it writes
      a file, at any depth, even with --clean.
  K5. Unknown files (P4), even with --clean.
  K6. Stale files without --clean.
  K7. Not writable (os.access): DIR, each existing folder the plan
      creates in, writes into or removes from, each planned folder that
      exists, each existing file it replaces or removes. When DIR does
      not exist, its nearest existing parent.
  K8. A folder of DIR that cannot be read.
  Apply: only when the check found nothing.
  A1. Remove stale files (--clean), create folders, write each file to a
      temporary name in its folder and rename it into place, then the
      manifest the same way. A crash leaves each file old or new.
  A2. An error during apply says DIR is partly updated.
  Other entries of DIR are left alone and named once.
"""

from __future__ import annotations

import os
import re
import shutil
import stat
import sys
import tempfile
import unicodedata

MANIFEST = "BOOTDIR.LST"
CFG = "PIPBOY/PIPOS.CFG"
NAME83 = r"[A-Z0-9!#$%&'()@^_`{}~-]"
SHORT = re.compile(rf"^{NAME83}{{1,8}}(\.{NAME83}{{1,3}})?$")
SHORT_HTP = re.compile(rf"^{NAME83}{{1,8}}\.HTP$")
MANAGED = re.compile(r"^(BOOTCODE\.BIN|START\.ELF|FIXUP\.DAT|PIPOS\.IMG|CONFIG\.TXT"
                     r"|PIPBOY/ROM1\.ROM|PIPBOY/HOLO/[^/]*\.HTP)$")


class Stop(Exception):
    pass


def fold(s):
    """The FAT view of a name: upper case, one character for one."""
    out = []
    for c in unicodedata.normalize("NFC", s):
        u = c.upper()
        out.append(u if len(u) == 1 else c)
    return "".join(out)


def kind_of(mode):
    if stat.S_ISLNK(mode):
        return "link"
    if stat.S_ISDIR(mode):
        return "folder"
    if stat.S_ISREG(mode):
        return "file"
    if stat.S_ISCHR(mode) or stat.S_ISBLK(mode):
        return "device"
    if stat.S_ISFIFO(mode):
        return "FIFO"
    if stat.S_ISSOCK(mode):
        return "socket"
    return "special file"


def lkind(path):
    """kind_of(lstat(path)), or None when nothing is there."""
    try:
        return kind_of(os.lstat(path).st_mode)
    except (FileNotFoundError, NotADirectoryError):
        return None


def dkind(root, rel):
    """lkind of root/rel, or None when a folder on the way is missing, a
    file or a link: what is behind a link is never looked at."""
    comps = rel.split("/")
    for i in range(1, len(comps)):
        if lkind(os.path.join(root, *comps[:i])) != "folder":
            return None
    return lkind(os.path.join(root, *comps))


def walk(root):
    """{relative path: kind} of everything below root, links not followed.

    Returns (entries, unreadable folders)."""
    entries, bad = {}, []

    def go(rel):
        path = os.path.join(root, rel) if rel else root
        try:
            with os.scandir(path) as it:
                items = sorted(it, key=lambda e: e.name)
        except OSError:
            bad.append(rel or ".")
            return
        for e in items:
            r = f"{rel}/{e.name}" if rel else e.name
            k = kind_of(e.stat(follow_symlinks=False).st_mode)
            entries[r] = k
            if k == "folder":
                go(r)

    go("")
    return entries, bad


def special(entries):
    return sorted(p for p, k in entries.items() if k not in ("file", "folder"))


def reserved(entries, prog):
    """Stops on a path at the card root whose name folds to MANIFEST (R1)."""
    bad = [p for p in sorted(entries) if "/" not in p and fold(p) == fold(MANIFEST)]
    if bad:
        for p in bad:
            print(f"{prog}: {p}: the name {MANIFEST} is reserved for the boot folder manifest "
                  "that make bootdir writes at the card root", file=sys.stderr)
        raise Stop(f"rename or move {', '.join(bad)} in CARD_DIR. Nothing was written.")


def is_dir(path):
    """A folder, or a link to one (for a root named on the command line)."""
    return os.path.isdir(path)


# ---- check-tapes -------------------------------------------------------------

def check_tapes(args):
    rom = None
    if args[:1] == ["--rom"]:
        if len(args) < 2:
            raise Stop("usage: card.py check-tapes [--rom ROM] [TAPE...]")
        rom, args = args[1], args[2:]
    if args[:1] == ["--"]:
        args = args[1:]
    errors, seen = [], {}
    for what, path in ([("ROM", rom)] if rom else []) + [("TAPE", t) for t in args]:
        k = lkind(path)
        if k not in (None, "file", "folder"):
            errors.append(f"{what} file {path} is a {'symbolic link' if k == 'link' else k}: "
                          "use a regular file")
    for t in args:
        n = fold(os.path.basename(t))
        if n in seen:
            errors.append(f"TAPE names {seen[n]} and {t}, the same file on a FAT card, which ignores case")
        else:
            seen[n] = t
        if not n.endswith(".HTP"):
            errors.append(f"TAPE file {t} does not end in .HTP, so holo_enumerate would never list it")
        elif not SHORT_HTP.match(n):
            print(f"check_tapes: warning: TAPE file {t} is not a plain 8.3 NAME.HTP: "
                  "ROMs see it under a FAT short name such as XXXXXX~1.HTP", file=sys.stderr)
    if errors:
        sys.stdout.write("; ".join(errors))
    return 0


# ---- stage-card-dir ------------------------------------------------------------

def source_tree(root, label, prog):
    """The entries of a source folder; links and special files stop."""
    entries, bad = walk(root)
    msgs = [f"  {p} (cannot read this folder)" for p in bad]
    msgs += [f"  {p} (a {'symbolic link' if entries[p] == 'link' else entries[p]})"
             for p in special(entries)]
    if msgs:
        print(f"{prog}: {label} holds paths that are not regular files or folders. "
              "FAT has none, and a link could bring a file from outside the project:", file=sys.stderr)
        print("\n".join(msgs), file=sys.stderr)
        raise Stop(f"replace them with regular files or folders. Nothing was written.")
    return entries


def stage_card_dir(args):
    if len(args) != 2:
        raise Stop("usage: card.py stage-card-dir <stage> <card-dir>")
    stage, src = args
    if not is_dir(stage):
        raise Stop(f"{stage} is not a folder")
    if not is_dir(src):
        raise Stop(f"CARD_DIR={src} is not a folder")
    have, _ = walk(stage)
    st = {fold(p): (p, k) for p, k in have.items()}
    entries = source_tree(src, f"CARD_DIR={src}", "card_dir")
    clashes = []
    for p, k in sorted(entries.items()):
        f = fold(p)
        if f not in st:
            continue
        sp, sk = st[f]
        if sk != "folder" or k != "folder":
            clashes.append(f"{p} clashes with the standard {sp}")
        elif sp != p:
            clashes.append(f"{p} differs only in case from the standard {sp}")
    if clashes:
        for c in clashes:
            print(f"card_dir: {c}", file=sys.stderr)
        raise Stop(f"CARD_DIR={src} would replace a standard card file: nothing copied")
    reserved(entries, "card_dir")
    for p, k in sorted(entries.items()):
        if k == "folder":
            os.makedirs(os.path.join(stage, p), exist_ok=True)
        else:
            shutil.copyfile(os.path.join(src, p), os.path.join(stage, p), follow_symlinks=False)
            shutil.copymode(os.path.join(src, p), os.path.join(stage, p), follow_symlinks=False)
    return 0


# ---- check-stage -----------------------------------------------------------------

def check_stage(args):
    if len(args) not in (1, 2):
        raise Stop("usage: card.py check-stage <stage> [<card-dir>]")
    stage = args[0]
    src = args[1] if len(args) == 2 else ""
    if not is_dir(stage):
        raise Stop(f"{stage} is not a folder")
    if src and not is_dir(src):
        raise Stop(f"CARD_DIR={src} is not a folder")
    entries = source_tree(stage, "the staged card", "check_stage")
    reserved(entries, "check_stage")
    seen, bad, clashes = {}, [], []
    for p in sorted(entries):
        if any(p.startswith(b + "/") for b in bad):
            continue
        f = fold(p)
        if f in seen:
            bad.append(p)
            q = seen[f]
            clashes.append(f"  {entries[q]} {q} and {entries[p]} {p}")
        else:
            seen[f] = p
    if clashes:
        print("check_stage: these paths differ only in case, and FAT ignores case, "
              "so the card would keep only one of each pair:", file=sys.stderr)
        print("\n".join(clashes), file=sys.stderr)
        raise Stop("rename them in CARD_DIR. Nothing was written.")
    if src:
        for p in sorted(source_tree(src, f"CARD_DIR={src}", "check_stage")):
            if not all(SHORT.match(fold(c)) for c in p.split("/")):
                print(f"check_stage: warning: CARD_DIR path {p} is not made of 8.3 names: FAT stores it "
                      "under an alias such as XXXXXX~1, and a ROM can only open it by that alias",
                      file=sys.stderr)
    return 0


# ---- sync-bootdir ------------------------------------------------------------------

def parse_manifest(data):
    """(names, problems) from the bytes of BOOTDIR.LST."""
    names, problems = [], []
    text = data.decode("utf-8", "surrogateescape")
    for n, line in enumerate(text.split("\n"), 1):
        if line.endswith("\r"):
            line = line[:-1]
        if not line.strip():
            continue
        parts = line.split("/")
        if "\0" in line or line.startswith("/") or any(c in ("", ".", "..") for c in parts):
            problems.append(f"  line {n}: {line!r}")
        else:
            names.append(line)
    return names, problems


def parent(p):
    return p.rsplit("/", 1)[0] if "/" in p else "."


def sync_bootdir(args):
    clean = "--clean" in args
    args = [a for a in args if a != "--clean"]
    if len(args) != 2:
        raise Stop("usage: card.py sync-bootdir <stage> <dir> [--clean]")
    stage, given = args
    if not is_dir(stage):
        raise Stop(f"{stage} is not a folder")
    root = given
    if os.path.islink(given):
        root = os.path.realpath(given)
        if not os.path.isdir(root):
            raise Stop(f"DIR={given} is a symbolic link that does not lead to a folder")
        print(f"bootdir: DIR={given} is a link, writing into {root}")
    elif os.path.lexists(given) and not os.path.isdir(given):
        raise Stop(f"DIR={given} exists and is not a folder")
    exists = os.path.isdir(root)

    def at(p):
        return root if p == "." else os.path.join(root, p)

    def kind(p):
        return "folder" if p == "." else dkind(root, p)

    # ---- plan
    staged = source_tree(stage, "the staged card", "bootdir")
    reserved(staged, "bootdir")
    special_names = {fold(MANIFEST), fold(CFG)}
    newdirs = sorted(p for p, k in staged.items() if k == "folder")
    new = sorted(p for p, k in staged.items() if k == "file" and fold(p) not in special_names)
    newf = {fold(p) for p in new}
    cfg = staged.get(CFG) == "file" and not (exists and kind(CFG) is not None)

    have, unreadable, old, bad_lines = {}, [], [], []
    if exists:
        have, unreadable = walk(root)
        if kind(MANIFEST) == "file":
            with open(at(MANIFEST), "rb") as f:
                old, bad_lines = parse_manifest(f.read())
    oldf = {fold(p) for p in old}
    stale, unknown, other = [], [], []
    for p, k in sorted(have.items()):
        f = fold(p)
        if k == "folder" or f in newf or f in special_names:
            continue
        if f in oldf:
            stale.append(p)
        elif MANAGED.match(f):
            unknown.append(p)
        else:
            other.append(p)
    remove = stale if clean else []

    # ---- check
    report = []

    def section(head, lines, tail=None):
        if lines:
            report.append(f"bootdir: {head}")
            report.extend(lines)
            if tail:
                report.append(f"bootdir: {tail}")

    if exists:
        touched = newdirs + new + [MANIFEST] + ([CFG] if cfg else []) + remove
        links = set()
        for p in touched:
            comps = p.split("/")
            for i in range(1, len(comps) + 1):
                sub = "/".join(comps[:i])
                k = lkind(at(sub))
                if k == "link":
                    links.add(sub)
                if k != "folder":
                    break
        section(f"{given} holds symbolic links where this run writes or removes files. "
                "bootdir never follows a link in DIR:",
                [f"  {p} (a symbolic link)" for p in sorted(links)],
                "replace them with real files or folders, or use an empty folder.")
        section(f"{given}/{MANIFEST} has lines that are not a plain relative path inside DIR, "
                "so bootdir cannot trust it:", bad_lines,
                f"fix or delete {MANIFEST} yourself.")
        section(f"{given} holds folders that bootdir cannot read:",
                [f"  {p}" for p in unreadable], "fix their permissions, or use an empty folder.")

        want = {fold(p): p for p in list(staged) + [MANIFEST]}
        badcase, case = [], []
        for p in sorted(have):
            if any(p.startswith(b + "/") for b in badcase):
                continue
            w = want.get(fold(p))
            if w is not None and w != p:
                badcase.append(p)
                case.append(f"  {p} (this run writes {w})")
        section(f"{given} holds paths that differ only in case from paths bootdir writes. "
                "FAT ignores case, so they would clash on the card:", case,
                "rename or remove them yourself, or use an empty folder.")

        types, typed = [], []
        for d in newdirs:
            k = kind(d)
            if k in (None, "folder", "link") or d in remove:
                continue
            if not any(d.startswith(t + "/") for t in typed):
                types.append(f"  {d} (a file: this run needs a folder there)")
                typed.append(d)
        for f in new + [MANIFEST, CFG]:
            k = kind(f)
            if k == "folder":
                types.append(f"  {f} (a folder: this run writes a file there)")
            elif k not in (None, "file", "link"):
                types.append(f"  {f} (not a regular file: this run writes a file there)")
        section(f"{given} holds a file where this run needs a folder, or a folder where it writes a file:",
                types, "move them yourself, or use an empty folder.")

        section(f"{given} holds files at paths bootdir writes, but this run would not write them "
                f"and no {MANIFEST} names them, so bootdir cannot tell whether they are stale:",
                [f"  {p}" for p in unknown], "delete or move them yourself, or use an empty folder.")
        if not clean:
            section(f"{given} holds files from an earlier bootdir run that this run would not write:",
                    [f"  {p}" for p in stale],
                    "use an empty folder, or run again with CLEAN=1 to remove them.")

        perm = []
        folders = {"."} | set(newdirs) | {parent(p) for p in newdirs + new + remove}
        if cfg:
            folders.add(parent(CFG))
        for d in sorted(folders):
            if kind(d) == "folder" and not os.access(at(d), os.W_OK | os.X_OK):
                perm.append(f"  {d} (folder)")
        for f in sorted(set(new + remove + [MANIFEST])):
            if kind(f) == "file" and not os.access(at(f), os.W_OK):
                perm.append(f"  {f}")
        section(f"this run must write these paths in {given}, but they are not writable:", perm)
    else:
        up = os.path.dirname(os.path.abspath(root))
        while not os.path.isdir(up):
            up = os.path.dirname(up)
        if not os.access(up, os.W_OK | os.X_OK):
            section(f"DIR={given} does not exist and bootdir cannot create it:",
                    [f"  {up} (folder, not writable)"])

    if report:
        print("\n".join(report), file=sys.stderr)
        raise Stop("Nothing was changed.")
    if other:
        print(f"bootdir: leaving these files in {given} alone, bootdir did not write them:", file=sys.stderr)
        print("\n".join(f"  {p}" for p in other), file=sys.stderr)

    # ---- apply
    mask = os.umask(0)
    os.umask(mask)
    try:
        os.makedirs(root, exist_ok=True)
        for f in remove:
            os.unlink(at(f))
            print(f"bootdir: removed {f}")
        for d in newdirs:
            if kind(d) != "folder":
                os.mkdir(at(d))
        for f in new + ([CFG] if cfg else []):
            with open(os.path.join(stage, f), "rb") as src:
                put(at(f), src.read(), mask)
        put(at(MANIFEST), "".join(p + "\n" for p in new).encode("utf-8", "surrogateescape"), mask)
    except OSError as e:
        print(f"bootdir: unexpected error after the checks passed: {e}.", file=sys.stderr)
        raise Stop(f"{given} is partly updated. Fix the cause (a full disk?) and run again.")
    return 0


def put(dst, data, mask):
    """Writes dst through a temporary file in the same folder and a rename."""
    k = lkind(dst)
    if k not in (None, "file"):
        raise OSError(f"{dst} changed during the run, it is now a {k}")
    mode = stat.S_IMODE(os.lstat(dst).st_mode) if k else 0o666 & ~mask
    fd, tmp = tempfile.mkstemp(prefix=".bootdir-", dir=os.path.dirname(dst))
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, dst)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


COMMANDS = {
    "check-tapes": ("check_tapes", check_tapes),
    "stage-card-dir": ("card_dir", stage_card_dir),
    "check-stage": ("check_stage", check_stage),
    "sync-bootdir": ("bootdir", sync_bootdir),
}


def main(argv):
    if not argv or argv[0] not in COMMANDS:
        print("usage: card.py {" + ",".join(COMMANDS) + "} ...", file=sys.stderr)
        return 1
    prog, fn = COMMANDS[argv[0]]
    try:
        return fn(argv[1:])
    except Stop as e:
        print(f"{prog}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
