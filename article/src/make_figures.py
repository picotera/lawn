#!/usr/bin/env python3
"""Regenerate the paper's figures from the committed C benchmark CSVs.

Reads src/benchmarks/c/results/*.csv and writes the article/*.png the paper
references. Reproducible: no manual renaming. Run via `make figures`.

Main-baseline sweeps live in results/<op>_<axis>.csv (n=100K, ttl_span=1024,
distinct=100, uniform, unless the axis itself varies that parameter).
Extended-scale sweeps (n=10M baseline) live alongside as
results/<op>_<axis>_huge.csv. Both have a real header row, axis name as the
first column.
"""
import csv
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "..", "..", "src", "benchmarks", "c", "results")
OUT = os.path.join(HERE, "..")

BASE_N = 100_000
BASE_N_HUGE = 10_000_000

# consistent styling per algorithm
STYLE = {
    "lawn":       ("Lawn (reference)",       "#888888", "o"),
    "lawn2":      ("Lawn2 (optimized)",      "#1f77b4", "s"),
    "wahern":     ("Timer Wheel",            "#ff7f0e", "^"),
    "naive":      ("Naive ring",             "#2ca02c", "D"),
    "heap":       ("Binary heap",            "#9467bd", "v"),
    "linuxwheel": ("Non-cascading wheel",    "#d62728", "*"),
}
ORDER = ["lawn", "lawn2", "wahern", "naive"]
# Adapters added later, to test the wheel-generalization and mixed-workload
# questions. Kept out of ORDER (and so out of every other figure below) to
# keep the paper's main Lawn-vs-wheel-vs-naive story uncluttered.
ORDER_ALL = ORDER + ["heap", "linuxwheel"]


def read(fname):
    with open(os.path.join(RESULTS, fname)) as f:
        return list(csv.DictReader(f))


def by_algo(rows, xcol, ycol, scol=None):
    d = {}
    for r in rows:
        d.setdefault(r["algo"], ([], [], []))
        d[r["algo"]][0].append(float(r[xcol]))
        d[r["algo"]][1].append(float(r[ycol]))
        d[r["algo"]][2].append(float(r[scol]) if scol else 0.0)
    return d


def axis_plot(fname, xcol, ycol, scol, xlabel, ylabel, title, out,
              logx=True, logy=True):
    rows = read(fname)
    d = by_algo(rows, xcol, ycol, scol)
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER:
        if algo not in d:
            continue
        x, y, s = d[algo]
        label, color, marker = STYLE[algo]
        ax.errorbar(x, y, yerr=s, marker=marker, color=color, label=label,
                    capsize=2, markersize=5, linewidth=1.3)
    if logx:
        ax.set_xscale("log")
    if logy:
        ax.set_yscale("log")
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, out), dpi=130)
    plt.close(fig)
    print("wrote", out)


def inflection_plot():
    """The lifecycle-cost crossover: full mixed-workload (insert/delete/tick)
    cost of Lawn2 vs the Timer Wheel, as the distinct-TTL count grows, at each
    population N. Reads the lifecycle columns of inflection.csv (written by the
    extended run_inflection for lawn2/wahern). Speedup = Wheel/Lawn2 on a log
    y-axis so the parity crossing (the inflection point) is readable at every
    N; the per-tick columns in the same CSV are not plotted here."""
    rows = read("inflection.csv")
    # Keep only rows with a real lifecycle measurement (0 = not measured: the
    # fixed_span pass, or an N skipped by the memory guard). Drop t=1 (a single
    # shared TTL): a degenerate edge case where the wheel is trivially cheap,
    # covered separately by workload_expiry.png, that otherwise makes the
    # large-N lines start low and then jump.
    rows = [r for r in rows
            if int(r["t"]) > 1
            and float(r.get("lawn2_life_ns", 0) or 0) > 0
            and float(r.get("wahern_life_ns", 0) or 0) > 0]
    ns = sorted({int(r["N"]) for r in rows})
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for N in ns:
        sub = sorted((r for r in rows if int(r["N"]) == N), key=lambda r: int(r["t"]))
        xs = [int(r["t"]) for r in sub]
        ys = [float(r["wahern_life_ns"]) / float(r["lawn2_life_ns"]) for r in sub]
        ax.plot(xs, ys, marker="o", markersize=4, label=f"N={N:,}")
    ax.axhline(1.0, color="k", linestyle="--", linewidth=1,
               label="parity (Lawn2 = Wheel)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("distinct-TTL count  t")
    ax.set_ylabel("speedup  Wheel / Lawn2  (higher = Lawn2 wins by more)")
    ax.set_title("Lifecycle cost: Lawn2 vs Timer Wheel across distinct-TTL count")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "inflection.png"), dpi=130)
    plt.close(fig)
    print("wrote inflection.png")


