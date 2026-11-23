#!/usr/bin/env bash
# Checks that every RobCo-Core link in the Markdown files points at the
# commit the RobCo-Core submodule has checked out:
#
#   tools/check_doc_links.sh [root]
#
# A link is github.com/RobCo-Industries/RobCo-Core/(blob|tree|commit)/<hash>.
# CHANGELOG.md is skipped: it records old pins on purpose.
# root defaults to the project root. The submodule is root/external/robco-core,
# or ROBCO_CORE if set. Like RobCo-Core's check_pins.sh, it reads the commit
# from the submodule's git files and never calls git: in the CI container git
# refuses a checkout owned by another user. Without git it cannot tell
# tracked files, so it scans every *.md file except under .git, build, .venv,
# external, .claude and sdk/examples/*/build, and except CLAUDE.md and AGENTS.md.
# Exits 0 when every link matches, 1 when one differs or the commit is unknown.
set -euo pipefail

die() {
    echo "check-doc-links: $*" >&2
    exit 1
}

root=${1:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$root"
core=${ROBCO_CORE:-external/robco-core}

# The git directory of the submodule: .git is a directory, or a file with
# "gitdir: <path>", the path relative to the submodule.
gitdir=$core/.git
if [ -f "$gitdir" ]; then
    line=$(head -n 1 "$gitdir")
    case "$line" in
    gitdir:*) ;;
    *) die "$gitdir does not start with gitdir:" ;;
    esac
    gitdir=${line#gitdir:}
    gitdir=${gitdir#"${gitdir%%[![:space:]]*}"}
    case "$gitdir" in
    /*) ;;
    *) gitdir=$core/$gitdir ;;
    esac
fi
[ -f "$gitdir/HEAD" ] || die "no HEAD in $gitdir: run git submodule update --init"

head=$(tr -d '\r' < "$gitdir/HEAD" | head -n 1)
case "$head" in
ref:*)
    ref=${head#ref:}
    ref=${ref#"${ref%%[![:space:]]*}"}
    commit=
    common=$gitdir
    [ -f "$gitdir/commondir" ] && common=$gitdir/$(head -n 1 "$gitdir/commondir")
    for d in "$gitdir" "$common"; do
        if [ -f "$d/$ref" ]; then
            commit=$(head -n 1 "$d/$ref" | tr -d '\r')
            break
        fi
        if [ -f "$d/packed-refs" ]; then
            commit=$(awk -v r="$ref" '$2 == r { print $1; exit }' "$d/packed-refs")
            [ -n "$commit" ] && break
        fi
    done
    [ -n "$commit" ] || die "cannot resolve $ref in $gitdir"
    ;;
*) commit=$head ;;
esac
case "$commit" in
*[!0-9a-f]* | '') die "bad commit '$commit' in $gitdir" ;;
esac

status=0
while IFS= read -r -d '' f; do
    case "${f#./}" in
    CHANGELOG.md | CLAUDE.md | AGENTS.md) continue ;;
    esac
    while IFS=: read -r n hash; do
        if [ "$hash" != "$commit" ]; then
            echo "check-doc-links: ${f#./}:$n links RobCo-Core at $hash, the submodule is at $commit" >&2
            status=1
        fi
    done < <(grep -noE 'github\.com/RobCo-Industries/RobCo-Core/(blob|tree|commit)/[0-9a-fA-F]+' "$f" |
        sed -E 's#^([0-9]+):.*/(blob|tree|commit)/#\1:#')
done < <(find . \( -name .git -o -path ./build -o -path ./.venv -o -path ./external \
    -o -name .claude -o -path './sdk/examples/*/build' \) -prune -o -name '*.md' -type f -print0)

[ "$status" -eq 0 ] && echo "check-doc-links: ok ($commit)"
exit "$status"
