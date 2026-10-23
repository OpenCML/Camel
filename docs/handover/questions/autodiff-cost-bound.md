# The cost bound of a CPU-bound slow test

Author: the H354 agent (autodiff wall margin), 2026-10-23. Status: open, needs the coordinator.

## Problem

`modules.autodiff/benchmark_gradients_vs_torch` (nine models, `benchmarks/train.py --check`) was
red in the slow layer on the runner's 4x wall margin alone:

```
[FAIL] benchmark_gradients_vs_torch                          210.76 s
    finished in 210757 ms against a 300000 ms timeout, below the 4x margin: the bound is today's
    wall time, not a bound
[SUMMARY] total: 1, pass: 0, fail: 1
```

with every one of its 312 gradient lines `ok` and stdout ending `gradient check: PASS`. Reproduced
on this branch at the state it starts from: `finished in 127203 ms against a 300000 ms timeout,
below the 4x margin` (same summary). The check's own cost is CPU work; the wall clock it reads is
the host's. The ruling of 2026-10-23 (`docs/handover/reviews/questions-2026-10-23.md`,
"declaration-cost-gate" (a)) re-expresses such a ceiling in CPU time and prints the wall figure
beside it:

> 改量 CPU 时间，不量墙钟。共享主机上的墙钟随负载漂移 ... 进程的 CPU 时间（用户态 + 内核态）基本不随负载变

That part is done and is not a question: `cpu_budget_ms = 1600000` on the test, read by
`test/tools/child-cpu.mjs` (it reaps exactly that test's process tree, so the `/proc/self/stat`
`cutime`+`cstime` delta is that test's CPU alone and stays exact while the layer runs in parallel),
with `timeout_ms = 900000` as its liveness bound. Measured on the Linux host (32 threads, 1-minute
load 67-128 beside the runs):

| run | CPU (user + system) | wall | load |
| --- | --- | --- | --- |
| 1 | 943.31 s | 127.5 s | 67-98 |
| 2 | 970.29 s | 148.7 s | 87-128 |
| 3 | 979.37 s | 165.5 s | same window |

A 3.8% spread in CPU against 30% in the wall clock. The same check done twice over reads about twice
the CPU with its content still green, and fails the budget by name.

## The two decisions

### 1. Does a test that declares a CPU budget still read the wall clock's margin rule?

- (a) **As implemented**: the test's cost bound is its CPU budget and `timeout_ms` is a liveness
  bound, so `TIMEOUT_MARGIN` does not read its wall clock -- the wall clock is no longer claimed as
  its bound. Cost: a slowdown that spends no CPU (a sleep, lock waits, an I/O stall) below the
  liveness bound passes. That is the ruling's own reading ("the wall clock is not gated at all",
  `perf.startup`), and a program that stops working is what the liveness bound is for.
- (b) **Keep the margin rule, read in CPU**: pass only when `cpuMs * 4 <= cpu_budget_ms`. Cost: the
  number in the plan stops being the bound -- the bound is a quarter of it, so a 970 s reading needs
  `cpu_budget_ms = 7760` to leave the same 2x room, and every reader and failure message has to
  divide by four. The rule exists to catch "the timeout is today's wall time"; a CPU budget states
  the measurement beside it in a comment, as `timeout_ms` does.

**Recommendation: (a).** A bound should be the number it prints, and the liveness bound should be
the only thing on the wall clock. (b) keeps one rule uniform at the price of naming a bound that is
not one.

### 2. The four resnet entries in the same plan are still read on the wall clock

`resnet_gradient_smooth_vs_torch64` and `resnetbn_gradient_smooth_vs_torch64`
(`timeout_ms = 300000`, so the margin rule bounds them at 75 s; their own comments record 6.3 s and
14.5 s serial), and `resnet_gradient_relu_kinks_vs_torch64` and
`resnetbn_gradient_relu_kinks_vs_torch64` (`timeout_ms = 3600000`, margin bound 900 s; their comment
records 3:12 and 1.8 GB peak alone and about 8.5 min at `-Jobs 8` -- a 1.8x bound). They are
CPU-bound the same way and the mechanism now exists.

**Recommendation**: measure and give them `cpu_budget_ms` too, one measurement run each. Not done
here: this branch changes only the bound the task named, and their wall bounds are wide enough today
that a measurement should come first.