def concurrency_plot():
    rows = [r for r in read("concurrent.csv") if r["regime"] == "sharded"]
    d = {}
    for r in rows:
        d.setdefault(r["impl"], ([], []))
        d[r["impl"]][0].append(int(r["threads"]))
        d[r["impl"]][1].append(float(r["ops_per_sec"]) / 1e6)
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER:
        if algo not in d:
            continue
        x, y = d[algo]
        label, color, marker = STYLE[algo]
        ax.plot(x, y, marker=marker, color=color, label=label, markersize=5)
    ax.set_xlabel("threads (sharded, one lock per shard)")
    ax.set_ylabel("throughput (M ops/s)")
    ax.set_title("Single-machine concurrency: modest edge, no scaling")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "concurrency.png"), dpi=130)
    plt.close(fig)
    print("wrote concurrency.png")


def extended_tick_plot():
    """tick_advance vs n, extended far past the main results' one-million-timer
    ceiling (results/tick_advance_n.csv + tick_advance_n_huge.csv), for
    Section VII.F."""
    rows = read("tick_advance_n.csv") + read("tick_advance_n_huge.csv")
    d = by_algo(rows, "n", "mean_ns", "std_ns")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER:
        if algo not in d:
            continue
        x, y, s = d[algo]
        pts = sorted(zip(x, y, s))
        x, y, s = zip(*pts)
        label, color, marker = STYLE[algo]
        ax.errorbar(x, y, yerr=s, marker=marker, color=color, label=label,
                    capsize=2, markersize=4, linewidth=1.2)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("number of timers (extended past 1M)")
    ax.set_ylabel("empty-tick latency (ns)")
    ax.set_title("Empty-tick cost to 100s of millions (lower is better)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "extended_tick.png"), dpi=130)
    plt.close(fig)
    print("wrote extended_tick.png")


def extended_memory_plot():
    """Per-timer memory vs n, extended far past the main results' one-million-
    timer ceiling (results/memory_n.csv + memory_n_huge.csv, which store total
    bytes, divided by n for per-timer bytes to match Figure memory.png's
    units)."""
    rows = read("memory_n.csv") + read("memory_n_huge.csv")
    d = {}
    for r in rows:
        n = float(r["n"])
        d.setdefault(r["algo"], ([], []))
        d[r["algo"]][0].append(n)
        d[r["algo"]][1].append(float(r["mean_bytes"]) / n)
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER:
        if algo not in d:
            continue
        x, y = d[algo]
        pts = sorted(zip(x, y)); x, y = zip(*pts)
        label, color, marker = STYLE[algo]
        ax.plot(x, y, marker=marker, color=color, label=label, markersize=4, linewidth=1.2)
    ax.set_xscale("log")
    ax.set_xlabel("number of timers (extended past 1M)")
    ax.set_ylabel("bytes per timer")
    ax.set_title("Per-timer memory to 100s of millions (lower is better)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "extended_memory.png"), dpi=130)
    plt.close(fig)
    print("wrote extended_memory.png")


def workload_expiry_plot():
    """Bar chart backing the Workload Characterization bullet list: Lawn2 vs.
    the wheel's per-tick expiry cost under four realistic TTL distributions
    (single TTL and ten distinct TTLs from expiry_distinct_ttls.csv, bursty and
    uniform arrivals from expiry_workload.csv)."""
    categories = ["Single TTL", "Few TTLs (10)", "Bursty", "Uniform"]
    d1 = [r for r in read("expiry_distinct_ttls.csv") if r["distinct_ttls"] == "1"]
    d10 = [r for r in read("expiry_distinct_ttls.csv") if r["distinct_ttls"] == "10"]
    wb = [r for r in read("expiry_workload.csv") if r["workload"] == "bursty"]
    wu = [r for r in read("expiry_workload.csv") if r["workload"] == "uniform"]

    def get(rows, algo):
        return next(float(r["mean_ns"]) for r in rows if r["algo"] == algo)

    lawn2_vals = [get(d1, "lawn2"), get(d10, "lawn2"), get(wb, "lawn2"), get(wu, "lawn2")]
    wheel_vals = [get(d1, "wahern"), get(d10, "wahern"), get(wb, "wahern"), get(wu, "wahern")]

    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    x = range(len(categories))
    width = 0.35
    l2_color = STYLE["lawn2"][1]; w_color = STYLE["wahern"][1]
    ax.bar([i - width / 2 for i in x], lawn2_vals, width, color=l2_color, label="Lawn2 (optimized)")
    ax.bar([i + width / 2 for i in x], wheel_vals, width, color=w_color, label="Timer Wheel")
    ax.set_xticks(list(x))
    ax.set_xticklabels(categories)
    ax.set_ylabel("per-tick expiry latency (ns)")
    ax.set_title("Expiry cost under realistic TTL distributions (lower is better)")
    ax.grid(True, axis="y", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "workload_expiry.png"), dpi=130)
    plt.close(fig)
    print("wrote workload_expiry.png")


def overflow_scale_plot():
    """Total memory vs. TTL span (results/memory_ttl_span.csv, n=100K, the
    main baseline) alongside the n=10M full sweep (memory_ttl_span_huge.csv),
    all four implementations, log-scale since the two baselines differ by
    ~2 orders of magnitude in absolute bytes."""
    small = by_algo(read("memory_ttl_span.csv"), "ttl_span", "mean_bytes")
    huge = by_algo(read("memory_ttl_span_huge.csv"), "ttl_span", "mean_bytes")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER:
        label, color, marker = STYLE[algo]
        if algo in small:
            x, y, _ = small[algo]
            pts = sorted(zip(x, y)); x, y = zip(*pts)
            ax.plot(x, y, marker=marker, color=color, linestyle="-", markersize=4,
                    linewidth=1.3, label=f"{label}, n=100K")
        if algo in huge:
            x, y, _ = huge[algo]
            pts = sorted(zip(x, y)); x, y = zip(*pts)
            ax.plot(x, y, marker=marker, color=color, linestyle="--", markersize=4,
                    linewidth=1.3, alpha=0.7, label=f"{label}, n=10M")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("TTL span (ticks)")
    ax.set_ylabel("total memory (bytes)")
    ax.set_title("Memory vs. TTL span at two baselines (lower is better)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "overflow.png"), dpi=130)
    plt.close(fig)
    print("wrote overflow.png")


def naive_overflow_scale_plot():
    """Naive ring's memory-vs-ttl_span overflow at two baselines, normalized
    to each series' own first point, to show the overflow effect weakening
    in relative terms. Lawn2 is plotted alongside as a flat control: it does
    not overflow at either baseline."""
    small = by_algo(read("memory_ttl_span.csv"), "ttl_span", "mean_bytes")
    huge = by_algo(read("memory_ttl_span_huge.csv"), "ttl_span", "mean_bytes")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ("naive", "lawn2"):
        label, color, marker = STYLE[algo]
        for baseline, d, linestyle, alpha in (("n=100K", small, "-", 1.0), ("n=10M", huge, "--", 0.7)):
            x, y, _ = d[algo]
            pts = sorted(zip(x, y)); x, y = zip(*pts)
            y0 = y[0]
            y_norm = [v / y0 for v in y]
            ax.plot(x, y_norm, marker=marker, color=color, linestyle=linestyle,
                    markersize=5, linewidth=1.5, alpha=alpha,
                    label=f"{label}, {baseline}")
    ax.axhline(1.0, color="k", linestyle=":", linewidth=1, label="no growth")
    ax.set_xscale("log")
    ax.set_xlabel("TTL span (ticks)")
    ax.set_ylabel("memory relative to smallest TTL span")
    ax.set_title("Naive ring's overflow weakens at scale (lower is better)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "naive_overflow_scale.png"), dpi=130)
    plt.close(fig)
    print("wrote naive_overflow_scale.png")


def density_vs_n_plot():
    """Tests whether N/ttl_span ('density') alone explains the wheel's
    empty-tick blowup, using the ttl_span sweep already collected at both
    baselines (results/tick_advance_ttl_span.csv, n=100K, and
    tick_advance_ttl_span_huge.csv, n=10M). Plots wahern's (and lawn2's, for
    scale reference) cost against actual density (N/ttl_span) for both
    baselines on one axis: if density were the sole driver, matched-density
    points from the two baselines would coincide."""
    small = by_algo(read("tick_advance_ttl_span.csv"), "ttl_span", "mean_ns")
    huge = by_algo(read("tick_advance_ttl_span_huge.csv"), "ttl_span", "mean_ns")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ("wahern", "lawn2"):
        label, color, marker = STYLE[algo]
        for baseline, n, d, linestyle, alpha in (("n=100K", BASE_N, small, "-", 1.0),
                                                  ("n=10M", BASE_N_HUGE, huge, "--", 0.7)):
            x, y, _ = d[algo]
            pts = sorted((n / span, cost) for span, cost in zip(x, y))
            dens, cost = zip(*pts)
            ax.plot(dens, cost, marker=marker, color=color, linestyle=linestyle, markersize=6,
                    linewidth=1.5, alpha=alpha, label=f"{label}, {baseline}")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("density  N / TTL span  (timers per tick, log scale)")
    ax.set_ylabel("empty-tick latency (ns)")
    ax.set_title("Density alone does not explain the wheel's cost (lower is better)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "density_vs_n.png"), dpi=130)
    plt.close(fig)
    print("wrote density_vs_n.png")


def lifecycle_plot():
    """Realistic mixed-workload latency vs n, main baseline (results/
    lifecycle_n.csv, 1K-1M): pre-fills n background timers, then times a
    fixed sequence of randomly interleaved insert/delete/tick operations,
    unlike every other figure here, which isolates one operation type.
    Solid = mean, dashed = p99, to show both typical and tail cost in one
    plot. Extended further in Section VII.G once (results/
    lifecycle_n_huge.csv) once machine memory pressure allowed it."""
    # Full population range: 1K-1M (lifecycle_n.csv) plus the extended
    # 2M-200M sweep (lifecycle_n_huge.csv) when present. Both halves use the
    # same fixed TTL span, so there is no density seam at the join.
    rows = read("lifecycle_n.csv")
    if os.path.exists(os.path.join(RESULTS, "lifecycle_n_huge.csv")):
        rows = rows + read("lifecycle_n_huge.csv")
    mean_d = by_algo(rows, "n", "mean_ns", "std_ns")
    p99_d = by_algo(rows, "n", "p99_ns")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER_ALL:
        if algo not in mean_d:
            continue
        label, color, marker = STYLE[algo]
        x, y, s = mean_d[algo]
        pts = sorted(zip(x, y, s)); x, y, s = zip(*pts)
        ax.errorbar(x, y, yerr=s, marker=marker, color=color, linestyle="-",
                    capsize=2, markersize=5, linewidth=1.3, label=f"{label}")
        xp, yp, _ = p99_d[algo]
        ptsp = sorted(zip(xp, yp)); xp, yp = zip(*ptsp)
        ax.plot(xp, yp, marker=marker, color=color, linestyle="--",
                markersize=4, linewidth=1.0, alpha=0.6)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("number of background timers")
    ax.set_ylabel("per-operation latency (ns)")
    ax.set_title("Realistic mixed workload across scale: mean (solid) vs p99 (dashed)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "lifecycle.png"), dpi=130)
    plt.close(fig)
    print("wrote lifecycle.png")


def extended_lifecycle_plot():
    """Realistic mixed-workload latency vs n, extended to one hundred
    million background timers (results/lifecycle_n.csv +
    lifecycle_n_huge.csv). Same mean (solid) / p99 (dashed) convention as
    Figure~lifecycle. Confirms the main-baseline finding holds at scale:
    every implementation except the cascading wheel stays flat."""
    rows = read("lifecycle_n.csv") + read("lifecycle_n_huge.csv")
    mean_d = by_algo(rows, "n", "mean_ns", "std_ns")
    p99_d = by_algo(rows, "n", "p99_ns")
    fig, ax = plt.subplots(figsize=(6.4, 4.2))
    for algo in ORDER_ALL:
        if algo not in mean_d:
            continue
        label, color, marker = STYLE[algo]
        x, y, s = mean_d[algo]
        pts = sorted(zip(x, y, s)); x, y, s = zip(*pts)
        ax.errorbar(x, y, yerr=s, marker=marker, color=color, linestyle="-",
                    capsize=2, markersize=4, linewidth=1.2, label=f"{label}")
        xp, yp, _ = p99_d[algo]
        ptsp = sorted(zip(xp, yp)); xp, yp = zip(*ptsp)
        ax.plot(xp, yp, marker=marker, color=color, linestyle="--",
                markersize=3, linewidth=0.9, alpha=0.6)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("number of background timers (extended past 1M)")
    ax.set_ylabel("per-operation latency (ns)")
    ax.set_title("Realistic mixed workload to 200M: mean (solid) vs p99 (dashed)")
    ax.grid(True, which="both", alpha=0.3)
    ax.legend(fontsize=7, ncol=2)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, "extended_lifecycle.png"), dpi=130)
    plt.close(fig)
    print("wrote extended_lifecycle.png")


def main():
    axis_plot("insert_n.csv", "n", "mean_ns", "std_ns",
              "number of timers", "insert latency (ns)",
              "Insertion latency vs scale (lower is better)", "insertion.png")
    axis_plot("delete_n.csv", "n", "mean_ns", "std_ns",
              "number of timers", "delete latency (ns)",
              "Deletion latency vs scale (lower is better)", "deletion.png")
    axis_plot("memory_per_timer_n.csv", "n", "mean_bytes", "std_bytes",
              "number of timers", "bytes per timer",
              "Memory per timer vs scale (lower is better)", "memory.png", logy=False)
    axis_plot("expiry_n.csv", "n", "mean_ns", "std_ns",
              "number of timers", "per-tick expiry latency (ns)",
              "Expiry-tick cost vs scale (lower is better)", "expiry.png")
    overflow_scale_plot()
    inflection_plot()
    concurrency_plot()
    extended_tick_plot()
    extended_memory_plot()
    density_vs_n_plot()
    naive_overflow_scale_plot()
    workload_expiry_plot()
    lifecycle_plot()
    extended_lifecycle_plot()
    print("done")


if __name__ == "__main__":
    main()
