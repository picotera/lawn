# Benchmark Harness Bugfixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the eight issues in `/tmp/lawn-bug-report.md` (against `picotera/lawn` master, commit `23aa599`+): a correctness bug that invalidates `linuxwheel`'s lifecycle numbers, the test gap that let it through, a misleading implementation name, a measurement cap that silently limits a sweep, a non-reproducible memory-skip gate, a missing timestamp column, an unsupported article claim, and bibliography/repo hygiene.

**Architecture:** No architectural change. Every fix is local: one adapter's `advance` function, one test function, one rename across ~7 reference points, two code comments, one CSV column addition (with a matching Python-side filter), one more CSV column, three sentences in `article/lawn.tex`, and one stray file deletion.

**Tech Stack:** C11 (`src/benchmarks/c/`), Python 3 + `csv` + `matplotlib` (`article/src/make_figures.py`), LaTeX (`article/lawn.tex`).

## Global Constraints

- No AI/model attribution anywhere (commits, code comments, article text) — see `[[no-ai-attribution]]`.
- This is `picotera/lawn` (the public repo). Never copy anything from `/Users/adamll/src/lawn-experimental` into this checkout — see `[[never-leak-experimental-to-public-lawn]]`. Nothing in this plan requires it; every fix here is self-contained to this repo's own state.
- Every step that changes `src/benchmarks/c/*` must end with `cd src/benchmarks/c && make clean && make test && ./test` passing (`ALL C CORRECTNESS TESTS PASSED`) before moving on.
- Don't fabricate numbers. Where the report says a claim is unsupported and re-measuring is out of scope, soften the prose — never invent a replacement figure.
- Isolate the actual editing in a fresh git worktree per the workdir policy (`EnterWorktree`, no `path` arg) before Task 1, and bring the result back to the main checkout the same way this session has been doing it all along (the worktree branches from `origin/master` fresh, so if it's missing anything the main checkout currently has, copy those specific files in before starting, per this session's established pattern — check `git status` in both places first).

---

### Task 1: Write a registry-wide, full-sequence `advance` differential test (replaces the broken one)

**Files:**
- Modify: `src/benchmarks/c/test.c:97-131` (the existing `advance_matches_tick` function and its call site in `main()` at line 192)

**Interfaces:**
- Consumes: `cts_algos`/`cts_nalgos` (`cts.h`), each `cts_vtable`'s `create`/`destroy`/`start`/`tick`/`size`/`advance` fields, `gen_ttls` (`util.h`).
- Produces: nothing new consumed by later tasks — this is the regression test Task 2 must make pass.

**Why this shape:** the report's three gaps are (1) hardcoded to `lawn2` instead of looping `cts_algos`, (2) exercises `lawn2_advance` (a library function no adapter calls) instead of `vt->advance`, (3) only compares totals/state immediately after the jump, which is exactly the instant `linuxwheel`'s bug is invisible at. The new version fixes all three: loop every adapter with non-NULL `advance`, call it through the vtable, and keep ticking control (tick-only) and subject (advance-then-tick) stores in lockstep afterward, comparing the full per-tick fired sequence, not just a final snapshot.

- [ ] **Step 1: Replace `advance_matches_tick` with the registry-wide version**

Replace the entire function (lines 97-131 of `src/benchmarks/c/test.c`) with:

```c
/* vt->advance must produce identical wheel state to reaching the same point
 * via repeated vt->tick() calls, not just matching totals right after the
 * jump: a cascading structure that strands a timer in the wrong (level,
 * slot) still reports a correct size() and a correct fired-so-far count at
 * that instant, and only diverges once ticking continues (this is exactly
 * how impl/linuxwheel.c's lw_advance bug survived the old, lawn2-only,
 * snapshot-only version of this test). For every adapter exposing a
 * non-NULL advance, build two stores with an identical staggered arrival
 * set -- control reaches each arrival via tick(), subject via advance(),
 * exactly mirroring pre_lifecycle's own staggering -- then tick both
 * forward together past every deadline and assert every single per-tick
 * fired count and live size match, not just the final ones. */
static void advance_matches_tick(void) {
    const size_t n = 2000;
    const uint64_t ttl_span = 20000;
    const uint64_t stagger_w = 10000;   /* below every ttl: nothing due during preload */
    const uint64_t run_ticks = 50000;   /* past every deadline (max is stagger_w+ttl_span), plus slack */

    uint64_t *ttls = malloc(n * sizeof *ttls);
    gen_ttls(ttls, n, ttl_span, 200, WL_UNIFORM, 11);

    int checked = 0;
    for (int a = 0; a < cts_nalgos; a++) {
        const cts_vtable *vt = cts_algos[a];
        if (!vt->advance) continue;
        checked++;

        cts_store *ctrl = vt->create();
        cts_store *subj = vt->create();

        uint64_t ctrl_now = 0;
        for (size_t i = 0; i < n; i++) {
            uint64_t arrival = (uint64_t)(((double)i / (double)n) * (double)stagger_w);
            while (ctrl_now < arrival) { vt->tick(ctrl); ctrl_now++; }
            vt->advance(subj, arrival);
            vt->start(ctrl, i, ttls[i]);
            vt->start(subj, i, ttls[i]);
        }
        while (ctrl_now < stagger_w) { vt->tick(ctrl); ctrl_now++; }
        vt->advance(subj, stagger_w);

        for (uint64_t t = 0; t < run_ticks; t++) {
            uint64_t fc = vt->tick(ctrl);
            uint64_t fs = vt->tick(subj);
            if (fc != fs || vt->size(ctrl) != vt->size(subj)) {
                fprintf(stderr,
                        "FAIL advance_matches_tick: %s tick %llu: ctrl_fired=%llu subj_fired=%llu "
                        "ctrl_size=%llu subj_size=%llu\n",
                        vt->name, (unsigned long long)t,
                        (unsigned long long)fc, (unsigned long long)fs,
                        (unsigned long long)vt->size(ctrl), (unsigned long long)vt->size(subj));
                exit(1);
            }
        }
        vt->destroy(ctrl);
        vt->destroy(subj);
    }
    free(ttls);
    printf("  advance_matches_tick: %d impls' advance() reproduces the full tick-driven "
           "expiry sequence over %llu ticks after a staggered preload\n",
           checked, (unsigned long long)run_ticks);
}
```

