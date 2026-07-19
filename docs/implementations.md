# Timer implementations: lawn, lawn2, wahern

The repo carries two Lawn implementations plus a third-party timing wheel used as
a baseline. All three are timer stores: schedule a timeout, cancel it, advance a
clock, and collect what expired. They differ in the algorithm, in who owns the
timer node, and in what workload they are good at.

## lawn (`src/lawn.c`)

The reference Queue-Map implementation (see [Algorithm.md](Algorithm.md)): timers
are bucketed by TTL value, each per-TTL queue is self-sorted, giving Push O(1),
Pull O(1), Poll O(max(x, t)) where t is the number of distinct TTLs.

Owning, key-addressed API - the store allocates the node and copies your key, and
you address a timer by an arbitrary opaque key:

```c
Lawn *l = newLawn();
set_element_ttl(l, key, key_len, ttl);   /* insert / update  */
del_element_exp(l, key);                  /* cancel by key    */
ElementQueue *due = pop_expired(l);       /* the expired nodes */
```

Use it when you want to cancel or look up timers by an external key and value
that convenience (and the readable reference) over raw speed. It costs a node
allocation, a key copy, and two hashmap operations per insert.

## lawn2 (`src/lawn2.c`)

The same Queue-Map algorithm as `lawn` (a differential test verifies both produce
identical expiry schedules), reimplemented for speed. Intrusive and handle-based,
like a kernel or wahern timer: you embed the node in your own object and keep the
handle. No per-insert allocation, no key copy, no second hashmap; delete is O(1)
through the node's own links, and an empty tick is O(1) via `next_expiration`.

```c
lawn2 *l = lawn2_new();
lawn2_node n;                 /* embed this in your own object */
lawn2_add(l, &n, ttl);        /* Push, O(1)  */
lawn2_del(l, &n);             /* Pull, O(1)  */
uint64_t expired = lawn2_tick(l);  /* advance one tick, count what came due */
```

Use it when performance and memory matter and you can hold a handle to each timer
(the common case when the timer lives inside an object you already own). It is the
fastest of the three on every operation, keeps flat memory over an unbounded TTL
span, and has no cascade jitter. Two consequences of the handle model: keep your
own key -> object map if you need to cancel by an external key (usually free,
since the object already exists), and `lawn2_tick` currently reports only the
count of expirations - wiring per-timer expiry actions (a callback field on the
node, or returning the expired list like `lawn`/wahern) is a small addition.

## wahern (`article/src/c/wheel/timeout.c`)

William Ahern's tickless hierarchical timing wheel - a battle-tested third-party
library, used here as the in-the-wild baseline. Timers are bucketed by absolute
time across levels and cascade down as the clock advances. Intrusive and
handle-based, absolute-time, with built-in periodic timers:

```c
struct timeouts *T = timeouts_open(0, &err);
struct timeout to;            /* embed this in your own object */
timeout_init(&to, 0);
timeouts_add(T, &to, now + delay);   /* absolute expiry */
timeouts_del(T, &to);
timeouts_update(T, now);             /* advance to current time */
struct timeout *e; while ((e = timeouts_get(T))) { /* expired */ }
```

Use it when timer deadlines are arbitrary or spread across a continuum (not a
small set of TTLs), when you need repeating timers, or when you want a hardened
external library. It handles very large timeouts through its levels; the tradeoff
is periodic cascade latency spikes when a level wraps.

## Choosing

- Few distinct TTL values (t much smaller than the timer count), performance
  critical, and you can hold a handle -> **lawn2**.
- You need to address timers by an external key, or you want the readable
  reference implementation -> **lawn**.
- Deadlines are arbitrary / continuous, you need periodic timers, or you want a
  proven third-party wheel -> **wahern**.

One-line algorithmic difference: `lawn`/`lawn2` bucket by TTL *value* - unbounded
span, no cascade, cost that grows with the number of distinct TTLs; `wahern`
buckets by absolute *time* - span bounded by its levels, periodic cascade spikes,
cost independent of TTL variety. The benchmark suites under
[`../src/benchmarks/`](../src/benchmarks/) quantify these tradeoffs.
