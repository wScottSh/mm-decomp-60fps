#!/usr/bin/env bash
# Usage: tools/fps60/worktree-setup.sh UNIT BRANCH [BASE_REF]
#
# Creates ~/repos/mm-wt/UNIT on BRANCH (new from BASE_REF, default origin/main, or the existing
# branch), copies the untracked build inputs from the main checkout (baseroms, extracted assets,
# the IDO recompilation, the venv, .make_options.mk), and builds tools/. The main checkout is only
# read. Safe to rerun: an existing worktree is reused and copied paths are skipped. Prints the path.
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 UNIT BRANCH [BASE_REF]" >&2
    exit 2
fi
unit=$1
branch=$2
base=${3:-origin/main}
main=${MM_MAIN:-$HOME/repos/mm-decomp-60fps}
wt=${MM_WT_ROOT:-$HOME/repos/mm-wt}/$unit

git -C "$main" fetch -q origin

if [[ -e "$wt/.git" ]]; then
    current=$(git -C "$wt" rev-parse --abbrev-ref HEAD)
    if [[ "$current" != "$branch" ]]; then
        echo "$wt exists on branch $current, not $branch" >&2
        exit 1
    fi
elif git -C "$main" show-ref --verify --quiet "refs/heads/$branch"; then
    git -C "$main" worktree add -q "$wt" "$branch"
else
    git -C "$main" worktree add -q --no-track "$wt" -b "$branch" "$base"
fi

# The main checkout's untracked build inputs. Built tools are not copied: they link against the
# host libraries of the day they were built (tools/audio/sfc wanted libxml2.so.2 after the host
# moved to libxml2.so.16), so `make -C tools` rebuilds them from the worktree's sources.
paths=(baseroms/n64-us/baserom.z64 baseroms/n64-us/baserom-decompressed.z64 extracted .venv .make_options.mk
    tools/ido_recomp tools/compiler_archives)

for p in "${paths[@]}"; do
    if [[ -e "$wt/$p" || ! -e "$main/$p" ]]; then
        continue
    fi
    mkdir -p "$(dirname "$wt/$p")"
    # A copy, not a hard link: `make setup` rewrites extracted files in place, which would write
    # through a hard link into the main checkout.
    cp -a --reflink=auto "$main/$p" "$wt/$p.tmp.$$"
    mv "$wt/$p.tmp.$$" "$wt/$p"
done

make -s -C "$wt/tools" -j"$(nproc)" >&2

echo "$wt"