Note `#include "lawn2.h"` and `#include "impl/lawn2_clamped.h"` at the top of the file are no longer needed by this function specifically, but leave them: `clamp_math`/`clamp_wiring` later in the file still use `clamp_timer_ttl` from `impl/lawn2_clamped.h`, and nothing else in the file uses `lawn2.h` directly any more after this change -- check with `grep -n "lawn2_advance\|lawn2_tick\|lawn2_new\|lawn2_add\|lawn2_free\|timer_for\|init_store\|destroy_store" src/benchmarks/c/test.c`; if that grep now returns nothing, remove the now-unused `#include "lawn2.h"` line (line 5) to avoid an unused-include warning under `-Wextra`. Keep `#include "impl/lawn2_clamped.h"` regardless.

- [ ] **Step 2: Build and run — expect a FAIL on `linuxwheel`, confirming the test catches the real bug**

```bash
cd src/benchmarks/c && make clean && make test
```

Expected: builds with zero warnings (matches the current `-Wall -Wextra` setup). Then:

```bash
./test
```

Expected: **fails** with a line starting `FAIL advance_matches_tick: linuxwheel tick <small number>: ...` (the report's own reproduction diverged by tick 3). If it fails on a *different* adapter, or passes outright, stop and re-read Task 1's Step 1 code against the current `cts_vtable` definition in `cts.h` (field order matters: `create, destroy, start, stop, tick, size, advance`) before continuing -- don't proceed to Task 2 on a mismatched assumption.

- [ ] **Step 3: Commit the test change alone, before touching `linuxwheel.c`**

```bash
git add src/benchmarks/c/test.c
git commit -m "test: make advance_matches_tick cover the whole registry and the full post-jump tick sequence

Replaces the lawn2-only, lawn2_advance-only, snapshot-only version. The old
test exercised a library function (lawn2_advance) no adapter actually
calls, checked only one adapter, and compared state only at the instant
right after the jump -- exactly when a cascading structure that stranded a
timer in the wrong slot still looks correct. This version loops every
vt->advance in the registry and compares the full per-tick fired sequence
against a tick-only control, which is what actually catches
impl/linuxwheel.c's advance bug (fixed next commit)."
```

This commit is expected to leave the test suite failing (`./test` exits 1). That's fine and intentional -- it's the red half of red/green, isolated in its own commit so the fix in Task 2 is a clean, reviewable diff against a known-failing baseline.

---

### Task 2: Fix `linuxwheel.c`'s `lw_advance` to process every cascade the jump crosses

**Files:**
- Modify: `src/benchmarks/c/impl/linuxwheel.c:139-142`

**Interfaces:**
- Consumes: the file's own already-correct `lw_tick` (unchanged).
- Produces: a `lw_advance` whose behavior is provably identical to calling `lw_tick` `(target - s->now)` times, satisfying Task 1's test.

**Why this shape:** `lw_tick`'s cascade logic (lines 113-135) is already correct per-tick -- the report itself confirms every adapter's `tick()` reproduces the reference schedule byte for byte. The bug is only that `lw_advance` skips straight to `now = target` without running that logic for every tick in between. The simplest fix that is correct by construction is to call the already-correct `lw_tick` in a loop; this is exactly what `wahern_advance` delegates to `timeouts_update` for, just without a bulk-update shortcut, since `linuxwheel` has no such library function. This costs O(elapsed) instead of O(levels), but `vt->advance` is only ever called from `pre_lifecycle` (`benchmark.c:396-431`), which is the *untimed* setup phase before the timed `payload_lifecycle` runs -- confirmed by reading `pre_lifecycle`: `vt->advance` appears only inside the loop that runs before `payload_lifecycle` is ever called. So this has zero effect on any reported per-op latency number.

- [ ] **Step 1: Replace `lw_advance`**

In `src/benchmarks/c/impl/linuxwheel.c`, replace:

```c
/* Jump the clock forward with no expiry/cascade processing (used for a
 * staggered preload, where target stays below every live deadline so
 * nothing is due and no level boundary is crossed). */
static void lw_advance(cts_store *s, uint64_t target) { s->now = target; }
```

with:

```c
/* Jump the clock forward to target by running the already-correct lw_tick
 * once per elapsed tick, so every cascade boundary the jump crosses is
 * processed exactly as it would be under tick-by-tick advancement. Costs
 * O(target - now) rather than O(levels), but advance() is only ever called
 * from pre_lifecycle's untimed preload setup (benchmark.c), never from a
 * timed payload, so this has no effect on any reported latency. Anything
 * that fires during the jump (shouldn't happen under the staggered-preload
 * precondition that target stays below every live deadline, but handled
 * defensively) is drained and counted down in live via lw_tick's own
 * bookkeeping, mirroring wahern_advance's same defensive drain. */
static void lw_advance(cts_store *s, uint64_t target) {
    while (s->now < target) lw_tick(s);
}
```

- [ ] **Step 2: Build and run — expect PASS on every adapter**

```bash
cd src/benchmarks/c && make clean && make test && ./test
```

Expected output includes (among the other passing lines):
```
  advance_matches_tick: 7 impls' advance() reproduces the full tick-driven expiry sequence over 50000 ticks after a staggered preload
ALL C CORRECTNESS TESTS PASSED
```
(the count `7` should equal however many adapters currently expose non-NULL `advance` -- at the time this plan was written that's `lawn`, `lawn2`, `lawn2clamp`, `wahern`, `naive`, `heap`, `linuxwheel`, i.e. all of them; if `cts_nalgos` has changed since, expect that many, not literally 7).

If it still fails on `linuxwheel`, re-check that `lw_tick` (unmodified) is genuinely being called -- not a stale build (`make clean` first) -- before suspecting the new test.

- [ ] **Step 3: Confirm the isolated single-timer reproduction from the bug report now fires on time**

This is a manual sanity check, not a permanent test (Task 1's test already covers the harness-shaped case; this just double-checks the report's simplest repro directly). From `src/benchmarks/c`, run:

```bash
cat > /tmp/lw_repro.c << 'EOF'
#include "cts.h"
#include <stdio.h>
int main(void) {
    extern const cts_vtable cts_linuxwheel_vtable;
    const cts_vtable *vt = &cts_linuxwheel_vtable;
    uint64_t ttls[] = {200, 300, 5000};
    for (int k = 0; k < 3; k++) {
        uint64_t ttl = ttls[k];
        cts_store *s = vt->create();
        vt->start(s, 0, ttl);
        vt->advance(s, ttl - 1);
        uint64_t fired = 0;
        for (uint64_t t = ttl; t <= ttl * 40; t++) { if (vt->tick(s)) { fired = t; break; } }
        printf("ttl=%llu fired_at=%llu (expected %llu)\n",
               (unsigned long long)ttl, (unsigned long long)fired, (unsigned long long)ttl);
        vt->destroy(s);
    }
    return 0;
}
EOF
cc -O0 -std=c11 -I. -I../.. -I../../utils -I../../../article/src/c/wheel -DWHEEL_NUM=6 \
   /tmp/lw_repro.c util.c impl/linuxwheel.c -o /tmp/lw_repro && /tmp/lw_repro
rm /tmp/lw_repro.c /tmp/lw_repro
```

Expected: all three lines read `fired_at=<ttl> (expected <ttl>)` with the two numbers equal, unlike the report's table (200/300/5000 previously fired at 4296/4396/never).

- [ ] **Step 4: Commit**

```bash
git add src/benchmarks/c/impl/linuxwheel.c
git commit -m "fix: linuxwheel's advance() now processes every cascade the jump crosses

lw_advance previously set now directly with no cascade processing, so any
staggered-preload jump crossing a level-1 (64-tick) or higher boundary left
timers stranded in stale slots -- confirmed losing 45% of expiry work over
a 5,000-tick measured window in the harness-shaped reproduction. Fixed by
looping the already-correct lw_tick, which costs O(elapsed) instead of
O(levels) but only runs during pre_lifecycle's untimed setup, never in a
timed payload, so no reported latency number is affected.

Fixes item 1 in the bug report. advance_matches_tick (previous commit) now
passes for every adapter, including linuxwheel."
```

---

### Task 3: Rename `linuxwheel` to an accurate name, and correct its description everywhere

**Files:**
- Rename: `src/benchmarks/c/impl/linuxwheel.c` -> `src/benchmarks/c/impl/wheel_exact.c`
- Modify: `src/benchmarks/c/cts.h:39`, `src/benchmarks/c/util.c:21`, `src/benchmarks/c/Makefile:9`, `src/benchmarks/c/concurrent/Makefile:8`, `src/benchmarks/c/benchmark.c:518`, `article/src/make_figures.py:34,40`, `article/lawn.tex:210`

**Interfaces:**
- Consumes: nothing new.
- Produces: `cts_wheel_exact_vtable` (replaces `cts_linuxwheel_vtable`), display name `"wheelexact"` (replaces `"linuxwheel"`) as it will now appear in every CSV/stdout line.

**Why this shape:** the file's own header comment and the article both describe this adapter as modeling "the Linux 4.8+ kernel timer-wheel redesign" and call it "non-cascading" -- but `lw_tick` cascades per level boundary (quoted in Task 1/2), and the actual Linux 4.8+ redesign's defining move was eliminating cascades by rounding expiry *up* to the level's granularity (`calc_index` in `article/src/c/wheel/kernel_impls/linux_kern_timer.c`, confirmed via `grep -ci cascade` on that file returning 0). This adapter keeps exactness and pays with cascades -- the opposite trade, and a legitimate, interesting design in its own right, just not a Linux wheel. Renaming it removes the false claim without requiring a from-scratch faithful kernel-wheel implementation (that's a separate, larger undertaking the report explicitly offers as an alternative, not a requirement).

- [ ] **Step 1: Rename the file and its internal identifiers**

```bash
git mv src/benchmarks/c/impl/linuxwheel.c src/benchmarks/c/impl/wheel_exact.c
```

In `src/benchmarks/c/impl/wheel_exact.c`, replace the file header comment:

```c
/* Hierarchical timer wheel modeled on the Linux 4.8+ kernel timer-wheel
 * redesign: timers are placed once, directly, into the (level, slot) their
 * remaining delta belongs to. No periodic full-cascade sweep. A level's
 * slot is only redistributed ("cascaded") the moment that level's own
 * granularity boundary is crossed, which is rare at high levels, giving
 * amortized O(1) per tick. This is an independent implementation of the
 * published algorithm (Varghese & Lauck, TW/TW87 in the bib), not a
 * literal kernel port.
 * 8 levels x 64 slots/level (6 bits/level, matching the kernel's
 * LVL_BITS/LVL_SIZE). Slots are dynamic id arrays with id-indexed
 * metadata for O(1) swap-removal, mirroring impl/naive.c's pattern. */
```

with:

```c
/* An exact hierarchical timer wheel with lazy cascading: timers are placed
 * once, directly, into the (level, slot) their remaining delta belongs to,
 * and a level's slot is only redistributed ("cascaded") the moment that
 * level's own granularity boundary is crossed. This preserves exact
 * expiry -- unlike the real Linux 4.8+ kernel redesign, whose defining move
 * is the opposite trade: it eliminates cascades entirely by rounding
 * expiry *up* to the level's granularity instead (see calc_index in
 * article/src/c/wheel/kernel_impls/linux_kern_timer.c). This is an
 * independent implementation of the published cascading-wheel algorithm
 * (Varghese & Lauck, TW/TW87 in the bib), not a Linux-kernel model and not
 * a literal port of anything.
 * 8 levels x 64 slots/level (6 bits/level). Slots are dynamic id arrays
 * with id-indexed metadata for O(1) swap-removal, mirroring
 * impl/naive.c's pattern. */
```

Then, still in `wheel_exact.c`, rename every `lw_` identifier to `we_` (the function bodies are unchanged, only names): `lw_create`->`we_create`, `lw_destroy`->`we_destroy`, `lw_start`->`we_start`, `lw_stop`->`we_stop`, `lw_tick`->`we_tick`, `lw_size`->`we_size`, `lw_advance`->`we_advance`. This includes the *internal* call site Task 2 just added: `we_advance`'s body currently reads `while (s->now < target) lw_tick(s);` and must become `while (s->now < target) we_tick(s);` -- it's easy to rename the seven declarations/definitions and miss this one call inside a function body. Confirm with:

```bash
grep -n "lw_" src/benchmarks/c/impl/wheel_exact.c
```
Expected: no output (every occurrence renamed).

Finally, rename the vtable definition at the bottom of the file, from:
```c
const cts_vtable cts_linuxwheel_vtable = {
    "linuxwheel", lw_create, lw_destroy,
    lw_start, lw_stop, lw_tick, lw_size, lw_advance,
};
```
to:
```c
const cts_vtable cts_wheel_exact_vtable = {
    "wheelexact", we_create, we_destroy,
    we_start, we_stop, we_tick, we_size, we_advance,
};
```

- [ ] **Step 2: Update every reference point**

`src/benchmarks/c/cts.h:39` -- change:
```c
extern const cts_vtable cts_linuxwheel_vtable;
```
to:
```c
extern const cts_vtable cts_wheel_exact_vtable;
```

`src/benchmarks/c/util.c:21` -- change:
```c
    &cts_linuxwheel_vtable,
```
to:
```c
    &cts_wheel_exact_vtable,
```

`src/benchmarks/c/Makefile:9` -- change:
```
ADAPTERS = impl/lawn.c impl/lawn2.c impl/lawn2_clamped.c impl/wahern.c impl/naive.c impl/heap.c impl/linuxwheel.c
```
to:
```
ADAPTERS = impl/lawn.c impl/lawn2.c impl/lawn2_clamped.c impl/wahern.c impl/naive.c impl/heap.c impl/wheel_exact.c
```

`src/benchmarks/c/concurrent/Makefile:8` -- change:
```
      ../impl/lawn.c ../impl/lawn2.c ../impl/wahern.c ../impl/naive.c ../impl/heap.c ../impl/linuxwheel.c \
```
to:
```
      ../impl/lawn.c ../impl/lawn2.c ../impl/wahern.c ../impl/naive.c ../impl/heap.c ../impl/wheel_exact.c \
```

`src/benchmarks/c/benchmark.c:518` -- change:
```c
    if (!strcmp(algo, "linuxwheel")) return 32.0;
```
to:
```c
    if (!strcmp(algo, "wheelexact")) return 32.0;
```

`article/src/make_figures.py:34` -- change:
```python
    "linuxwheel": ("Non-cascading wheel",    "#d62728", "*"),
```
to:
```python
    "wheelexact": ("Exact lazily-cascading wheel", "#d62728", "*"),
```

`article/src/make_figures.py:40` -- change:
```python
ORDER_ALL = ORDER + ["heap", "linuxwheel"]
```
to:
```python
ORDER_ALL = ORDER + ["heap", "wheelexact"]
```

`article/lawn.tex:210` -- within the Methodology paragraph, change:
```
we also differentially verified two further baselines, a non-cascading Linux-style wheel and a binary min-heap, whose numbers occupy a materially different point in the design space and are reported in the repository.
```
to:
```
we also differentially verified two further baselines, an exact hierarchical wheel that cascades lazily rather than tickwise and a binary min-heap, whose numbers occupy a materially different point in the design space and are reported in the repository.
```

- [ ] **Step 3: Confirm no reference to the old name survives**

```bash
grep -rn "linuxwheel\|\blw_" --include="*.c" --include="*.h" --include="*.tex" --include="Makefile" --include="*.py" .
```
Expected: no output.

- [ ] **Step 4: Build and run**

```bash
cd src/benchmarks/c && make clean && make test && ./test
```
Expected: `C correctness gate (7 impls: lawn, lawn2, lawn2clamp, wahern, naive, heap, wheelexact)` and `ALL C CORRECTNESS TESTS PASSED`.

```bash
make benchmark && ./benchmark single insert wheelexact manual 10000 100000 1000 uniform
rm -f test benchmark
```
Expected: a normal `DONE algo wheelexact ...` block, confirming the renamed adapter runs end to end through the CLI.

- [ ] **Step 5: Commit**

```bash
git add -A src/benchmarks/c article/src/make_figures.py article/lawn.tex
git commit -m "rename linuxwheel to wheelexact and correct its description

The old name and its header comment/article description claimed it models
the Linux 4.8+ kernel timer-wheel redesign and is 'non-cascading' -- neither
is true. It cascades per level boundary (that's what item 1's advance bug
was in), and the real 4.8+ redesign's defining move is eliminating cascades
by rounding expiry up instead of preserving it, the opposite trade this
adapter makes. It's a legitimate, independent implementation of the
classic Varghese & Lauck cascading-wheel algorithm with exact expiry; it
just isn't a Linux wheel. Renamed rather than rewritten -- a faithful
kernel-wheel port is a separate, larger piece of work.

Fixes item 3 in the bug report."
```

---

### Task 4: Document the `measure_expiry` distinct-TTL cap, in code and in the article

**Files:**
- Modify: `src/benchmarks/c/benchmark.c` (the comment above `setup_expiry`, around line 201)
- Modify: `article/lawn.tex` (Methodology section, after line 210)

**Interfaces:** none (documentation only, no behavior change).

**Why documentation, not a behavior change:** `setup_expiry` passes the hardcoded `WINDOW` (200) as `gen_ttls`'s span argument, and `gen_ttls` clamps `distinct` to `span`, so this op's effective distinct-TTL count can never exceed 200 regardless of what a sweep requests. The report's own severity note says the article's four-point workload table (values <= 200) is unaffected, only sweep points requesting `distinct` > 200 on this specific op are silently not testing what they claim. Changing the op's behavior (e.g. ticking for `ttl_span` ticks instead of a fixed window) would change every existing `expiry_distinct_ttls.csv` number and require re-running and re-plotting that sweep -- out of scope for a bugfix pass that isn't re-measuring anything. Documenting the cap is the zero-risk fix that matches what the report itself offers as an acceptable option ("or document the cap explicitly").

- [ ] **Step 1: Add the cap to the code comment**

Find the current comment/function (`src/benchmarks/c/benchmark.c`, search for `static void setup_expiry`). Immediately above it, add:

```c
/* Effective distinct-TTL count on this op is capped at WINDOW (200):
 * gen_ttls clamps `distinct` to the span it's given, and this op passes
 * WINDOW as that span so every tick in the fixed measurement window has a
 * chance to be due. Sweeping distinct_ttls past 200 on this op changes
 * nothing -- results/expiry_distinct_ttls.csv rows above 200 are really
 * measuring t=200, not the requested value. */
```

placed directly before the existing `static void setup_expiry(scenario_t *sc) {` line.

- [ ] **Step 2: Add a Methodology disclosure to the article**

In `article/lawn.tex`, immediately after the Methodology paragraph ending `...before any measurement is accepted.` (the paragraph containing the sentence fixed in Task 3, Step 2's `article/lawn.tex:210` edit), insert a new paragraph:

```latex
The isolated per-tick expiry measurement (Figure~\ref{fig:workloadexpiry}) uses a fixed 200-tick window, so its effective distinct-TTL count cannot exceed 200 regardless of what a sweep requests; readings above that are measuring $t=200$, not the requested value. The $t$-dependence claim at larger $t$ is therefore established through the full-lifecycle results (Section~\ref{sec:lifecycle}) instead.
```

Check the actual current label name for the lifecycle subsection before using `\ref{sec:lifecycle}` -- run `grep -n "label{sec:" article/lawn.tex` and confirm a label matching the Realistic Mixed Workload / lifecycle subsection exists under that exact name; if it's named differently, use the label that's actually there instead of guessing.

- [ ] **Step 3: Commit**

```bash
git add src/benchmarks/c/benchmark.c article/lawn.tex
git commit -m "docs: disclose measure_expiry's 200-distinct-TTL cap

setup_expiry passes the fixed WINDOW (200) as gen_ttls's span, which clamps
the effective distinct-TTL count to 200 regardless of what a sweep
requests -- sweep points above that are silently measuring t=200. The
article's own four-point table stays under that cap and is unaffected;
this documents the limit rather than changing measured behavior, since
changing it would require re-running and re-plotting an existing sweep.

Fixes item 4 in the bug report."
```

---

### Task 5: Record memory-gate skips in the CSV instead of omitting the row

**Files:**
- Modify: `src/benchmarks/c/benchmark.c` (the CSV header/row-writing in `sweep_axis`, around lines 651-719)
- Modify: `article/src/make_figures.py` (`by_algo`, around line 48)

**Interfaces:**
- Produces: every `*_<axis>.csv`/`*_<axis>_huge.csv` row now has a trailing `status` column (`ok` or `skip`) plus `need_gb`/`budget_gb`. `by_algo` (consumed by every `*_plot` function) must skip `status != "ok"` rows.

**Why this shape:** `memory_ok`'s budget comes from live `vm_stat` at the moment it's called (`free_and_inactive_bytes()`), so whether a large point is skipped depends on machine state during the run, not on anything about the point itself -- the report observed the identical `n=10^8` point declined once and completed twice more minutes apart with no config change. Today a skip is only a `printf`; the CSV row for that (algo, axis-value) pair simply doesn't exist, and `make_figures.py`'s `by_algo` silently produces a shorter series for whichever algo lost points, so a figure can be missing a line with no visible sign why. Adding a `status` column makes every skip a first-class, visible, analyzable row instead of an absence.

- [ ] **Step 1: Add the `status`/`need_gb`/`budget_gb` columns to the CSV header**

In `src/benchmarks/c/benchmark.c`, inside `sweep_axis`, change:
```c
    fprintf(f, "%s,algo,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed\n", axis, u, u, u, u);
```
to:
```c
    fprintf(f, "%s,algo,status,mean_%s,std_%s,p99_%s,max_%s,n_samples,runs,warmup,seed,need_gb,budget_gb\n", axis, u, u, u, u);
```

- [ ] **Step 2: Write a row for both outcomes, not just success**

Change:
```c
        for (int a = 0; a < cts_nalgos; a++) {
            if (results[a].success) {
                fprintf(f, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n", valstr, cts_algos[a]->name, 
                        results[a].agg.mean, results[a].agg.std, results[a].agg.p99, results[a].agg.max, 
                        results[a].agg.n, runs, warmup, SEED);
                if (pf) {
                    fprintf(pf, "%s,%s,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d\n", valstr, cts_algos[a]->name, 
                            results[a].agg.mean / (double)p.n, results[a].agg.std / (double)p.n, 
                            results[a].agg.p99 / (double)p.n, results[a].agg.max / (double)p.n, 
                            results[a].agg.n, runs, warmup, SEED);
                }
                printf("DONE %-8s %-13s axis=%-13s value=%s n=%s\n", op->name, cts_algos[a]->name, axis, valstr, n_str);
            } else {
                printf("SKIP %-8s %-13s axis=%-13s value=%s n=%s (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                    op->name, cts_algos[a]->name, axis, valstr, n_str, results[a].need_gb, results[a].budget_gb);
            }
        }
```
to:
```c
        for (int a = 0; a < cts_nalgos; a++) {
            if (results[a].success) {
                fprintf(f, "%s,%s,ok,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d,%.3f,%.3f\n", valstr, cts_algos[a]->name,
                        results[a].agg.mean, results[a].agg.std, results[a].agg.p99, results[a].agg.max,
                        results[a].agg.n, runs, warmup, SEED, results[a].need_gb, results[a].budget_gb);
                if (pf) {
                    fprintf(pf, "%s,%s,ok,%.4f,%.4f,%.4f,%.4f,%zu,%d,%d,%d,%.3f,%.3f\n", valstr, cts_algos[a]->name,
                            results[a].agg.mean / (double)p.n, results[a].agg.std / (double)p.n,
                            results[a].agg.p99 / (double)p.n, results[a].agg.max / (double)p.n,
                            results[a].agg.n, runs, warmup, SEED, results[a].need_gb, results[a].budget_gb);
                }
                printf("DONE %-8s %-13s axis=%-13s value=%s n=%s\n", op->name, cts_algos[a]->name, axis, valstr, n_str);
            } else {
                fprintf(f, "%s,%s,skip,0,0,0,0,0,%d,%d,%d,%.3f,%.3f\n", valstr, cts_algos[a]->name,
                        runs, warmup, SEED, results[a].need_gb, results[a].budget_gb);
                if (pf) {
                    fprintf(pf, "%s,%s,skip,0,0,0,0,0,%d,%d,%d,%.3f,%.3f\n", valstr, cts_algos[a]->name,
                            runs, warmup, SEED, results[a].need_gb, results[a].budget_gb);
                }
                printf("SKIP %-8s %-13s axis=%-13s value=%s n=%s (needs ~%.1fGB, safe budget ~%.1fGB)\n",
                    op->name, cts_algos[a]->name, axis, valstr, n_str, results[a].need_gb, results[a].budget_gb);
            }
        }
```

- [ ] **Step 3: Make `make_figures.py` skip non-`ok` rows**

In `article/src/make_figures.py`, find `by_algo` (around line 48):
```python
def by_algo(rows, xcol, ycol, scol=None):
    d = {}
    for r in rows:
        d.setdefault(r["algo"], ([], [], []))
        d[r["algo"]][0].append(float(r[xcol]))
        d[r["algo"]][1].append(float(r[ycol]))
        d[r["algo"]][2].append(float(r[scol]) if scol else 0.0)
    return d
```
Change to:
```python
def by_algo(rows, xcol, ycol, scol=None):
    d = {}
    for r in rows:
        if r.get("status", "ok") != "ok":
            continue
        d.setdefault(r["algo"], ([], [], []))
        d[r["algo"]][0].append(float(r[xcol]))
        d[r["algo"]][1].append(float(r[ycol]))
        d[r["algo"]][2].append(float(r[scol]) if scol else 0.0)
    return d
```
The `.get("status", "ok")` default means CSVs from before this change (no `status` column at all) still parse as if every row were `ok`, so this doesn't retroactively break any already-committed CSV that lacks the column.

- [ ] **Step 4: Verify against a real skip**

There's no guaranteed-reproducible way to force a skip (that's the point of item 5), but you can verify the plumbing directly:

```bash
cd src/benchmarks/c && make clean && make benchmark
./benchmark single insert lawn2 manual 1000 10000 100 uniform
```
This uses the `single` CLI path, not `sweep_axis`, so it won't show the new columns -- that's expected and fine, `single` was never affected by this bug. To exercise `sweep_axis` itself:
```bash
./benchmark sweeps
head -3 results/insert_n.csv
```
Expected: header line now reads `n,algo,status,mean_ns,std_ns,p99_ns,max_ns,n_samples,runs,warmup,seed,need_gb,budget_gb`, and every data row's third field is `ok` (small sweeps shouldn't trigger a real skip). Then:
```bash
python3 article/src/make_figures.py
```
Expected: runs to completion with no Python exception (confirms `by_algo` and every `*_plot` caller still work against the new column layout). Clean up:
```bash
rm -rf results test benchmark
```

