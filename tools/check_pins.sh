#!/usr/bin/env bash
# Checks that .github/workflows/ci.yml calls the RobCo-Core reusable
# workflow at the commit the submodule is pinned to, with the CI image tag
# from the submodule's ci/IMAGE_TAG. A reusable workflow takes literals
# only, so these values are copies and can drift. PIPOS_CI_WORKFLOW names
# another workflow file to check, for tests.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
core=external/robco-core
workflow=${PIPOS_CI_WORKFLOW:-$root/.github/workflows/ci.yml}

die() {
    echo "check-pins: $*" >&2
    exit 1
}

[ -f "$workflow" ] || die "$workflow is missing"
[ -f "$root/$core/ci/IMAGE_TAG" ] || die "$core is empty: run git submodule update --init"

# Reads the commit checked out in the submodule from its files, without
# git: CI runs as root on a checkout owned by another user, where every git
# command on the work tree fails.
read_ref() { # gitdir ref
    local f
    if [ -f "$1/$2" ]; then
        tr -d ' \t\r\n' < "$1/$2"
        return
    fi
    f=$1/packed-refs
    [ -f "$f" ] && awk -v r="$2" '$2 == r { print $1; exit }' "$f"
    return 0
}

dotgit=$root/$core/.git
if [ -d "$dotgit" ]; then
    gitdir=$dotgit
elif [ -f "$dotgit" ]; then
    gitdir=$(sed -n 's/^gitdir:[[:space:]]*//p' "$dotgit" | tr -d '\r')
    [ -n "$gitdir" ] || die "$dotgit has no gitdir line"
    case $gitdir in
        /*) ;;
        *) gitdir=$root/$core/$gitdir ;;
    esac
else
    die "$core/.git is missing: run git submodule update --init"
fi
[ -f "$gitdir/HEAD" ] || die "$gitdir/HEAD is missing"
common=$gitdir
if [ -f "$gitdir/commondir" ]; then
    common=$(tr -d '\r\n' < "$gitdir/commondir")
    case $common in /*) ;; *) common=$gitdir/$common ;; esac
fi
head=$(tr -d '\r\n' < "$gitdir/HEAD")
case $head in
    "ref: "*)
        ref=${head#ref: }
        pinned=$(read_ref "$gitdir" "$ref")
        [ -n "$pinned" ] || pinned=$(read_ref "$common" "$ref")
        ;;
    *) pinned=$head ;;
esac
[[ "$pinned" =~ ^[0-9a-f]{40}$ ]] || die "cannot read the commit of $core from $gitdir"

# Optional cross-check: when git works, the index entry must match.
if indexed=$(git -C "$root" ls-files -s -- "$core" 2>/dev/null | awk '$1 == "160000" { print $2 }') &&
    [ -n "$indexed" ] && [ "$indexed" != "$pinned" ]; then
    die "$core is checked out at $pinned, the index pins $indexed: run git submodule update"
fi
tag=$(tr -d ' \t\r\n' < "$root/$core/ci/IMAGE_TAG")

uses=$(sed -n 's/^[[:space:]]*uses:[[:space:]]*RobCo-Industries\/RobCo-Core\/\.github\/workflows\/robco-ci\.yml@\([0-9a-f]*\)[[:space:]]*$/\1/p' "$workflow")
wtag=$(sed -n 's/^[[:space:]]*image-tag:[[:space:]]*\([0-9A-Za-z._-]*\)[[:space:]]*$/\1/p' "$workflow")

status=0
if [ "$uses" != "$pinned" ]; then
    echo "check-pins: workflow uses robco-ci.yml@${uses:-<none>}, submodule is pinned at $pinned" >&2
    status=1
fi
if [ "$wtag" != "$tag" ]; then
    echo "check-pins: workflow image-tag is ${wtag:-<none>}, $core/ci/IMAGE_TAG is $tag" >&2
    status=1
fi
[ "$status" -eq 0 ] && echo "check-pins: ok ($pinned, $tag)"
exit "$status"
