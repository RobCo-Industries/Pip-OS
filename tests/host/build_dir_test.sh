#!/bin/sh
# Checks that make test-target passes a custom BUILD to pytest and that
# the fixture resolves the kernel image inside that directory.
set -eu
cd "$(dirname "$0")/../.."
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cmd=$(make -n test-target BUILD="$dir" | grep 'pytest tests')
case "$cmd" in
  *"--build-dir $dir"*) ;;
  *) echo "build_dir_test: pytest does not get --build-dir $dir: $cmd" >&2; exit 1 ;;
esac
path=$(python3 -c 'import os,sys
root=os.getcwd()
src=open("tests/conftest.py").read()
start=src.index("def build_args")
ns={"os":os,"ROOT":root}
exec(src[start:src.index("\n\n\n",start)],ns)
print(ns["build_args"](sys.argv[1])["image"])' "$dir")
case "$path" in
  "$dir/pipos.img") ;;
  *) echo "build_dir_test: image path $path is not in $dir" >&2; exit 1 ;;
esac
echo "build_dir_test: passed"