- [ ] **Step 5: Commit**

```bash
git add src/benchmarks/c/benchmark.c article/src/make_figures.py
git commit -m "sweep CSVs: record memory-gate skips as rows instead of omitting them

memory_ok's budget comes from live vm_stat at call time, so whether a large
point is skipped depends on machine state during the run, not on anything
about the point itself -- observed the same n=10^8 point declined once and
completed twice more minutes apart with no config change. A skip used to
be printf-only; the CSV row for that (algo, value) pair simply didn't
exist, so make_figures.py silently produced a shorter series with no
visible sign why. Every row now carries a status (ok/skip) plus the
computed need_gb/budget_gb, and by_algo skips non-ok rows so existing plots
are unaffected by real data.

Fixes item 5 in the bug report."
```

---

### Task 6: Add a timestamp column to `inflection.csv`

**Files:**
- Modify: `src/benchmarks/c/benchmark.c` (`run_inflection`'s header and `sweep_inflection_curve`'s row-writing, both in the inflection-sweep code found via `grep -n "inflection.csv\|sweep_inflection_curve" src/benchmarks/c/benchmark.c`)

**Interfaces:** none new consumed elsewhere; `make_figures.py`'s `inflection_plot` reads `inflection.csv` by column name via `csv.DictReader`, so an added trailing column doesn't break it (verify this assumption in Step 2 below rather than trusting it blind).

