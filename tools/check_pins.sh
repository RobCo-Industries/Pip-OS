#!/usr/bin/env bash
# Checks that .github/workflows/ci.yml calls the RobCo-Core reusable
# workflow at the commit the submodule is pinned to, with the CI image tag
# from the submodule's ci/IMAGE_TAG. A reusable workflow takes literals
# only, so these values are copies and can drift.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
core=external/robco-core
workflow=$root/.github/workflows/ci.yml

die() {
    echo "check-pins: $*" >&2
    exit 1
}

[ -f "$workflow" ] || die "$workflow is missing"
[ -f "$root/$core/ci/IMAGE_TAG" ] || die "$core is empty: run git submodule update --init"

# The commit recorded in the index for the submodule (mode 160000).
pinned=$(git -C "$root" ls-files -s -- "$core" | awk '$1 == "160000" { print $2 }')
[ -n "$pinned" ] || die "cannot read the pinned commit of $core"
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
