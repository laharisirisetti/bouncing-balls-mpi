#!/usr/bin/env python3
"""Aggregate results/raw.csv into results/summary.csv.

For each (case, impl, np) config, computes mean / median / min / max / stddev of
the timed runs, then speedup (sequential mean / parallel mean) and efficiency
(speedup / np). Uses only the Python standard library.
"""
import csv
import os
import statistics as st
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RAW = os.path.join(ROOT, "results", "raw.csv")
SUMMARY = os.path.join(ROOT, "results", "summary.csv")


def main():
    # group times by (case, impl, np)
    groups = defaultdict(list)
    with open(RAW) as f:
        for row in csv.DictReader(f):
            key = (row["case"], row["impl"], int(row["np"]))
            groups[key].append(float(row["time_sec"]))

    # sequential mean per case = speedup baseline
    seq_mean = {}
    for (case, impl, np_), times in groups.items():
        if impl == "sequential":
            seq_mean[case] = st.mean(times)

    rows = []
    for (case, impl, np_), times in groups.items():
        mean = st.mean(times)
        base = seq_mean.get(case)
        speedup = (base / mean) if (base and mean > 0) else ""
        efficiency = (speedup / np_) if (speedup != "" and np_ > 0) else ""
        rows.append({
            "case": case,
            "impl": impl,
            "np": np_,
            "mean": round(mean, 6),
            "median": round(st.median(times), 6),
            "min": round(min(times), 6),
            "max": round(max(times), 6),
            "stddev": round(st.pstdev(times) if len(times) < 2 else st.stdev(times), 6),
            "speedup": round(speedup, 3) if speedup != "" else "",
            "efficiency": round(efficiency, 3) if efficiency != "" else "",
        })

    rows.sort(key=lambda r: (r["case"], r["impl"], r["np"]))

    fields = ["case", "impl", "np", "mean", "median", "min", "max",
              "stddev", "speedup", "efficiency"]
    with open(SUMMARY, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    # pretty print to console
    widths = {k: max(len(k), *(len(str(r[k])) for r in rows)) for k in fields}
    print("  ".join(k.ljust(widths[k]) for k in fields))
    for r in rows:
        print("  ".join(str(r[k]).ljust(widths[k]) for k in fields))
    print(f"\nwrote {SUMMARY}")


if __name__ == "__main__":
    main()
