# Lawn2 Architecture & System Integration Guide

`lawn2` is an allocation-lean, portable C11 Queue-Map (Lawn) timer store designed for high-throughput, low-latency event-driven systems. It provides a complete, modern rewrite of the original Queue-Map algorithm (`src/lawn.c`), eliminating the per-insertion heap allocation tax, element-ID hash lookups, and key copying overhead of traditional timer managers.

At its core, `lawn2` maps each **Time-To-Live (TTL) duration to a self-sorted doubly-linked queue**. Because timers with the same TTL naturally arrive in strict chronological order, appending new timers to the tail of a per-TTL queue maintains explicit expiration sorting without requiring sorting algorithms, heap operations, or tree rebalancing.

---

## When Should Lawn2 Be Used?

Ideal Workloads for Lawn2 Include:
* **High-Throughput Networking & Servers:** Managing thousands or millions of timeouts, HTTP keep-alives, gRPC request deadlines, or session expirations.
* **Repeating TTL Profiles:** Workloads where many concurrent timers share identical TTL values (e.g., Millions of users with similar Timers to track for each).
* **Allocation-Sensitive Environment:** Systems where `malloc`/`free` during hot paths (such as handling incoming network packets) introduces unwanted heap lock contention or latency spikes.
* **Deterministic Real-Time Loops:** Applications needing predictable $O(1)$ push/pull guarantees.

---

## Technical Architecture & Design Principles

`lawn2` combines two key design choices to achieve high performance (on top of the `Lawn` Algorithm design):

1. **Open-Addressing Blade Table:** Per-TTL queues (called "blades") are managed inside a flat open-addressing table indexed via hashing. The table dynamically resizes when load factor exceeds 70%.
2. **Intrusive Caller-Owned Nodes:** `lawn2_timer` structures are embedded directly inside your domain objects. Storage is caller-owned; `lawn2` only manipulates internal links (`next`, `prev`). This guarantees zero dynamic allocations during insertion.


```
                 +-------------------------------------------------------+
                 |                     lawn2 Store                       |
                 |  now: 1005   live: 3   next_expiration: 1010          |
                 +-------------------------------------------------------+
                                           |
                                           v
                       Open-Addressing Blade Table (cap = 16)
               +---------+---------+-------------------+---------+
  Slot Index:  |    0    |    1    |       ...         |    i    |
               +---------+---------+-------------------+---------+
               | unused  | TTL=10  |                   | TTL=50  |
               +---------+---------+-------------------+---------+
                           | head                              | head
                           v                                   v
                      +---------+                         +---------+
                      | Node A  | (exp: 1010)             | Node C  | (exp: 1055)
                      +---------+                         +---------+
                           | next
                           v
                      +---------+
                      | Node B  | (exp: 1012)
                      +---------+
```


---

## Integration

Integration involves three main steps: embedding `lawn2_timer` into your domain structures, registering/canceling timers on events, and driving `lawn2_tick()` from your event loop.

### Step 1: Embed `lawn2_timer` in Your Data Structure

Embed `lawn2_timer` inside your primary connection or request structure. Use standard container macros (e.g., `container_of`) to resolve from the node pointer back to your parent object.

```c
#include "lawn2.h"
#include <stddef.h>

/* Standard container_of macro */
#define container_of(ptr, type, member)     ((type *)((char *)(ptr) - offsetof(type, member)))

typedef struct connection {
    int socket_fd;
    char client_ip[45];
    
    /* Intrusive timer node embedded directly */
    lawn2_timer timer;
} connection_t;
```

### Step 2: Initialize Store & Manage Timers

Initialize the global or per-thread `lawn2` store during system startup.

