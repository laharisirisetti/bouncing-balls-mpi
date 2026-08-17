#!/usr/bin/env python3
"""Plot benchmark results as standalone SVG files (no third-party deps).

Reads results/summary.csv and writes:
  results/plots/runtime.svg     mean algorithm time vs np (per case)
  results/plots/speedup.svg     speedup vs np (per case) with ideal y=x
  results/plots/efficiency.svg  efficiency vs np (per case) with ideal y=1
"""
import csv
import os
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SUMMARY = os.path.join(ROOT, "results", "summary.csv")
PLOTS = os.path.join(ROOT, "results", "plots")

W, H = 860, 440
ML, MR, MT, MB = 64, 300, 44, 52  # margins (right margin holds the legend)
PX0, PX1 = ML, W - MR
PY0, PY1 = MT, H - MB
COLORS = ["#1f77b4", "#d62728", "#2ca02c", "#9467bd"]


def load():
    cases = defaultdict(lambda: {"np": [], "runtime": [], "speedup": [],
                                 "efficiency": [], "seq": None})
    with open(SUMMARY) as f:
        for r in csv.DictReader(f):
            c = cases[r["case"]]
            if r["impl"] == "sequential":
                c["seq"] = float(r["mean"])
            else:
                c["np"].append(int(r["np"]))
                c["runtime"].append(float(r["mean"]))
                c["speedup"].append(float(r["speedup"]))
                c["efficiency"].append(float(r["efficiency"]))
    # sort each case's series by np
    for c in cases.values():
        order = sorted(range(len(c["np"])), key=lambda i: c["np"][i])
        for k in ("np", "runtime", "speedup", "efficiency"):
            c[k] = [c[k][i] for i in order]
    return cases


def read_params(name):
    """Return (N, M, K, T) from the benchmark input header, or None if absent."""
    p = os.path.join(ROOT, "tests", "bench", name + ".in")
    try:
        with open(p) as f:
            N, M, K, T = f.readline().split()[:4]
        return int(N), int(M), int(K), int(T)
    except (OSError, ValueError):
        return None


def label_for(name):
    p = read_params(name)
    if not p:
        return name
    return f"{name}  ({p[0]}\u00d7{p[1]}, K={p[2]}, T={p[3]})"


def _sx(x, xmin, xmax):
    return PX0 + (x - xmin) / (xmax - xmin) * (PX1 - PX0)


def _sy(y, ymin, ymax):
    return PY1 - (y - ymin) / (ymax - ymin) * (PY1 - PY0)


def _ticks(vmax, n=5):
    step = vmax / n
    # round step to a "nice" number
    import math
    mag = 10 ** math.floor(math.log10(step)) if step > 0 else 1
    for m in (1, 2, 2.5, 5, 10):
        if m * mag >= step:
            step = m * mag
            break
    ticks, v = [], 0.0
    while v <= vmax + 1e-9:
        ticks.append(round(v, 6))
        v += step
    return ticks