**Why this shape:** every other sweep output can be located in time; `inflection.csv` cannot, so if a machine-state anomaly (memory pressure, thermal throttling, competing load) affects part of what can be a very long run, there's no way afterward to identify which rows fall inside the affected window -- the only options are to distrust the whole file or none of it. A per-row wall-clock timestamp makes partial quarantine possible.

- [ ] **Step 1: Add a `unix_ts` column**

Find the header write in `run_inflection` (search `fprintf(f, "span_regime,N,t,ttl_span`), and add `unix_ts` as the first column:
```c
    fprintf(f, "unix_ts,span_regime,N,t,ttl_span,t_over_N,lawn_pertick_ns,lawn2_pertick_ns,wahern_pertick_ns,naive_pertick_ns,"
               "ratio_lawn_over_wahern,ratio_lawn2_over_wahern,"
               "lawn2_life_ns,wahern_life_ns,ratio_wahern_over_lawn2_life,"
               "lawn2_life_p99,wahern_life_p99,ratio_wahern_over_lawn2_life_p99,"
               "lawn2_life_max,wahern_life_max\n");
```
(the columns after `unix_ts` are whatever the current header already lists -- copy them verbatim from the existing `fprintf` call, only prepending `unix_ts,` to the format string and adding the value described in Step 2 as the first `fprintf` argument).

