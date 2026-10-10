#!/bin/sh
# sweep-compile.sh -- how many translation units of the decompilation compile?
#
# This is the harness behind every "N of 66" figure published in this repo. It
# compiles ONE unit at a time with `-c` (a real object file, not a syntax check)
# and `-ferror-limit=0` so the first error does not hide the other 300. It does
# not fix anything and it does not stop at the first failure.
#
# Requirements:
#   * the decompiled source tree (separate project, GPL, not shipped here)
#   * a compilable platform/windows.h shim layer
#   * the 1999 DirectX 7 SDK headers
#   * clang or `zig c++`
#
# Usage:
#   CMR2_TREE=/path/to/CMR2Decomp \
#   CMR2_PLATFORM=/path/to/port/platform \
#   CMR2_SHIM=/path/to/dx7sdk-7001/include \
#   ./sweep-compile.sh [outdir] [target]
#
#   target defaults to x86_64-linux-gnu. Running it twice, once with
#   `x86-linux-gnu` (i386), is a DIAGNOSTIC: a file that fails on x86_64 and
#   passes on i386 is failing on pointer width and nothing else. Zero files go
#   the other way. See docs/PORT-PLAN.md section 3.
set -u

# Optional: source a toolchain environment first (this is how the published
# numbers were produced -- zig/cmake/ninja installed rootless).
if [ -n "${CMR2_ENV:-}" ]; then
    # shellcheck disable=SC1090
    . "$CMR2_ENV"
fi
# zig's cache is shared state and a pruned one breaks linking with
# "cannot open .../crt1.o". Point it somewhere private if you have seen that.
export ZIG_GLOBAL_CACHE_DIR="${ZIG_GLOBAL_CACHE_DIR:-${TMPDIR:-/tmp}/zigcache-cmr2}"

TREE="${CMR2_TREE:?set CMR2_TREE to the decompiled source directory}"
PLAT="${CMR2_PLATFORM:?set CMR2_PLATFORM to the platform shim directory}"
SHIM="${CMR2_SHIM:?set CMR2_SHIM to the DirectX 7 SDK include directory}"
OUT="${1:-./sweep-out}"
TARGET="${2:-x86_64-linux-gnu}"
CXX="${CXX:-zig c++}"

# Include order is load-bearing: platform, then tree, then the SDK. Put the SDK
# first and the 1999 headers win and nothing compiles. Do not "tidy" this.
mkdir -p "$OUT/obj" "$OUT/log"
: > "$OUT/results.txt"
: > "$OUT/all_errors.txt"
PASS=0; FAIL=0

for f in "$TREE"/*.cpp; do
    n=$(basename "$f"); b=${n%.cpp}
    case "$n" in
        Zlib*) STD="-std=gnu++14"; EXTRA="-I$TREE/zlib" ;;
        *)     STD="-std=c++17";   EXTRA="" ;;
    esac
    # shellcheck disable=SC2086
    # -Wno-c++11-narrowing: the i386 build passes this, and it is why "66/66 on
    # i386" is true. Without it clang makes two 32-bit files fail on case values
    # above INT_MAX (Game.cpp and GameInfo.cpp, HRESULT-style constants) -- the
    # published sweep then reports 64, not 66. It does NOT change the x86_64
    # count: both of those files already fail there on the pointer class first.
    # measured both ways 2026-10-10.
    if $CXX -target "$TARGET" -w -Wno-c++11-narrowing -ferror-limit=0 $STD -DCMR2_NATIVE=1 \
         -include "$PLAT/platform_types.h" \
         -I"$PLAT" -I"$TREE" -I"$SHIM" $EXTRA \
         -c "$f" -o "$OUT/obj/$b.o" > "$OUT/log/$n.log" 2>&1; then
        echo "PASS $n" >> "$OUT/results.txt"; PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
        e=$(grep -m1 -E 'error:' "$OUT/log/$n.log" | sed 's/^.*error: //' | cut -c1-110)
        echo "FAIL $n :: $e" >> "$OUT/results.txt"
        grep -E 'error:' "$OUT/log/$n.log" | sed "s|^.*/$b\.cpp|$b.cpp|" >> "$OUT/all_errors.txt"
    fi
done

echo "=== sweep-compile  target=$TARGET  includes: PLAT,TREE,SHIM ==="
echo "PASS $PASS  FAIL $FAIL  TOTAL $((PASS+FAIL))"
echo "objects produced: $(ls "$OUT/obj" 2>/dev/null | wc -l)"
echo
echo "=== ranked failure reasons (FIRST error per file) ==="
grep '^FAIL' "$OUT/results.txt" | sed 's/^FAIL [^:]*:: //' \
    | sed -E "s/'[^']*'/'X'/g" | sort | uniq -c | sort -rn | head -30
echo
echo "=== every error, ranked by class (all errors, all files) ==="
printf '  %6d  cast from pointer to smaller type\n' "$(grep -c 'cast from pointer to smaller type' "$OUT/all_errors.txt" || true)"
printf '  %6d  undeclared identifier\n'             "$(grep -c 'use of undeclared identifier' "$OUT/all_errors.txt" || true)"
printf '  %6d  unknown type name\n'                 "$(grep -c 'unknown type name' "$OUT/all_errors.txt" || true)"
printf '  %6d  incomplete type\n'                   "$(grep -c 'incomplete type' "$OUT/all_errors.txt" || true)"
printf '  %6d  negative array size (sizeof assert)\n' "$(grep -c 'negative size' "$OUT/all_errors.txt" || true)"
echo "  full log: $OUT/all_errors.txt"
