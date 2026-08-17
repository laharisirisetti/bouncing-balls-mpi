#!/usr/bin/env bash
# Correctness check: run the parallel implementation at several process counts
# and diff each result against the sequential reference, using the input cases
# already present in tests/cases.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
CASES="$ROOT/tests/cases"

NPROCS=(1 2 4 8 12)

mkdir -p "$BUILD"

echo "== building =="
g++    -std=c++17 -O2 "$SRC/sequential.cpp" -o "$BUILD/sequential" || exit 1
mpic++ -std=c++17 -O2 "$SRC/parallel.cpp"   -o "$BUILD/parallel"   || exit 1

pass=0; fail=0
for in in "$CASES"/*.in; do
  ref="$("$BUILD/sequential" < "$in")"
  for np in "${NPROCS[@]}"; do
    out="$(mpirun --oversubscribe -np "$np" "$BUILD/parallel" < "$in" 2>/dev/null)"
    if [[ "$out" == "$ref" ]]; then
      echo "PASS  $(basename "$in")  np=$np"
      pass=$((pass+1))
    else
      echo "FAIL  $(basename "$in")  np=$np"
      diff <(printf '%s\n' "$ref") <(printf '%s\n' "$out") | head -20
      fail=$((fail+1))
    fi
  done
done

echo "== done: $pass passed, $fail failed =="
[[ $fail -eq 0 ]]