Then find `sweep_inflection_curve`'s row-writing `fprintf(f, "%s,%zu,%llu,...` call and prepend `%ld,` to the format string and `(long)time(NULL),` as the new first argument -- e.g. if the current line is:
```c
        fprintf(f, "%s,%zu,%llu,%llu,%.6g,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f\n",
                regime_name, n, (unsigned long long)t, (unsigned long long)span, (double)t / n, 
                tm.lawn, tm.lawn2, tm.wahern, tm.naive, ratio_tick, ratio2_tick, 
                lm.lawn2.mean, lm.wahern.mean, ratio_life, lm.lawn2.p99, lm.wahern.p99, ratio_p99, lm.lawn2.max, lm.wahern.max);
```
change it to:
```c
        fprintf(f, "%ld,%s,%zu,%llu,%llu,%.6g,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f,%.4f,%.2f,%.2f\n",
                (long)time(NULL), regime_name, n, (unsigned long long)t, (unsigned long long)span, (double)t / n,
                tm.lawn, tm.lawn2, tm.wahern, tm.naive, ratio_tick, ratio2_tick,
                lm.lawn2.mean, lm.wahern.mean, ratio_life, lm.lawn2.p99, lm.wahern.p99, ratio_p99, lm.lawn2.max, lm.wahern.max);
```
Confirm `<time.h>` is already included at the top of `benchmark.c` (`grep -n "#include <time.h>" src/benchmarks/c/benchmark.c`); add it if not.

