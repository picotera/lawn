# Plan: Fix the Lawn article's crossover/lifecycle figures

## TODO (prose deliberately not written yet, per user instruction)

**MAJOR UPDATE (superseding the "corrected story" notes further below):** the bulk-load
preload in `measure_lifecycle` was itself an artifact. Inserting all N background
timers at t=0 collapses every deadline onto the t distinct TTL values, so a wheel
populates only ~t slots (its cheap case), manufacturing BOTH the "no Lawn advantage at
low t" reading AND the deterministic multi-millisecond cascade stalls (up to 49ms at
N=10M) that drove the whole mean-vs-p99/two-panel narrative. Fixed by staggering the
preload's arrival times (commit `39afb63`): under staggering the wheel's mean and p99
now agree (no more tail-dominated mean), the cascade stalls vanish, and low-t points
that looked like parity under bulk load show a real 3-11x Lawn2 win. All prior "mean
panel = Lawn stability benefit" / "49ms tail stall" language is RETIRED — do not use it.

Before writing prose:
1. DONE — regenerated `inflection.csv` (now to N=100M) and the huge lifecycle population
   sweep (to 200M) under staggering (commit `1ceb034`; `bench.c` staggering itself in
   `39afb63`).
2. DONE — re-inspected the raw crossover numbers under staggering. Two panels were kept
   (not collapsed to one), but redesigned twice since the original two-panel version:
   - Left panel: RAW mean latency (ns), Lawn2 solid vs Wheel dashed, x-axis = raw
     distinct-TTL count `t` (commit `a8c13b8`, then `4c1ca06`). Not a ratio — a
     Wheel/Lawn2 ratio amplifies noise when the denominator swings, which is what made
     an earlier ratio-based mean panel zigzag.
   - Right panel: p99 (typical-cost) speedup ratio Wheel/Lawn2, x-axis = distinct-TTL
     **fraction t/N (%)**, not raw t (commit `4c1ca06`).
3. DONE — investigated the p99 panel's flat ratio=1.0 plateau at N=1K/10K/100K (low t).
   Root cause: at these (N,t), BOTH algorithms' true per-op cost is below the ~41.667ns
   Apple-Silicon clock quantum (24MHz mach clock), so p99 reads exactly one tick (42.00)
   for both, for the ENTIRE t range tested at these three N values, not just the
   apparent "plateau" — wahern's p99 never leaves the clock floor at N<=100K regardless
   of t. Confirmed via a standalone diagnostic (scratch, not in the repo) that batching K
   consecutive lifecycle iterations together (K=1/4/16/64, mirroring bench.c's own BATCH
   trick used by the OTHER measure_* functions) does NOT fix this: it destroys the tail
   signal (wahern's real max at a tail-heavy point shrunk ~48x at K=64, diluted by the
   K-1 cheap iterations averaged alongside it) AND makes the percentile estimate noisier,
   not better (fewer total samples at high K: 35,000 -> 553 at K=64). Mean is unaffected
   by K in both regimes (already unbiased). CONCLUSION: no code fix needed or wanted —
   the two-panel design already has the right complementary metric for each regime: the
   MEAN panel is trustworthy at N<=100K (where p99 is floor-locked and uninformative),
   and the P99 panel is trustworthy at N>=1M (where real cost clears the clock floor and
   p99 genuinely varies with t). When prose is written, state which panel is authoritative
   in which N regime, rather than treating the flat p99 plateau at small N as a genuine
   "no advantage" finding.
4. Explored two more figure variants (t/N% x-axis for the raw-mean panel; a violin plot
   of raw per-op samples). t/N% for the mean panel didn't reveal anything the raw-t
   version didn't already show more directly (discarded). The violin plot confirmed the
   mean-panel zigzag is tail-driven (near-identical distribution body across t=10/20/50
   at N=1M, only the max swinging) but was reverted per user instruction (violin code +
   the `RAW_DUMP_FILE` diagnostic hook that produced it were both removed, commit
   `2f4563c`) — not needed for the article.
5. THEN write the crossover subsection, fix the caption, reconcile abstract/limitations/
   conclusion numbers, and explain the mean-vs-p99 panel split (per item 3) — all still
   pending, not started.

## Context (historical — the two original figure defects that started this round)

The article rewrite is landed and pushed (`38a5b9a`). Reviewing its 7 figures against
the data surfaced two that are wrong, both rooted in the lifecycle/inflection
measurement parameterization:

1. **`inflection.png` is senseless.** It plots the per-tick (tick_scan) cost ratio
   `wahern/lawn2` vs `t/N%` on a linear y-axis. The result is dominated by a spike to
   ~111x at `t/N ≈ 2%` for the two smaller N, with every other point crushed against
   the parity line so no crossover is readable. The article caption calls it "Lifecycle
   speedup," but the data is per-tick — a caption/data mismatch.
2. **`lifecycle.png` is confounded.** Its x-axis is labeled "number of background
   timers (1K-1M)," but in `measure_lifecycle` the swept `p.n` is the count of *timed
   foreground operations*; the background population is fixed at `BASE_N`=100K and then
   **jumps to 10M at the last point** (the `p.n >= 10*BASE_N` regime switch at
   bench.c:225). The orange wheel spike at 1M is partly that population discontinuity,
   not smooth scaling, and the axis label is wrong.

