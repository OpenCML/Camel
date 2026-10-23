# Profiler: the allocation column of `std::profile`

`std::profile` times operators at their boundary: one timestamp pair around the call of the
operator's function. That answers "how long did this operator take" but not "how much of it was
the allocator". Two earlier reports had to write the limit down:

> allocation and copying inside a kernel is not separable; `std::profile` times operator boundaries

and, of a matmul row,

> the matmul row includes its own packing and output allocation

The first is half retired: the Camel value allocators are now timed and attributed to the operator
call that entered them, so a row can say which part of its wall time it spent allocating. The
second is answered for the block space: a row reports how many data blocks it allocated, so a
matmul that allocates exactly one block per call (its output) is not also allocating a packing
buffer there. Copies a kernel makes without allocating (`memcpy` over memory it owns), a BLAS
library's own `malloc`-ed packing buffer, and the page faults of first touching a reserved segment
remain inside the kernel row and are still not separable; §7 says so in the report too.

## 1. What existed before

The allocators already counted *what* they did:

- `mm::blockBytesLive()`, `blockBytesPeak()`, `blockLiveCount()`, `blockCreations()`,
  `blockFrees()` and `resetBlockStatsForTests()` in `include/camel/core/mm/block.h` / `block.cpp`,
  exposed to Camel as `std::mem::blocks()` (`allocs`, `frees`, `live`, `bytes`);
- `mm::space usage()` (used, reserved, segments, allocations) for each space;
- `src/core/mm/profiler.cpp` writes a GC/heap snapshot as JSON (an unrelated tool, despite the
  name).

None of it was time, and none of it was attributed to a graph node or an operator. A program could
say it allocated 2.24 GiB but not which row of the profile paid for it. That missing attribution,
not the counters, is what this change adds.

## 2. Where the hooks are

`include/camel/profile/allocation.h` declares one function and one RAII scope:

```cpp
void noteAllocation(uint64_t ns, uint64_t bytes, bool block) noexcept;

class AllocationScope {
    // constructed where an allocation starts, destroyed where it ends
};
```

`AllocationScope` is cheap when the session is off: the constructor tests
`profile::enabledInline()`, and the destructor does nothing if it did not start a timestamp. When
the session is on it takes one `steady_clock` pair, and the destructor calls `noteAllocation`.

Three places construct it, which is the whole set of Camel value allocators:

| site | file | `block` | counted as |
|---|---|---|---|
| `Block::create` and `Block::grow` | `src/core/mm/block.cpp` | `true` | data block, with `bytes` = the payload length the block was asked for |
| `GenerationalAllocatorWithGC::alloc` | `src/core/mm/gen.cpp` | `false` | collected-heap object (`mm::autoSpace()`) |
| `GrowableBumpAllocator::alloc` | `src/core/mm/growable.cpp` | `false` | permanent-space object (`mm::permSpace()`) |

`GrowableFreeListAllocator` is the block space's own free list and is entered from `Block::create`
already; it is not instrumented twice. The `Block` header itself comes from `new` and is not
counted (§7).

`bytes` for a block is the payload length passed to `Block::create`/`grow`, not the block space's
rounded request: header, alignment and rounding are the allocator's own overhead and show up in
the time, not in the "requested" number.

## 3. Attribution

The profile thunk that times an operator also announces it:

```cpp
const OperatorCallScope call(I);   // sets a thread-local operator index
entry.op(...);                     // allocations inside see that index
```