- [ ] **Step 2: Verify `make_figures.py`'s inflection reader survives the new column**

```bash
grep -n "def inflection_plot" -A5 article/src/make_figures.py
```
Confirm it reads via `csv.DictReader` (column-name-based) and doesn't do positional column indexing (e.g. `row[0]`) anywhere in that function or in `read()`. If it does index positionally anywhere, that's a separate latent fragility -- note it but don't fix it as part of this task unless it actually breaks; DictReader-based access is unaffected by an added column regardless of position.

- [ ] **Step 3: Smoke-test**

```bash
cd src/benchmarks/c && make clean && make benchmark
./benchmark inflection
head -3 results/inflection.csv
```
Expected: header starts with `unix_ts,span_regime,N,t,...` and the first data row's first field is a plausible current Unix timestamp (10 digits, matches `date +%s` run around the same time). This can be slow (the huge-N points take a while) -- it's fine to `Ctrl-C` it after a few rows appear; the point is confirming the column, not completing the sweep. Clean up:
```bash
rm -rf results test benchmark
```

- [ ] **Step 4: Commit**

```bash
git add src/benchmarks/c/benchmark.c
git commit -m "inflection.csv: add a unix_ts column per row

Every other sweep output can be located in time; this one couldn't, so a
machine-state anomaly partway through a long run left no way to identify
which rows fall inside the affected window afterward -- the only options
were to distrust the whole file or none of it. A per-row timestamp makes
partial quarantine possible.

Fixes item 6 in the bug report."
```