The user wants a true **lifecycle-cost crossover**: Lawn2 vs Timer Wheel, across
population N = 1K..10M and growing distinct-TTL count, showing the inflection point
where Lawn2 stops beating the wheel. And to fix `lifecycle.png` while we're here.

**Decisions (confirmed with the user):**
- New figure = speedup ratio `wahern/lawn2` on a **log y-axis** with a parity line,
  one line per N, x = distinct-TTL count. Log-y is the fix for the exact squashing that
  ruined the old figure.
- **Replace** `inflection.png` with this lifecycle version (stay at 7 figures).
- **Fix `lifecycle.png`** this round (re-run with an explicit population per point).

## Figure review verdict

| Figure | Verdict |
|---|---|
| `overflow.png` | Keep. Clear (thesis: naive overflows, others flat). |
| `memory.png` | Keep. Clear (Lawn2 ~48B vs wheel ~88B at scale). |
| `extended_tick.png` | Keep. The killer graph (wheel to millions of ns, others flat). |
| `deletion.png` | Keep. Clear (Lawn2 below wheel throughout). |
| `workload_expiry.png` | Keep. Clear bar chart, Lawn2 << wheel on all 4 distributions. |
| `inflection.png` | **Replace** with the lifecycle crossover (below). |
| `lifecycle.png` | **Fix** the x-axis confound (below). |

(The other figures `make_figures.py` still emits — `extended_lifecycle`,
`extended_memory`, `density_vs_n`, `naive_overflow_scale`, `concurrency`, `insertion`,
`expiry` — are not referenced by the rewritten article; leave them alone.)

## Prerequisite: finalize the paused cleanup agent

The benchmark-cleanup agent is paused mid-Phase-3 with uncommitted `bench.c`/`Makefile`
edits (the 5 warning fixes + `-Wextra`) and one pending decision. Its proposed
`BASE_PARAMS` resolution — rewriting the one call site to `params_t base_params =
{BASE_N, BASE_SPAN, 100, WL_UNIFORM, 0};` (macro definition untouched) — is
behavior-preserving and correct; approve it. Let the agent run `./test`, commit Phase 3,
and do Phase 4 (README). `bench.c` must be committed-clean before adding new code on top.

## Work items

### A. Extend the EXISTING `run_inflection` (no new subcommand)
"lifecycle" is the renamed "stability" op, and `run_inflection` (bench.c:668) already
loops the exact `(N, distinct)` grid for lawn2/wahern — it just measures per-tick scan
cost. Extend it to ALSO measure the lifecycle op; do not add a subcommand.
- Change `NS[]` to `{1000, 10000, 100000, 1000000, 10000000}` (add 1K).
- Add `#define FG_OPS 5000` (fixed timed foreground-op count).
- At each `(N, t)`, alongside the existing `mean_tick_scan` calls, measure lifecycle for
  lawn2 and wahern: `params_t lp = {FG_OPS, BASE_SPAN_HUGE, t, WL_UNIFORM, N};`
  (population = N via `preload_n`; span 65536 >= max t), then
  `run_point(algo, measure_lifecycle, lp, OP_PER_N).mean`. Guard with
  `memory_ok("lifecycle", algo, FG_OPS, N, ...)`; skip + notice if it fails.
- Add 3 columns to `inflection.csv`: `lawn2_life_ns,wahern_life_ns,ratio_wahern_over_lawn2_life`.
  Keep all existing per-tick columns. Only run the lifecycle measurements in the
  `scale_span==true` call (inflection.csv), not the fixed_span call, so the heavy grid
  isn't measured twice.

Reuses `run_inflection`'s loop/algo-lookup/crossover math and `run_point`/
`measure_lifecycle`/`memory_ok` unchanged — no new op, no new subcommand.

### B. Fix `lifecycle.png`'s parameterization
The lifecycle-vs-scale sweep must sweep the **population**, not the foreground-op count.
In `sweep_axis` (bench.c:529), for `op->name == "lifecycle"` on `axis == "n"`, set
`p.preload_n = <axis value>` and `p.n = FG_OPS` (fixed), instead of setting `p.n = axis
value`. This makes `lifecycle_n.csv` / `lifecycle_n_huge.csv` genuinely
population-swept, removing the 100K->10M discontinuity, and makes the existing
"number of background timers" axis label true. Keep the change scoped to the lifecycle
op so no other sweep is affected. Re-verify the resulting curve is monotone (no 1M jump).

### C. `article/src/make_figures.py`
- Repoint `inflection_plot()` to read the LIFECYCLE columns of `inflection.csv`
  (`wahern_life_ns`/`lawn2_life_ns`, added in item A) and plot: x = distinct count `t`
  (log), y = `wahern_life_ns / lawn2_life_ns` (**log**), one line per N (skip sentinel
  rows), dashed parity at 1.0. Title: "Lifecycle cost: Lawn2 vs Timer Wheel across
  distinct-TTL count." Keep the output filename `inflection.png` so `lawn.tex` is
  unchanged. (The per-tick columns remain in the CSV, just unplotted.)
