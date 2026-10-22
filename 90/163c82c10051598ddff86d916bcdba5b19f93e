# H40 — the C2 memory peak: dead blocks are released, but the pacer counted them as live

Worktree `D:\Projects\Camel-wt\h40-memory`, branch `handover/H40-c2-memory`. Everything is in the
working tree: no commit, no new branch, no push. Logs and generated data are under `D:\Temp\h40`,
not in the tree; the reproduction programs are `D:\Temp\h40\repro_churn.cml`,
`D:\Temp\h40\repro_int_param.cml` and `D:\Temp\h40\repro_int_const.cml`.

The showcase measurement is taken at **`7c250059`**, the last commit where the C2 program runs: the
merge `6a7231b2` (this branch's tip) refuses it for an autodiff reason unrelated to memory — see
section 8. `src/core/mm/gen.cpp` and the header are byte-identical in `7c250059`, `6a7231b2` and
`develop` (`git diff --quiet` on both), so the collector work is the same at all three.

## 1. What was asked

The C2 showcase reports a peak working set of 262 MiB against PyTorch's 320 MiB, while its data is
10000 x 784 floats (30.1 MiB) and its parameters are 0.4 MiB. `README`/`H13` attribute the gap to
`take` copying every batch ("111 MiB before `take`, 262 MiB after"). H40: find out whether the data
blocks of those copies are reclaimed promptly, and fix it.

## 2. Summary

The copies are **not** the cause: a contiguous `slice` of the same rows copies just as much and
gives the same 260.5 MiB peak (section 3). The 111 MiB figure is not reproducible on this base at
all.

The real defect is in the collector's pacing. A block that a dead array owns is released by that
array's finalizer, and the finalizer only **queues** the release (`Block::releaseFromFinalizer`,
`gen.cpp` runs the queue after the collection has unlocked). `collectAtSafepointUnlocked` took the
base of the next pacing window *before* that queue ran, so `blockBytesAtCollection_` included the
very blocks the collection had just freed. The next window therefore had to grow past the previous
window's *pre-release* high-water mark before the trigger could fire again: with a workload that
recycles the same working set, every window starts higher than the last, the number of steps
between collections grows, and the high-water mark climbs linearly even though nothing is retained.

The fix takes that base after `runDeferredReleases()` (`include/camel/core/mm/gen.h`,
`src/core/mm/gen.cpp`). Dead blocks then live at most one pacing window (`minorTriggerBytes_` = 3.6
MB of growth), which for C2 is ~1.4 steps instead of 4–30.

| C2, 1000 steps, batch 100 (`compare.py` / the same sampler used for H13) | before | after |
|---|---|---|
| peak working set, standard profile | 261.5 MiB (`compare.py`), 260.5–260.7 (5 runs) | 65.0–65.6 MiB (5 runs, one 72.9) |
| peak working set, small profile | 81.8 MiB (`compare.py`) | 31.2 MiB (`compare.py`) |
| minor collections, standard | 33 | 503 |
| total minor pause, standard | 1.29 ms | 9.81 ms |

The values of the training run are unchanged (`val_loss=0.184876805`, `val_accuracy=0.9485` at
standard; bit-identical to the pre-fix run), so this is a scheduling change, not a semantic one.

## 3. `take` is not the cause

* `take` copies rows (`modules/tensor/kernels/layout.cpp`); `slice` with a contiguous range copies
  them too (same file, same `memcpy` path). A copy of the C2 program that replaces both `take`s by
  `slice(train_x, 0, off, off + batch)` peaks at **260.5 MiB**, the same as `take`: the copies are a
  per-step cost, not the retention.
* The 111 MiB number in `H13` cannot be reproduced on `7c250059` with either form of the batch
  selection. It predates the current array representation (data outside the GC heap in a `Block`)
  and the loop rewrite (H20/H21). The `take` narrative in `H13`/`README` should be corrected once
  the real cause is fixed; I did not edit those files.

## 4. Where the memory was

Two instruments: `std::mem::blocks()` (`live`, `bytes`, `allocs`, `frees`) every step, and the
allocator's own line with `CAMEL_GC_STATS=1`.

Live block bytes at the end of each step in the first 45 steps of the standard profile (a `take`
copy of the batch is 313 KB, the step's own churn — batch, activations, gradient, Adam state — is
~2.5 MB):

```
step  0 41 blocks 42.3 MB    step 13 50 blocks 43.5 MB   step 21 50 blocks 43.5 MB
step  1 67       44.8         step 17 154      53.7       step 27 206      58.7
step  2 93       47.4         step 18 180      56.2       step 30 284      66.4
step  3 50       43.5  <- GC  step 19 206      58.7       step 31 50       43.5  <- GC
step  7 50       43.5  <- GC  step 20 232      61.3       step 43 50       43.5  <- GC
```