`noteAllocation` adds to the global counters and, when the thread-local index is set, to that
operator's row and to the "inside operator calls" counters. The scope restores the previous index
on exit, so a nested call (an operator calling another operator's function, a GC hook) attributes
each allocation to the innermost operator, never to two rows.

The limits this design buys:

- **Per thread.** The index is `thread_local`; an allocation on a worker thread is counted as
  outside operator calls, even if the worker is running this operator's work. The parallel
  operators therefore under-report their own allocation time in the row and the aggregate's
  "outside" part carries it.
- **Per operator URI, not per graph node.** Rows are keyed by the resolved operator, so two nodes of
  the same operator share a row. The profile has always done this; the allocation column inherits
  it.
- **What is inside an operator** is what its call does on the calling thread, including any
  function call it makes through the runtime.

The totals are relaxed atomics; a run that is hot enough for the atomics to matter is not a run
whose per-allocation time is interesting.

## 4. What the report prints

Aggregate, above the steps:

```
  allocations          26771 calls (6031 block payloads), 2.24 GiB requested, 11.57 ms inside the allocators
                       inside operator calls: 25151 calls (6031 block payloads, 2.24 GiB), 11.46 ms; the rows below
```

Step block:

```
  allocations per step 1498.9 calls (362.0 block payloads, 137.94 MiB payload, 680.6 µs in the allocators)
```

Operator row (the columns after the share of operator time):

```
  cpu  tensor:matmul                       306    681.48 ms      1.38 ms      2.23 ms   21.9%    498.1 µs (0.1%)       1224        306   181.12 MiB
```

`time (share of the row)` is the allocator time and its share of that row's total. `allocator
calls` counts every instrumented allocator entry; `block payloads` is the subset that allocated a
data block; `payload bytes` is the sum of those blocks' payload lengths. The JSON has the same
numbers: top-level `allocations` (`calls`, `blocks`, `bytes`, `ns`, `inside_calls`,
`inside_blocks`, `inside_bytes`, `inside_ns`), `step.allocations_per_round`, and per row
`alloc_ns`, `alloc_calls`, `alloc_blocks`, `alloc_bytes`.

The per-step numbers are inside-operator means over the measured rounds: an allocation made
outside any operator call is in the aggregate but not in a step row.

## 5. Cost

Off (the default): one relaxed flag load and a not-taken branch per operator call and per
allocation, plus the counter reads a loop round already did. The session is off unless the
pipeline names `std::profile`; `std::profile report=false hints=false` keeps it off, while
`report=false` alone still records.

On: one timestamp pair (reported as 50 ns on the host used here) per operator call and per
allocation, and a handful of relaxed atomic adds. On the d=256 training step of §6 that is
5,976 operator calls and 26,771 allocator calls, about 1.6 ms over a 3.19 s run (0.05%). The
allocation timestamp pair sits inside the operator's wall time, so the row is very slightly
slower under the session than off; the report prints the pair cost and the allocation time
separately so the instrument and the measured arithmetic can be told apart.

The program's own output is unchanged. The d=256 step's own `naive bench` median moved 144.27 ms
(no pass) to 164.09 ms (with it), but the host carried load 60 on 32 hardware threads and the
same run's steps ranged 123–239 ms; that difference is not separable from the host's noise, and
the file records it as such rather than as an overhead figure.

## 6. Measurement: d=256 and d=1024

One thread, 16 steps and 3 warm-up, the generated `train_step.cml` (`measure.py`'s substitutions),
`tensor::fuse std::jit`, `--block-limit 8192`, `std::profile report=true top=0`. Full data:
[`examples/models/naive-in-expert-out/results/2026-10-23-d256-d1024-alloc-profile.json`](../../examples/models/naive-in-expert-out/results/2026-10-23-d256-d1024-alloc-profile.json).

| | d=256 | d=1024 |
|---|---|---|
| step wall, median | 171.19 ms | 6.27 s |
| block payloads per step | 362.0 | 362.0 |
| allocator calls per step | 1,498.9 | 1,498.9 |
| payload per step | 137.9 MiB | 2.64 GiB |
| allocator time per step | 0.68 ms | 1.34 ms |
| allocator share of the step | 0.40% | 0.02% |
| allocator share of operator time | 0.37% | 0.022% |
| matmul rows, allocator share | 0.075% | 0.0072% |
| elementwise rows, allocator share | 0.48% | 0.081% |

The three matmul rows allocate 850 blocks for 850 calls: exactly one block per call, the output,
so the canonical d=256 matmul allocates no packing buffer. The d=1024 library path may pack inside
the BLAS call, where this instrument cannot see it.

**Answer to the d=256 question.** Allocation is not the gap. A 34% gap on the 66.1 ms step of the
sweep is about 22 ms, roughly 33 times the 0.68 ms a whole step spends in the allocators; the
matmul rows are 0.075% allocator time, and the elementwise rows 0.48%. The gap is kernel time. The
tool rules out the Camel allocators; it does not split the copies inside a kernel, so it does not
by itself prove the gap is arithmetic rather than copies. The host was loaded (load 60) during
this run, which inflates the absolute step times; the shares are ratios within one run.

This is worth stating against the fusions: if a d=256 fusion is built to remove allocation and
packing, the measurement says the allocation it removes is under half a percent of the step. The
part of the gap it could address must be copies inside the kernel, which this column does not
measure; a fusion justified by allocation alone is justified by 0.4%.

## 7. What it cannot see

- a `malloc` behind `new` that never touches a Camel space: the `Block` header itself, a
  `std::vector`'s buffer (some kernels use them for packing);
- a BLAS library's own `malloc`-ed packing buffers (`sgemm` packs inside the call);
- a `memcpy` the kernel runs over memory it already owns, including the packing a kernel does into
  a stack buffer;
- the free side: the report times allocations, not the free-list push or the collection that
  reclaims a block. A step frees as much as it allocates, but the reclaim mostly happens at a
  safepoint between operator calls, so it is not in the rows; the allocation column is a lower
  bound on the allocator's cost, not the whole of it. It is a tight lower bound for the question
  in §6 because doubling it does not move the answer.
- the page faults of first touching a newly reserved segment: the allocator returns untouched
  memory, and the kernel that writes it pays for the fault, so the cost is in the kernel row;
- graph/meta-space allocations and the block-space segment reservation itself (only the payload
  call is timed; a `mmap` inside it is included, which is why one first big allocation can look
  expensive, as `tensor:sum_axis` does in the d=256 file);
- allocations on other threads (see §3).

The report says "inside the allocators" and "kernel time" rather than implying the split is
complete. That is the honesty limit this column has, and it is narrower than the one it replaces.

## 8. Guards

`test/plans/feat/trans/profile_alloc.plan.toml` runs `test/cases/trans/profile/profile_alloc.py`:

- `count`: two `tensor:zeros([1024])` and one `tensor:add` report exactly 3 block payloads and
  12,288 bytes, with 2 blocks / 8,192 bytes under `tensor:zeros` and 1 / 4,096 under `tensor:add`.
  The counts are deterministic; no duration is asserted. A hook that never fires reports zero and
  fails.
- `step`: one loop round allocates one block of 4,096 bytes, the `zeros` before the loop is the
  ninth block, the aggregate row, the per-step row and the sum of the operator rows agree.
- `off`: no pass and `report=false hints=false` print byte-identical stdout, and a session that is
  on prints the program's output first, unchanged.

The JSON is read for the counts because it is stable; the text row is checked for presence and
for the per-step numbers.