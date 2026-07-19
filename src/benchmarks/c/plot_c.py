"""Render the C-suite CSVs into PNGs, reusing the Python harness plot code so the
C graphs match the Python ones. Run with the venv python (has matplotlib)."""
import csv
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "python"))
from experiment import ExperimentManager, Point  # noqa: E402

RESULTS = os.path.join(HERE, "results_c")
LOGX_AXES = {"n", "ttl_span", "distinct_ttls"}
NUMERIC_AXES = {"n", "ttl_span", "distinct_ttls"}


def load(path):
    axis = None
    pts = []
    with open(path) as f:
        r = csv.reader(f)
        head = next(r)
        axis = head[0]
        unit = "bytes" if head[2].endswith("bytes") else "ns"
        for row in r:
            val = int(row[0]) if axis in NUMERIC_AXES else row[0]
            pts.append(Point(val, row[1], float(row[2]), float(row[3]),
                             float(row[4]), float(row[5]), int(row[6])))
    return axis, unit, pts


def plot_standard():
    for path in sorted(glob.glob(os.path.join(RESULTS, "*.csv"))):
        base = os.path.basename(path)[:-4]
        if base == "inflection":
            continue
        axis, unit, pts = load(path)
        op = base.rsplit("_" + axis, 1)[0] if base.endswith(axis) else base
        ylabel = f"{op} ({unit})"
        logy = True
        if base == "memory_per_timer_n":
            ylabel, logy = "bytes per timer", False
        ExperimentManager.plot(
            pts, os.path.join(RESULTS, base + ".png"), axis, ylabel,
            f"[C] {op} vs {axis}", logx=(axis in LOGX_AXES), logy=logy)
    print("standard C plots written")


def plot_inflection():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    rows = list(csv.DictReader(open(os.path.join(RESULTS, "inflection.csv"))))
    ns = sorted({int(r["N"]) for r in rows})
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for n in ns:
        sub = [r for r in rows if int(r["N"]) == n]
        xs = [int(r["t"]) for r in sub]
        ax.plot(xs, [float(r["ratio_lawn_over_wahern"]) for r in sub],
                marker="o", linestyle="--", alpha=0.5, label=f"lawn  N={n:,}")
        ax.plot(xs, [float(r["ratio_lawn2_over_wahern"]) for r in sub],
                marker="o", label=f"lawn2 N={n:,}")
    ax.axhline(1.0, color="grey", linestyle=":", label="parity (= wahern)")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("distinct TTL count  t")
    ax.set_ylabel("lifecycle cost ratio (Lawn / wahern)")
    ax.set_title("[C] Lawn & lawn2 vs wahern: full-lifecycle cost by distinct-TTL count")
    ax.legend(); ax.grid(True, which="both", alpha=0.3)
    fig.tight_layout(); fig.savefig(os.path.join(RESULTS, "inflection.png"), dpi=120)
    plt.close(fig)
    print("inflection C plot written")


if __name__ == "__main__":
    plot_standard()
    plot_inflection()
    print("PNGs in", RESULTS)
