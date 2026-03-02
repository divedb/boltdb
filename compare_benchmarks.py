#!/usr/bin/env python3
"""
Compare Go boltdb vs C++ boltdb freelist benchmark results.

Reads:
  - build/cpp_benchmark_results.json  (Google Benchmark JSON output)
  - build/go_benchmark_results.json   (custom Go benchmark JSON output)

Produces:
  - build/benchmark_comparison.png    (bar chart comparing the two)
  - Console table output
"""

import json
import os
import sys

# ── locate result files ──────────────────────────────────────────────
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(SCRIPT_DIR, "build")

CPP_FILE = os.path.join(BUILD_DIR, "cpp_benchmark_results.json")
GO_FILE = os.path.join(BUILD_DIR, "go_benchmark_results.json")

for f in (CPP_FILE, GO_FILE):
    if not os.path.exists(f):
        print(f"ERROR: Missing result file: {f}", file=sys.stderr)
        sys.exit(1)

# ── load C++ results ─────────────────────────────────────────────────
with open(CPP_FILE) as f:
    cpp_data = json.load(f)

cpp_release = {}
for bm in cpp_data["benchmarks"]:
    name = bm["name"]  # e.g. "BM_FreelistRelease/10000"
    if "BM_FreelistRelease" in name:
        size = int(name.split("/")[1])
        cpp_release[size] = bm["cpu_time"]  # already in ms

# ── load Go results ──────────────────────────────────────────────────
with open(GO_FILE) as f:
    go_data = json.load(f)

go_release = {}
for bm in go_data:
    size = bm["size"]
    go_release[size] = bm["ms_per_op"]

# ── common sizes ─────────────────────────────────────────────────────
sizes = sorted(set(cpp_release.keys()) & set(go_release.keys()))
if not sizes:
    print("ERROR: No matching benchmark sizes found!", file=sys.stderr)
    sys.exit(1)

cpp_times = [cpp_release[s] for s in sizes]
go_times = [go_release[s] for s in sizes]

# ── console table ────────────────────────────────────────────────────
print("\n" + "=" * 72)
print("  FreeList::Release Benchmark Comparison  (lower is better)")
print("=" * 72)
print(f"{'Size':>12}  {'C++ (ms)':>12}  {'Go (ms)':>12}  {'Speedup':>10}")
print("-" * 72)
for s, ct, gt in zip(sizes, cpp_times, go_times):
    speedup = gt / ct if ct > 0 else float("inf")
    print(f"{s:>12,}  {ct:>12.3f}  {gt:>12.3f}  {speedup:>9.1f}x")
print("=" * 72)
print()

# ── matplotlib chart ─────────────────────────────────────────────────
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import numpy as np
except ImportError:
    print("matplotlib/numpy not installed – skipping chart generation.")
    print("Install with:  pip install matplotlib numpy")
    sys.exit(0)

x = np.arange(len(sizes))
width = 0.35

fig, axes = plt.subplots(1, 2, figsize=(14, 6))

# ── subplot 1: absolute time (log scale) ────────────────────────────
ax1 = axes[0]
bars_cpp = ax1.bar(x - width / 2, cpp_times, width, label="C++ (boltdb-cpp)",
                   color="#3574A6", edgecolor="white")
bars_go = ax1.bar(x + width / 2, go_times, width, label="Go (boltdb)",
                  color="#00ADD8", edgecolor="white")

ax1.set_xlabel("Number of Free Pages", fontsize=12)
ax1.set_ylabel("Time per Operation (ms)", fontsize=12)
ax1.set_title("FreeList Release Benchmark", fontsize=14, fontweight="bold")
ax1.set_xticks(x)
ax1.set_xticklabels([f"{s:,}" for s in sizes], rotation=20, ha="right")
ax1.set_yscale("log")
ax1.legend(fontsize=11)
ax1.grid(axis="y", alpha=0.3)

# Add value labels on bars
for bar in bars_cpp:
    h = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width() / 2., h * 1.1,
             f"{h:.2f}", ha="center", va="bottom", fontsize=8)
for bar in bars_go:
    h = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width() / 2., h * 1.1,
             f"{h:.2f}", ha="center", va="bottom", fontsize=8)

# ── subplot 2: speedup ratio ────────────────────────────────────────
ax2 = axes[1]
speedups = [gt / ct if ct > 0 else 0 for ct, gt in zip(cpp_times, go_times)]
colors = ["#2ecc71" if s > 1 else "#e74c3c" for s in speedups]
bars_sp = ax2.bar(x, speedups, width * 1.5, color=colors, edgecolor="white")

ax2.axhline(y=1, color="gray", linestyle="--", alpha=0.7, label="Parity (1x)")
ax2.set_xlabel("Number of Free Pages", fontsize=12)
ax2.set_ylabel("Speedup (Go time / C++ time)", fontsize=12)
ax2.set_title("C++ Speedup over Go", fontsize=14, fontweight="bold")
ax2.set_xticks(x)
ax2.set_xticklabels([f"{s:,}" for s in sizes], rotation=20, ha="right")
ax2.legend(fontsize=10)
ax2.grid(axis="y", alpha=0.3)

for bar, sp in zip(bars_sp, speedups):
    ax2.text(bar.get_x() + bar.get_width() / 2., bar.get_height() + 0.3,
             f"{sp:.1f}x", ha="center", va="bottom", fontsize=11, fontweight="bold")

plt.tight_layout()
out_path = os.path.join(BUILD_DIR, "benchmark_comparison.png")
plt.savefig(out_path, dpi=150)
print(f"Chart saved to: {out_path}")