Two things to read from it:

* The **low point never moved**: after every collection live returned to 43.5 MB (the 31.4 MB of
  `train_x`, the validation arrays, the permutation and the model). Nothing was leaked.
* The **high point of each cycle grew**: 47.4, 51.1, 56.2, 58.7, 61.3, 66.4, 71.4 MB, and the cycle
  got longer (4, 6, 8, 10, 12 steps between collections). The peak of the whole run is ~150 MB of
  block bytes above the 43.5 MB floor, which is the 262 − 111 MiB the showcase reports.

A minimal reproduction that runs at this branch's tip (no autodiff, no showcase) shows the same
thing in 600 steps: a loop that allocates one 2.4 MB array per step and keeps only its sum reaches a
**peak of 139.8 MiB of live block bytes over a 32.2 MiB base** before the fix, and stays flat at
**34.5 MiB** after it.

## 5. The cause, in the code

* `Block` is outside the collected heap and is released when its refcount drops to zero
  (`block.cpp`). For an array that dies, the refcount drops in the array's finalizer, which calls
  `Block::releaseFromFinalizer` — a **deferred** release, run after the collection has unlocked.
  `block.h` says so and says why: the finalizer runs under the allocator lock, the release must not.
* `block.h` also states the contract the pacing is supposed to honour: "The live byte count takes
  part in the collector's pacing … the blocks would never be released [otherwise]".
* `collectAtSafepointUnlocked` ended with
  `blockBytesAtCollection_ = blockBytesLive();` under the comment "buffers that its finalizers
  dead-arrayed are now free". They were not free yet: only the queue had been filled. The base of
  the next window was therefore *the pre-release* volume.
* `blockBytesGrowthUnlocked()` = `blockBytesLive() - blockBytesAtCollection_`, added to the
  birth-space occupancy for the minor trigger (`birthSpace_.used() + growth >= minorTriggerBytes_`,
  3.6 MB by default). Measured growth stayed at 3.6–4.4 MB at every request, but the actual growth
  between collections was 10–30 MB: the difference is exactly the blocks that were about to be
  released. Each cycle then needed to beat the previous cycle's pre-release peak, so the window
  grew by ~the freed volume, and the freed volume grew with the window. That is the linear climb.

The old comment was taken as evidence that the ordering was intended and correct; it was the only
place the two halves of the release (`finalize` → queue, queue → `releaseNow`) were described
together.

## 6. The fix

Two files, 33 insertions / 7 deletions, no semantic change:

* `gen.h`: declare `noteBlockBytesAfterDeferredReleases()`.
* `gen.cpp`: remove the base capture from `collectAtSafepointUnlocked`, and take it after
  `runDeferredReleases()` in `safepoint()`, `minorGC()` and `majorGC()`. `safepoint()` captures
  whether a collection actually ran (`requested != None`) so that a safepoint without a collection
  does not move the base — moving it there would erase the growth signal at every graph boundary.
* The base is re-read from `blockBytesLive()` rather than accumulated from per-release amounts,
  because the queue may also hold releases queued by earlier work.

Cost: the collections are ~15x more frequent (503 vs 33 for 1000 steps), and the whole run measures
9.81 ms in minor pauses (was 1.29 ms), i.e. ~1% of the 1.03 s training loop. The peak is the solid
result; the wall-clock difference between the two builds is inside this machine's run-to-run noise
(before: 5 runs, median 1.028 s, 1.013–1.497; after: median 1.420 s, 0.940–1.642 — the minimum is
lower and the maximum higher; `compare.py` measured 1.072 s before).

## 7. Verification

At `7c250059` (where the showcase runs), with the fix: `compare.py --profile small` **PASS** on all
agreement rows (initial loss, worst logged loss, final loss, accuracy, checkpoint round-trip),
31.2 MiB peak; the standard profile's values are bit-identical to the pre-fix run.

At `6a7231b2` (branch tip, fix applied):

* `pwsh -NoProfile -File D:\Projects\Camel-wt\camel-test.ps1 -Path D:\Projects\Camel-wt\h40-memory
  -Plan smoke,feat/mm,feat/compile -Jobs 8` — **878 tests, 878 pass, 0 fail** (fast tier,
  wall 28.3 s).
* The churn reproduction above: 139.8 MiB → 34.5 MiB peak.
* The C2 refusal is unchanged by the fix (same diagnostic, same location). `perf.showcase_c2`
  cannot run at the tip for that reason and was not run there; it passes at `7c250059`.

Not run, per the brief: full suite, GC stress, poison sweep, progen, PyTorch comparisons beyond the
small profile.

### Finding: `feat/mm/gc_pacing` is stale, independent of this fix

