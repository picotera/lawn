"""Experiment manager: owns the sweep loop, aggregation, CSV, and plotting.

The measurement functions run a single (algorithm, parameter-point). The manager
sweeps one axis while holding the others at baseline, repeats each point over
`runs` seeds (after discarding `warmup` seeds), aggregates the raw per-op samples
into mean / jitter-stddev / p99 / max, writes a CSV, and renders one PNG per
(operation, axis) with algorithms as lines and jitter as error bars.
"""
from __future__ import annotations

import csv
import statistics
from dataclasses import dataclass
from typing import Callable, Dict, List

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from timerstore import TimerStore, percentile  # noqa: E402

SUBSAMPLE_CAP = 200_000  # cap samples fed to the percentile sort; max uses all

Factory = Callable[[], TimerStore]
MeasureFn = Callable[..., List[float]]


@dataclass
class Sweep:
    axis: str            # "n" | "ttl_span" | "distinct_ttls" | "workload"
    values: list
    baseline: dict       # fixed values for the non-swept params


@dataclass
class Point:
    value: object        # the swept axis value
    algo: str
    mean: float
    std: float           # jitter: stddev of the per-op distribution
    p99: float
    maxv: float
    n_samples: int


class ExperimentManager:
    def __init__(self, algorithms: Dict[str, Factory], runs: int = 7,
                 warmup: int = 2, seed: int = 1234) -> None:
        self.algorithms = algorithms
        self.runs = runs
        self.warmup = warmup
        self.seed = seed

    def run(self, measure_fn: MeasureFn, sweep: Sweep) -> List[Point]:
        points: List[Point] = []
        for value in sweep.values:
            params = dict(sweep.baseline)
            params[sweep.axis] = value
            for name, factory in self.algorithms.items():
                combined: List[float] = []
                for r in range(self.warmup + self.runs):
                    samples = measure_fn(factory, seed=self.seed + r, **params)
                    if r >= self.warmup:
                        combined.extend(samples)
                points.append(self._aggregate(value, name, combined))
        return points

    @staticmethod
    def _aggregate(value: object, algo: str, combined: List[float]) -> Point:
        if not combined:
            return Point(value, algo, 0.0, 0.0, 0.0, 0.0, 0)
        mean = statistics.fmean(combined)
        std = statistics.pstdev(combined) if len(combined) > 1 else 0.0
        maxv = max(combined)  # true max over the full set
        sample = combined
        if len(sample) > SUBSAMPLE_CAP:
            sample = sample[::len(sample) // SUBSAMPLE_CAP]
        p99 = percentile(sorted(sample), 99)
        return Point(value, algo, mean, std, p99, maxv, len(combined))

    def to_csv(self, points: List[Point], path: str, axis: str, unit: str) -> None:
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow([axis, "algo", f"mean_{unit}", f"std_{unit}",
                        f"p99_{unit}", f"max_{unit}", "n_samples",
                        "runs", "warmup", "seed"])
            for p in points:
                w.writerow([p.value, p.algo, f"{p.mean:.4f}", f"{p.std:.4f}",
                            f"{p.p99:.4f}", f"{p.maxv:.4f}", p.n_samples,
                            self.runs, self.warmup, self.seed])

    @staticmethod
    def plot(points: List[Point], path: str, axis: str, ylabel: str,
             title: str, logx: bool = False, logy: bool = True) -> None:
        by_algo: Dict[str, List[Point]] = {}
        for p in points:
            by_algo.setdefault(p.algo, []).append(p)

        categorical = not all(isinstance(p.value, (int, float)) for p in points)
        fig, ax = plt.subplots(figsize=(7.0, 4.5))
        for name, pts in by_algo.items():
            labels = [p.value for p in pts]
            xs = list(range(len(pts))) if categorical else labels
            ys = [p.mean for p in pts]
            errs = [p.std for p in pts]
            container = ax.errorbar(xs, ys, yerr=errs, marker="o", capsize=3,
                                    label=name)
            color = container[0].get_color()
            ax.plot(xs, [p.p99 for p in pts], linestyle="--", alpha=0.4,
                    color=color)
            if categorical:
                ax.set_xticks(xs)
                ax.set_xticklabels([str(v) for v in labels])

        if not categorical and logx:
            ax.set_xscale("log")
        if logy:
            ax.set_yscale("log")
        ax.set_xlabel(axis)
        ax.set_ylabel(ylabel)
        ax.set_title(title)
        ax.legend(title="algorithm (dashed = p99)")
        ax.grid(True, which="both", alpha=0.3)
        fig.tight_layout()
        fig.savefig(path, dpi=120)
        plt.close(fig)