```c
/* Create store at server startup */
lawn2 *timer_store = lawn2_new();

connection_t *conn = create_connection(fd);

/* Add connection timeout (e.g. 5000ms TTL) */
uint64_t ttl_ms = 5000;
lawn2_add(timer_store, &conn->timer, ttl_ms);

/* If data arrives on socket: refresh/re-arm the timer */
void on_socket_activity(connection_t *conn) {
    /* Unlink existing timer */
    lawn2_del(timer_store, &conn->timer);
    
    /* Re-add with fresh TTL */
    lawn2_add(timer_store, &conn->timer, 5000);
}

/* If connection closes normally: remove timer */
void on_connection_close(connection_t *conn) {
    lawn2_del(timer_store, &conn->timer);
    free_connection(conn);
}
```

### Step 3: Drive `lawn2_tick` from Your Event Loop

Hook `lawn2_tick` into your main event loop (e.g., `epoll`, `kqueue`, `libuv`, or a dedicated timer thread).

```c
void event_loop_tick(lawn2 *l) {
    lawn2_timer *expired_head = NULL;

    /* Advance clock by 1 tick and retrieve expired nodes list */
    uint64_t count = lawn2_tick(l, &expired_head);

    if (count == 0) return; /* Empty tick handled in O(1) */

    /* Process all expired timers */
    lawn2_timer *curr = expired_head;
    while (curr != NULL) {
        lawn2_timer *next = curr->next;
        
        /* Recover parent connection object */
        connection_t *conn = container_of(curr, connection_t, timer);
        
        /* Handle timeout event */
        handle_connection_timeout(conn);
        
        curr = next;
    }
}
```

---

## API Quick Reference

| Function | Complexity | Description |
| :--- | :--- | :--- |
| `lawn2_new()` | $O(1)$ | Allocates and returns a new timer store instance. |
| `lawn2_free(l)` | $O(1)$ | Frees store table structures (caller nodes remain untouched). |
| `lawn2_add(l, node, ttl)` | **$O(1)$** | Assigns TTL/expiration to node and appends to per-TTL queue. |
| `lawn2_del(l, node)` | **$O(1)$** | Unlinks node directly from store without table lookups. |
| `lawn2_tick(l, &out_head)` | **$O(1)$ empty** / $O(\max(x,t))$ | Advances clock $+1$, sets `*out_head` to expired list, returns count. |
| `lawn2_size(l)` | $O(1)$ | Returns total number of active timers currently in store. |
| `lawn2_now(l)` | $O(1)$ | Returns current store logical clock tick value. |


---

## Lawn2 Intrusive Timer Storage Architecture & Evaluation Guide

Since `lawn2` is an allocation-lean, intrusive Queue-Map timer store designed for high-performance, we elected to include an implementation of a **built-in two-level chunked array (block storage) implementation** to store `lawn2_timer`s in order the to assist developers who require a self-contained, turnkey ID-to-node mapping system without writing a custom memory allocator.

The built-in storage engine implements a **two-level page table** (chunked array) that directly maps a 64-bit integer timer ID to a stable `lawn2_timer` memory address in $O(1)$ time. 

Instead of storing nodes in a single flat array—which would require resizing and memory relocation as the timer population grows—the storage system divides memory into fixed-size slabs called **blocks**. Each block contains exactly **4,096 nodes** ($2^{12}$).

#### Bitwise Indexing Architecture
Resolving an ID to its corresponding node pointer bypasses hashing, collision resolution, and tree traversals entirely. It relies on two ultra-fast bitwise operations:

1. **Block Index (`id >> 12`):** Extracts the upper 52 bits of the ID to locate the correct block pointer inside the root array (`state->blocks[...]`).
2. **Slot Offset (`id & 4095`):** Extracts the lower 12 bits using a bitmask (`BLK_MASK`) to locate the exact node struct within the selected 4,096-element slab.

```c
#define BLK_BITS 12
#define BLK_SIZE (1u << BLK_BITS)
#define BLK_MASK (BLK_SIZE - 1)

/* O(1) bitwise resolution from ID to memory address */
size_t block_idx = (size_t)(id >> BLK_BITS);
size_t slot_idx  = (size_t)(id & BLK_MASK);
lawn2_timer *n    = &s->blocks[block_idx][slot_idx];
```

---

### Key Advantages