---

### Task 7: Fix the article's unsupported and contradictory TTL-distribution claims

**Files:**
- Modify: `article/lawn.tex` (three sentences: lines ~253, ~274/276, ~289 as of this plan's writing -- re-locate by the quoted text, not the line number, since Tasks 3-4 shift line numbers above this point)

**Interfaces:** none.

**Why this shape:** the Limitations sentence "is more than two orders of magnitude slower when nearly every timer has a distinct TTL" has no supporting number anywhere in the current article (confirmed: `grep -n "349\|116 times" article/lawn.tex` finds neither an old measurement nor a replacement one). It also sits against two other sentences that assert Lawn2 "still drains faster than the wheel" at the exact same $t/N=1$ point, which a reader can reasonably read as contradictory. Both of those two sentences over-extrapolate a measurement that is capped well below $t/N=1$ (Task 4's `measure_expiry` cap) to a conclusion at $t/N=1$, and one of them uses that unsupported extrapolation as the paper's stated *causal explanation* for the crossover ("idle-tick amortization, not draining work"). The fix removes the unsupported extrapolation in both places and lets the paper's own, independently-solid complexity argument ($O(t+k)$ vs $O(L+k)$, immediately adjacent in both cases) stand on its own -- it doesn't depend on the deleted claim at all.

- [ ] **Step 1: Draining Due Timers -- remove the unsupported $t/N=1$ sentence**

Find (search for `still drains faster`):
```
...Lawn2 wins on every distribution, most when TTLs cluster. Even at one distinct TTL per timer ($t/N=1$) Lawn2 still drains faster than the wheel, so it is the idle-tick amortization, not the draining work, that flips at the crossover below.
```
Replace with:
```
...Lawn2 wins on every distribution, most when TTLs cluster.
```
(delete the second sentence entirely; the paragraph now ends at "most when TTLs cluster.")

- [ ] **Step 2: The Distinct-TTL Crossover -- remove the same claim, let the complexity argument stand alone**

Find (search for `idle-tick cost amortized over the full lifecycle`):
```
...The crossover is about idle-tick cost amortized over the full lifecycle, not about draining work, which Lawn2 wins even at $t/N=1$ (Section~\ref{sec:workload}). Lawn2's per-tick cost is $O(t+k)$ for $t$ active buckets and $k$ expiring timers...
```
Replace with:
```
...Lawn2's per-tick cost is $O(t+k)$ for $t$ active buckets and $k$ expiring timers...
```
(delete the middle sentence; the text immediately before and after it should now read as one continuous flow, with a single space and no double period between them -- check this visually after editing.)

After this edit, check whether `\label{sec:workload}` (the label the deleted sentence referenced) is still referenced anywhere else:
```bash
grep -n "sec:workload" article/lawn.tex
```
If the only remaining occurrence is the `\label{sec:workload}` definition itself, that's fine -- an unreferenced LaTeX label produces no warning or error, no further action needed.

- [ ] **Step 3: Limitations -- soften the unsupported magnitude claim**

Find (search for `more than two orders of magnitude`):
```
...and is more than two orders of magnitude slower when nearly every timer has a distinct TTL.
```
Replace with:
```
...and is substantially slower when nearly every timer has a distinct TTL.
```

- [ ] **Step 4: Confirm no LaTeX build regression, if a toolchain is available**

```bash
which pdflatex latexmk 2>&1
```
If either exists:
```bash
cd article && pdflatex lawn.tex && pdflatex lawn.tex
```
Expected: exits 0, produces `lawn.pdf`. If neither toolchain is available (this has been the case on this machine throughout this session), skip this step -- it isn't a blocker, but don't claim the PDF was verified if it wasn't actually built.

If no toolchain is available, instead do a structural sanity check: confirm brace and inline-math-dollar counts are unchanged by the edit (each edit only removes prose between two already-balanced sentences, so both counts should net to what they were before minus exactly what was in the deleted text):
```bash
grep -o '{' article/lawn.tex | wc -l
grep -o '}' article/lawn.tex | wc -l
```
Expected: these two counts match each other (balanced braces), same as before the edit.

- [ ] **Step 5: Commit**

```bash
git add article/lawn.tex
git commit -m "article: remove unsupported t/N=1 draining claims, soften unsupported magnitude claim

Two sentences asserted Lawn2 'still drains faster than the wheel' at
t/N=1, extrapolated from a measurement (Figure fig:workloadexpiry) that is
capped at distinct-TTL counts well below that point (measure_expiry's
200-tick window, documented separately this session). One of them used
that extrapolation as the paper's stated causal explanation for the
crossover; removing it leaves the O(t+k) vs O(L+k) complexity argument,
immediately adjacent in both places, to stand on its own -- it doesn't
depend on the deleted claim. Separately, the Limitations sentence claiming
'more than two orders of magnitude slower' at t/N=1 has no supporting
number anywhere in the current article (the earlier 349x measurement was
removed in a prior revision and never replaced); softened to a qualitative
statement rather than inventing a replacement figure.

Fixes item 7 in the bug report."
```

---

### Task 8: Bibliography and repository hygiene

**Files:**
- Delete: `article/lawn.bib.bbl`
- No action needed: the CQ bib-entry fix and `run_benchmarks.sh` items from the report are already resolved (see Step 1)

**Interfaces:** none.

- [ ] **Step 1: Confirm two report items are already resolved, take no action on them**

The report's `article/lawn.bib` CQ-entry fix (wrong authors, entry type, journal-name typo, missing pages) is **already applied** -- commit `113941d` on this branch fixed it directly from the user's own corrected BibTeX, matching the report's independent finding that the CACM 1988 paper is sole-authored by Randy Brown. Confirm:
```bash
git log --oneline -1 -- article/lawn.bib
grep -n -A8 "@ARTICLE {CQ" article/lawn.bib
```
Expected: shows commit `113941d` (or later) and an entry reading `author = {Randy Brown}`.

The report's `run_benchmarks.sh` item ("hardcodes an absolute path... cannot run as committed") does not apply to this repository -- the file does not exist:
```bash
find . -iname "run_benchmarks.sh"
```
Expected: no output. No action needed; this was presumably already removed by an earlier repository cleanup pass (`cleanup: remove orphaned run scripts...`, visible in `git log --oneline -- '*run_*.sh'`).

- [ ] **Step 2: Delete the stray zero-byte `.bbl`**

```bash
ls -la article/lawn.bib.bbl
```
Expected: `0` in the size column, confirming it's the stray, not the real `.bbl`. Then:
```bash
git rm article/lawn.bib.bbl
```

- [ ] **Step 3: Commit**

```bash
git commit -m "article: remove stray zero-byte lawn.bib.bbl

A populated article/lawn.bbl already exists; this zero-byte file was a
stray build artifact.

Partially fixes item 8 in the bug report (the bib author fix and the
run_benchmarks.sh item were already resolved or don't apply -- see the
commit this one's stacked on for how each was confirmed)."
```

- [ ] **Step 4: Leave the two remaining item-8 sub-items as flagged, not fixed**

The report's other two item-8 points are genuine judgment calls, not defects with a single correct fix, and this plan deliberately does not resolve them unilaterally:

- **Uncited `kernel_impls/` sources**: `article/src/c/wheel/kernel_impls/linux_kern_timer.c` and `bsd_kern_timeout.c` are the strongest available evidence for what production wheels actually do (used as reference material during Task 3's rename), but are cited nowhere in `lawn.tex`. Citing them would strengthen the related-work discussion; leaving them is also defensible (they're reference material, not compiled or measured). No task here adds a citation -- flag for a human decision.
- **`n`/`N` notation**: `lawn.tex` uses lowercase `n` in the theory sections and uppercase `N` in the evaluation, mixing both within single sentences in Limitations. A full sweep to one convention is a bigger, more disruptive editorial change than anything else in this plan (touches nearly every section) and was flagged, not fixed, in this same repository's other article-review pass this session for the same reason. No task here performs this sweep -- flag for a human decision.

No commit for this step -- it's a note, not an edit.

---

## Definition of Done

- `cd src/benchmarks/c && make clean && make test && ./test` passes (`ALL C CORRECTNESS TESTS PASSED`) after every task, and in particular after Task 2, `advance_matches_tick` reports all adapters passing (it should report a FAIL specifically on `linuxwheel`/`wheelexact` right after Task 1 and before Task 2 -- that's the intended red/green split).
- `make benchmark` builds clean with zero warnings under the existing `-Wall -Wextra`.
- `grep -rn "linuxwheel\|\blw_"` across the tree returns nothing (Task 3 complete).
- `python3 article/src/make_figures.py` runs to completion with no exception against a fresh `./benchmark sweeps` run (Task 5's column addition didn't break plotting).
- `article/lawn.tex` no longer contains `more than two orders of magnitude`, `still drains faster than the wheel`, or `idle-tick cost amortized over the full lifecycle, not about draining work` (Task 7 complete) -- `grep -n` each phrase to confirm.
- `article/lawn.bib.bbl` no longer exists; `article/lawn.bib`'s `CQ` entry credits only Randy Brown (Task 8 complete).
- `git log --oneline` shows one commit per task (8-9 commits total, since Task 3 is one commit and Task 8 is one commit despite multiple steps), each independently reviewable and each leaving the test suite green except the single intentional red commit at the end of Task 1.