- Confirm `lifecycle_plot()` still reads `lifecycle_n.csv` and that its
  "background timers" label is now accurate after item B.

### D. Run the sweeps (user runs — long)
Per the standing rule, the user launches the long benchmarks; I regenerate figures after:
```
cd src/benchmarks/c && make
./bench inflection                  # -> inflection.csv now incl. lifecycle columns (10M rows heavy: tens of min)
./bench sweep-op lifecycle n        # -> results/lifecycle_n.csv (+ _huge)  with the fixed population sweep
```
Then I run `python3 article/src/make_figures.py` and inspect the raw CSVs before
trusting them (same discipline as prior sweeps).

### E. Article prose (Opus agent) — measured findings to write from
- **Present BOTH metrics (two-panel `inflection.png`), per user:**
  - *Mean panel (left):* Lawn2's mean stays stable while the wheel's mean is inflated by
    rare O(N/t) cascade stalls, so Lawn2's mean-cost lead widens with scale. Frame this
    as Lawn's stability/no-stall benefit, NOT as a naive "wins everywhere" (the "wins
    everywhere at N>=1M" reading is a window artifact: the mean does not converge with
    window length; a 50000-tick window brings the 1M crossover back near t=500).
  - *p99 panel (right):* the honest typical-cost crossover, ~**t = 20-100 distinct TTLs,
    roughly scale-independent** (N=10K/100K cross near t=20, N=1M near t=100). Lawn2's
    typical cost rises with t (O(t) scan); the wheel's p99 is flat.
  - Both REPLACE the old per-tick claims (abstract "2 to 6%", Limitations "0.71 to
    6.4%", counts 643/2234/12920/71407). Confirm exact per-N crossover t from the full
    1K-10M re-run.
- **Tail/max callout (required, per user):** the wheel's mean lifecycle cost is
  dominated by rare, *deterministic* cascade stalls — p99 stays ~200-583 ns while a
  handful of ticks hit 1.7 ms (1M) to 49 ms (10M) as one boundary crossing cascades
  ~N/t timers down the hierarchy. Lawn2 is flat (~16-20 ns) with a stable p99 and no
  such tail. State this explicitly so Lawn's no-stall advantage lands; it also explains
  the crossover figure's metric choice (see item C decision below).
- Fix the `fig:inflection` caption to say lifecycle (not per-tick).
- Update `sec:lifecycle` numbers: the population sweep now uses TTL span 65536 (not
  1024) and extends to ~100M; regenerated `lifecycle.png` shows Lawn2 flat and the wheel
  climbing to ~388 us at 100M with no baseline discontinuity.
- Rebuild the PDF (`tectonic`), no-ai-slop self-check, send to user before any push.

**Open decision (pending the metric-preview render):** whether the crossover figure
plots the ratio of means, p99, or a longer-window mean. p99 is window-insensitive and
reproducible; the mean is cascade-tail-dominated and does not converge with window
length (cascade period >> feasible window). If p99 is chosen, `run_inflection` must be
extended to record p99 (and max) for the lifecycle columns, then re-run.

## Verification
- `cd src/benchmarks/c && make clean && make` builds with `-Wall -Wextra`, zero warnings;
  `./test` prints `ALL C CORRECTNESS TESTS PASSED`.
- `results/lifecycle_inflection.csv` has a row for every `(N, t)` with `t <= N`; ratios
  are finite and the per-N crossover is detected. Raw numbers inspected directly.
- Regenerated `inflection.png` shows readable per-N parity crossings on the log axis (no
  squashing, no lone spike).
- Regenerated `lifecycle.png` is monotone in population with no 1M discontinuity; x-axis
  genuinely means background population.
- `tectonic lawn.tex` builds with zero undefined refs; PDF visually reviewed and sent to
  the user before pushing.

## Execution (agents + models)
- **Code (items A, B, C)** — a **Sonnet** agent: mechanical C/Python edits, build+gate verified, local commits, no push.
- **Article prose (item E)** — an **Opus** agent: the crossover-section rewrite and the reconciled abstract/limitations/conclusion numbers (prose quality + adam-voice/no-ai-slop).
- **Dependency**: item E cannot start until (1) items A-C are committed and (2) the user has run the sweeps (item D), because the prose must quote the new lifecycle crossover numbers. Order is: A-C (Sonnet) -> D (user runs) -> regenerate figures + E (Opus).
- **Note**: any agent can only make edits once plan mode is exited (plan mode propagates to sub-agents).

## Files
- `src/benchmarks/c/bench.c` — extend `run_inflection` with lifecycle columns + `FG_OPS`
  define + N=1K; scope the lifecycle `n`-axis to sweep `preload_n` in `sweep_axis`.
- `article/src/make_figures.py` — repoint `inflection_plot()` to the lifecycle columns.
- `article/lawn.tex` — crossover subsection + caption + reconciled numbers.
- Data (regenerated by the user's runs): `inflection.csv` (now with lifecycle columns),
  `results/lifecycle_n.csv` (+ `_huge`).
