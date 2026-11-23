#!/usr/bin/env bash
# Integration tests for minic.
#   1. Golden tests: every examples/*.mini must print exactly tests/golden/<name>.out,
#      both with the optimizer (-O1, the default) and without it (-O0).
#   2. Differential native tests: examples marked `//! native` are compiled to x86-64
#      executables and must print exactly what the VM prints.
#   3. Error tests: each tests/errors/*.mini starts with `// expect: <exit code>: <message>`.
#
# Usage: tests/run_tests.sh [--update]    (--update regenerates the golden files)
set -u
cd "$(dirname "$0")/.."
MINIC=${MINIC:-./minic}
pass=0
fail=0
skip=0
work=$(mktemp -d "${TMPDIR:-/tmp}/minic-tests.XXXXXX")
cleanup() { [ -n "$work" ] && [ -d "$work" ] && find "$work" -mindepth 1 -delete && rmdir "$work"; }
trap cleanup EXIT

ok()  { echo "  PASS  $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL  $1"; fail=$((fail + 1)); }

if [ "${1:-}" = "--update" ]; then
    mkdir -p tests/golden
    for src in examples/*.mini; do
        name=$(basename "$src" .mini)
        $MINIC run "$src" > "tests/golden/$name.out" 2>&1
        echo "updated tests/golden/$name.out"
    done
    exit 0
fi

echo "golden tests (VM, -O1 and -O0)"
for src in examples/*.mini; do
    name=$(basename "$src" .mini)
    golden="tests/golden/$name.out"
    if [ ! -f "$golden" ]; then
        echo "  SKIP  $name (no golden file)"
        skip=$((skip + 1))
        continue
    fi
    for opt in -O1 -O0; do
        if $MINIC run $opt "$src" > "$work/out" 2>&1 && diff -u "$golden" "$work/out" > "$work/diff"; then
            ok "$name $opt"
        else
            bad "$name $opt"
            head -20 "$work/diff"
        fi
    done
done

echo "native backend (x86-64 output must match the VM)"
if [ "$(uname -s)" != "Linux" ] || [ "$(uname -m)" != "x86_64" ] || ! command -v "${CC:-cc}" > /dev/null; then
    echo "  SKIP  native tests need x86-64 Linux and a C compiler"
    skip=$((skip + 1))
else
    for src in $(grep -l '^//! native' examples/*.mini); do
        name=$(basename "$src" .mini)
        if ! $MINIC build "$src" -o "$work/$name" 2> "$work/err"; then
            bad "native $name (build failed)"
            cat "$work/err"
            continue
        fi
        if "$work/$name" > "$work/out" 2>&1 && diff -u "tests/golden/$name.out" "$work/out" > "$work/diff"; then
            ok "native $name"
        else
            bad "native $name"
            head -20 "$work/diff"
        fi
    done
fi

echo "error tests"
for src in tests/errors/*.mini; do
    name=$(basename "$src" .mini)
    header=$(head -1 "$src" | sed -n 's|^// expect: ||p')
    want_code=${header%%:*}
    want_msg=${header#*: }
    $MINIC run --no-color "$src" > /dev/null 2> "$work/err"
    code=$?
    if [ "$code" = "$want_code" ] && grep -qF -- "$want_msg" "$work/err"; then
        ok "$name (exit $code)"
    else
        bad "$name: expected exit $want_code with \"$want_msg\", got exit $code:"
        sed 's/^/        /' "$work/err"
    fi
done

echo
echo "$pass passed, $fail failed, $skip skipped"
[ "$fail" -eq 0 ]
