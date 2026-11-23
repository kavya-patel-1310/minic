#!/usr/bin/env bash
# Times each benchmark on the VM (-O0 and -O1), as a native executable, and on CPython
# for reference. Reports the best of 3 runs in seconds.
set -u
cd "$(dirname "$0")/.."
MINIC=${MINIC:-./minic}
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/minic-bench.XXXXXX")
cleanup() { [ -n "$out_dir" ] && [ -d "$out_dir" ] && find "$out_dir" -mindepth 1 -delete && rmdir "$out_dir"; }
trap cleanup EXIT

best_of_3() {
    local best="" t
    for _ in 1 2 3; do
        local start end
        start=$(date +%s.%N)
        "$@" > /dev/null 2>&1 || { echo "failed"; return; }
        end=$(date +%s.%N)
        t=$(echo "$end - $start" | bc)
        if [ -z "$best" ] || [ "$(echo "$t < $best" | bc)" = 1 ]; then best=$t; fi
    done
    printf "%.3f" "$best"
}

printf "%-10s %10s %10s %10s %10s\n" "benchmark" "VM -O0" "VM -O1" "native" "python3"
for src in benchmarks/*.mini; do
    name=$(basename "$src" .mini)
    o0=$(best_of_3 $MINIC run -O0 "$src")
    o1=$(best_of_3 $MINIC run "$src")
    native="n/a"
    if grep -q '^//! native' "$src" && $MINIC build "$src" -o "$out_dir/$name" 2> /dev/null; then
        native=$(best_of_3 "$out_dir/$name")
    fi
    py="n/a"
    if command -v python3 > /dev/null && [ -f "benchmarks/$name.py" ]; then
        py=$(best_of_3 python3 "benchmarks/$name.py")
    fi
    printf "%-10s %10s %10s %10s %10s\n" "$name" "$o0" "$o1" "$native" "$py"
done