Run as a file target (its tests are `tier = "slow"`), all 8 cases fail: they expect `major=2..4` and
`by_large_budget` to account for every collection, but the case's arrays are now `Block`s, not
large GC objects (`alloc_large_mb=0.0`, `large_live_mb=0.0`, `major=0`). The same case run with the
main checkout's `develop` build (`D:\Projects\Camel\out\latest`, 2026-10-21 13:44) gives
`major=0 by_large_budget=0 large_live_mb=0.0` too. The plan's premise ("the large-object space holds
the arrays") did not survive the move of array data into blocks; its subject — how much the allocator
holds and how often it collects — is now block-paced, which is exactly the mechanism this task
fixed. I did not change the plan; rewriting it (or adding a block-pacing case) is a separate task.

## 8. The showcase does not run at the branch tip (unrelated to memory)

* Last commit where `perf.showcase_c2` passes: **`7c250059`** ("An enum can be a key …"). C2 runs
  there and `compare.py --profile small` PASSes.
* First commit where it fails: **`6a7231b2`**, the merge of `c254a2e4` ("A gradient that cannot
  exist is refused, and a loop that appends builds one array"). The rule is entirely in
  `modules/autodiff/pullback.cpp`: `git log 7c250059..6a7231b2 -- modules/autodiff/pullback.cpp`
  names only `c254a2e4`, and that file is identical in `c254a2e4` and the merge.
* Diagnostics, both shapes:
  * `std::macro std::nvm`: `error[RuntimeDiag::MacroEvaluationFailed]` — `'autodiff:with_grad'
    could not be evaluated by std::macro: RuntimeError: … a value of type 'Tensor<int64>' depends
    on a differentiated parameter but has no gradient channel, and it is used as data here (at
    train.cml:53:12)`.
  * `std::nvm` alone: the same text as `error[RuntimeDiag::RuntimeError]` after
    `c2 init val_loss=…`, i.e. at run time.
* The refused value is the `cast(labels, 'int64')` argument of `softmax_cross_entropy` in `loss`
  (`train.cml:53`): a data parameter of the differentiated function, of a type with no gradient
  channel.

### Minimal reproduction (for the autodiff owner)

Refused (`D:\Temp\h40\repro_int_param.cml`; the `with` parameter is the only differentiated one,
`labels` is ordinary data):

```camel
import { Tensor, cast, reshape } from tensor
import { softmax_cross_entropy } from nn
import { with_grad } from autodiff

with <w: Tensor>
func loss(labels: Tensor): float {
    return softmax_cross_entropy(w, cast(labels, 'int64'))
}

func main(): int sync {
    let w = reshape([0.5, 0.1, 0.2, 0.2], [1, 4])
    let labels = cast(reshape([2.0], [1]), 'int64')
    let (v, g) = with_grad(loss)<w>(labels)     // RuntimeError: Tensor<int64> … used as data
    return 0
}
```

The same integer tensor built **inside** the function instead of arriving as a parameter passes
(`repro_int_const.cml`, result `v=1.448… g=[0.3173 0.2127 -0.765 0.235]`). Two notes that narrow the
rule: the integer does not have to be an *ancestor* of the differentiated value — being a data input
of an op next to it is enough — and an integer **scalar** parameter is not refused
(`with <w: float> func f(n: int): float { return w * float(n) }` differentiates fine).

## 9. Open questions and recommendations

1. **The `H13`/`README` memory narrative should be replaced.** "`take` raised the peak from 111 to
   262 MiB" is not supported: `slice` copies identically and peaks the same, and 111 MiB is not
   reproducible on this base. The per-step copy is still a cost (313 KB per batch) but the memory
   was the pacer.
2. **Should block pressure ask for a *major* collection?** Blocks owned by arrays that were
   promoted to the elder space are freed only by a major collection, and the only block-driven
   request is the *minor* trigger. In C2 this does not bite (elder space stays at ~0.01 MB,
   `promoted=89` objects are genuinely live), but a program that promotes block-owning arrays and
   then drops them has no block-driven path to a major collection: `elderBytesSinceSweep_` counts
   elder *allocations*, not elder live volume. Worth a decision; not changed here.
3. `feat/mm/gc_pacing` should be rewritten for block pacing (section 7). It is the test that should
   have caught this, and it cannot fail loudly today because its own premise is obsolete.
4. The C2 refusal itself is with the autodiff owner (H22). The showcase was left untouched, per the
   brief.
5. A regression test for this fix would be a case like `D:\Temp\h40\repro_churn.cml` (peak live
   bytes over a churning loop, bound ~base + 4 MB + one allocation). The numbers it would assert
   (3.6 MB trigger, 2.4 MB per step) are deterministic; the case was not added because the report
   leaves the tree as a fix only.