def svg_chart(path, title, series, xvals, ylabel, ymax, ideal=None):
    xmin, xmax = min(xvals), max(xvals)
    ymin = 0
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
        f'font-family="sans-serif" font-size="13">',
        f'<rect width="{W}" height="{H}" fill="white"/>',
        f'<text x="{W/2}" y="24" text-anchor="middle" font-size="16" '
        f'font-weight="bold">{title}</text>',
    ]
    # y gridlines + labels
    for t in _ticks(ymax):
        y = _sy(t, ymin, ymax)
        parts.append(f'<line x1="{PX0}" y1="{y:.1f}" x2="{PX1}" y2="{y:.1f}" '
                     f'stroke="#e6e6e6"/>')
        parts.append(f'<text x="{PX0-8}" y="{y+4:.1f}" text-anchor="end" '
                     f'fill="#444">{t:g}</text>')
    # x ticks + labels
    for xv in xvals:
        x = _sx(xv, xmin, xmax)
        parts.append(f'<line x1="{x:.1f}" y1="{PY1}" x2="{x:.1f}" y2="{PY1+5}" '
                     f'stroke="#444"/>')
        parts.append(f'<text x="{x:.1f}" y="{PY1+20}" text-anchor="middle" '
                     f'fill="#444">{xv}</text>')
    # axes
    parts.append(f'<line x1="{PX0}" y1="{PY0}" x2="{PX0}" y2="{PY1}" stroke="#444"/>')
    parts.append(f'<line x1="{PX0}" y1="{PY1}" x2="{PX1}" y2="{PY1}" stroke="#444"/>')
    parts.append(f'<text x="{(PX0+PX1)/2}" y="{H-12}" text-anchor="middle" '
                 f'fill="#444">processes (np)</text>')
    parts.append(f'<text x="16" y="{(PY0+PY1)/2}" text-anchor="middle" fill="#444" '
                 f'transform="rotate(-90 16 {(PY0+PY1)/2})">{ylabel}</text>')
    # ideal reference line
    if ideal is not None:
        pts = " ".join(f"{_sx(x,xmin,xmax):.1f},{_sy(ideal(x),ymin,ymax):.1f}"
                       for x in xvals)
        parts.append(f'<polyline points="{pts}" fill="none" stroke="#888" '
                     f'stroke-dasharray="6 4"/>')
    # data series
    for i, (label, ys) in enumerate(series):
        color = COLORS[i % len(COLORS)]
        pts = " ".join(f"{_sx(x,xmin,xmax):.1f},{_sy(y,ymin,ymax):.1f}"
                       for x, y in zip(xvals, ys))
        parts.append(f'<polyline points="{pts}" fill="none" stroke="{color}" '
                     f'stroke-width="2"/>')
        for x, y in zip(xvals, ys):
            parts.append(f'<circle cx="{_sx(x,xmin,xmax):.1f}" '
                         f'cy="{_sy(y,ymin,ymax):.1f}" r="3.5" fill="{color}"/>')
    # legend
    lx, ly = PX1 + 18, PY0 + 8
    legend = list(series) + ([("ideal", None)] if ideal is not None else [])
    for i, (label, _) in enumerate(legend):
        y = ly + i * 22
        color = "#888" if label == "ideal" else COLORS[i % len(COLORS)]
        dash = ' stroke-dasharray="6 4"' if label == "ideal" else ""
        parts.append(f'<line x1="{lx}" y1="{y}" x2="{lx+22}" y2="{y}" '
                     f'stroke="{color}" stroke-width="2"{dash}/>')
        parts.append(f'<text x="{lx+28}" y="{y+4}" fill="#333" '
                     f'font-size="12">{label}</text>')
    parts.append("</svg>")
    with open(path, "w") as f:
        f.write("\n".join(parts))
    print("wrote", path)


def main():
    os.makedirs(PLOTS, exist_ok=True)
    cases = load()
    names = sorted(cases)
    labels = {n: label_for(n) for n in names}
    # a common np axis (assume all cases share it)
    xvals = cases[names[0]]["np"]

    # runtime
    ymax = max(max(cases[n]["runtime"]) for n in names) * 1.1
    svg_chart(os.path.join(PLOTS, "runtime.svg"), "Runtime vs processes",
              [(labels[n], cases[n]["runtime"]) for n in names], xvals,
              "mean time (s)", ymax)

    # speedup (ideal y = x)
    ymax = max(max(cases[n]["speedup"]) for n in names + [names[0]])
    ymax = max(ymax, max(xvals)) * 1.1
    svg_chart(os.path.join(PLOTS, "speedup.svg"), "Speedup vs processes",
              [(labels[n], cases[n]["speedup"]) for n in names], xvals,
              "speedup (T_seq / T_par)", ymax, ideal=lambda x: x)

    # efficiency (ideal y = 1)
    ymax = max(1.0, max(max(cases[n]["efficiency"]) for n in names)) * 1.15
    svg_chart(os.path.join(PLOTS, "efficiency.svg"), "Efficiency vs processes",
              [(labels[n], cases[n]["efficiency"]) for n in names], xvals,
              "efficiency (speedup / np)", ymax, ideal=lambda x: 1.0)


if __name__ == "__main__":
    main()
