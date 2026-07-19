# Python timer benchmark suite

Algorithm-agnostic harness measuring timer data structures on an integer logical
clock. Absolute latencies carry Python interpreter overhead, so this suite is for
**scaling shape and jitter**, not C-grade absolute numbers (use `../c/` for
those).

## Layout

- `timerstore.py` - `TimerStore` protocol + `ALGORITHMS` registry + memory helper.
- `lawn_store.py`, `timerwheel_store.py`, `naive_wheel_store.py` - the three
  adapters (Lawn; hierarchical cascading wheel; single-level growing ring).
- `measurements.py` - one function per operation + the workload generator.
- `experiment.py` - `ExperimentManager`: sweep, aggregate, CSV, error-bar plots.
- `run_benchmarks.py` - CLI: 5 operations x 4 axes -> CSV + PNG.
- `inflection.py` - distinct-TTL / timer-count (t/N) crossover study.
- `test_correctness.py` - differential correctness gate (run this first).

## Run

```bash
python -m venv .venv && . .venv/bin/activate && pip install -r requirements.txt
python test_correctness.py          # must pass before trusting numbers
python run_benchmarks.py --quick     # small smoke run; drop --quick for the full set
python inflection.py
```

Outputs land in `results/` (git-ignored). Only dependency is `matplotlib`;
everything else is the standard library.

## Adding an algorithm

Create a class implementing the `TimerStore` protocol (`start_timer`,
`stop_timer`, `per_tick_bookkeeping`, `size`) and call `register("name", Factory)`.
The measurement, sweep, and plotting code needs no changes.