* **Guaranteed Pointer Stability:** Because `lawn2` manages timers using intrusive doubly-linked lists (`next` and `prev` pointers inside `lawn2_timer`), individual node addresses **must never change** while active in the store. When the system needs to expand, only the root array of block pointers (`realloc(s->blocks, ...))`) is resized. Individual 4,096-node memory slabs remain fixed in place, completely eliminating the risk of dangling pointers or corrupted internal queues.
* **Amortized Zero-Allocation Hot Path:** Once a block slab is provisioned via `calloc`, adding (`lawn2_add`), deleting (`lawn2_del`), or ticking (`lawn2_tick`) timers incurs zero heap allocations or deallocations. 
* **Deterministic $O(1)$ Access:** Lookup performance is strictly bounded and constant-time, free from algorithmic degradation caused by hash table load factors or tree rebalancing.
* **Instant Slot Reuse:** When a timer expires or is deleted, its corresponding node is unlinked from the active queue but remains resident in its memory block. When the application later re-arms the same ID, the existing node is immediately reused in place with zero setup latency.

---

### Limitations & Trade-offs

* **Monotonic Memory Retention:** The storage engine acts as an arena allocator; memory scales monotonically up to the highest ID ever encountered ($	ext{max\_id\_seen}$). Individual block slabs are **never freed** during runtime as timers expire. Reclaimed memory only returns to the operating system when the entire state store is destroyed via `state_free()`.
* **Sensitivity to Sparse IDs:** The architecture assumes dense, sequentially distributed IDs. If timer IDs are sparse, random (e.g., 64-bit cryptographic hashes or UUIDs), or jump across wide numeric ranges, the store will allocate full 4,096-node blocks for empty intermediate spaces, resulting in severe memory fragmentation and waste.
* **Provisioning Step Costs:** Requesting an ID that crosses into a new block boundary triggers an immediate allocation of 4,096 `lawn2_timer` structs (approx. 192 KB on 64-bit architectures). While amortized to near-zero over time, this creates a minor allocation step-cost on the specific call that breaks into a new page.

---

### Decision Matrix: Should You Use This Implementation?

| Evaluation Criteria | Use Built-In Block Storage | Implement Custom Storage |
| :--- | :--- | :--- |
| **ID Distribution** | Dense, sequential 0-indexed integers (e.g., auto-incrementing IDs, array indices). | Highly sparse, randomized integers, UUID hashes, or alphanumeric keys. |
| **Memory Management** | Peak memory retention is acceptable; application runs as a persistent service with stable ID ceilings. | Aggressive runtime memory reclamation is required; memory must shrink immediately after timers expire. |
| **Data Architecture** | Standalone timer management where timers reference external data via integer IDs. | Domain objects already exist (e.g., `struct connection`); embedding `lawn2_timer` directly into existing structs is preferred. |
| **Development Speed** | Turnkey, zero-configuration setup required immediately. | Custom memory pools, custom arena allocators, or specialized indexing logic required. |

---

### Architectural Guidance for Custom Implementations

If you decide to bypass the built-in block storage and implement your own timer node management, it is recommanded to adhere to these fundamental rules required by the `lawn2` intrusive design:

1. **Never Move Active Timer Nodes:** Do not store active `lawn2_timer` structs in standard dynamic arrays (`std::vector` or raw `realloc` buffers) where elements can shift in memory. Any memory relocation of a timer node linked into `lawn2` will corrupt the store's internal doubly-linked queues.
2. **Embed When Possible:** The most memory-efficient pattern for custom storage is **struct embedding** (the "wahern wheel" style). Place the `lawn2_timer` directly inside your application's primary domain object:
   ```c
   struct client_session {
       int socket_fd;
       char ip_address[64];
       /* Embed timer directly — zero secondary lookup or allocation needed */
       lawn2_timer timer; 
   };
   ```
   You can then use `container_of` / `offsetof` macros to retrieve your parent session structure when the timer expires during `lawn2_tick`.
3. **Handle Deletion Cleanly:** Always check `n->in_store` before attempting to delete or modify a timer node to prevent double-free or double-unlink vulnerabilities.
