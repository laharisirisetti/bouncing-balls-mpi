#!/usr/bin/env bash
# Benchmark runner: times the sequential reference and the parallel implementation
# at several process counts over the generated benchmark inputs, repeating each
# configuration REPS times (plus one untimed warm-up) and recording every run to
# results/raw.csv. Timing comes from the "TIME <seconds>" line each binary prints
# to stderr (the algorithm region only, excluding I/O).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/src"
BUILD="$ROOT/build"
TOOLS="$ROOT/tools"
BENCH="$ROOT/tests/bench"
RESULTS="$ROOT/results"

NPROCS=(1 2 4 8)
REPS=3

# benchmark cases: "name N M K T"
CASES=(
  "medium 1000 1000 100000 500"
  "large  2000 2000 200000 500"
)

mkdir -p "$BUILD" "$BENCH" "$RESULTS"

echo "== building =="
g++    -std=c++17 -O2 "$SRC/sequential.cpp"  -o "$BUILD/sequential" || exit 1
mpic++ -std=c++17 -O2 "$SRC/parallel.cpp"    -o "$BUILD/parallel"   || exit 1
g++    -std=c++17 -O2 "$TOOLS/gen_input.cpp" -o "$BUILD/gen_input"  || exit 1

echo "== generating inputs =="
for c in "${CASES[@]}"; do
  read -r name N M K T <<< "$c"
  in="$BENCH/$name.in"
  if [[ ! -f "$in" ]]; then
    "$BUILD/gen_input" "$N" "$M" "$K" "$T" 42 > "$in"
  fi
  echo "  $name -> $in"
done

# extract the seconds from a "TIME <sec>" stderr line
parse_time(){ awk '/^TIME/{print $2}'; }

raw="$RESULTS/raw.csv"
echo "case,impl,np,run,time_sec" > "$raw"

echo "== benchmarking (REPS=$REPS) =="
for c in "${CASES[@]}"; do
  read -r name N M K T <<< "$c"
  in="$BENCH/$name.in"

  # sequential (baseline)
  "$BUILD/sequential" < "$in" >/dev/null 2>/dev/null   # warm-up
  for r in $(seq 1 "$REPS"); do
    t="$("$BUILD/sequential" < "$in" 2>&1 >/dev/null | parse_time)"
    echo "$name,sequential,1,$r,$t" >> "$raw"
    echo "  $name sequential run=$r  ${t}s"
  done

  # parallel at each process count
  for np in "${NPROCS[@]}"; do
    mpirun --oversubscribe -np "$np" "$BUILD/parallel" < "$in" >/dev/null 2>/dev/null  # warm-up
    for r in $(seq 1 "$REPS"); do
      t="$(mpirun --oversubscribe -np "$np" "$BUILD/parallel" < "$in" 2>&1 >/dev/null | parse_time)"
      echo "$name,parallel,$np,$r,$t" >> "$raw"
      echo "  $name parallel np=$np run=$r  ${t}s"
    done
  done
done

echo "== raw results -> $raw =="
