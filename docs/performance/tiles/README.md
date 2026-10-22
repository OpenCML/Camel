# Tiles measurements

## RTX 4060 Ti, 2026-10-15

Raw records: [2026-10-15-rtx4060ti.jsonl](2026-10-15-rtx4060ti.jsonl).
Each record includes the measured Git revision (`5e5c4485`) and a fingerprint of
the Tiles source after a standard rebuild, device,
driver, NVRTC version, target, shapes, plan statistics, raw samples and numerical
comparison. Reproduce using:

```text
npm run build
python test/tools/benchmark-tiles.py --runs 3 --output results.jsonl
```

Three fresh Camel processes ran the same seeded workloads. Each variant has a
correctness run, five warmups and 21 CUDA-event measurements, alternating which
variant runs first. Inputs are restored before each launch; restores are timed
on the host and excluded from the device interval. Both variants use identical
inputs and the same f32 arithmetic policy. The baseline disables elementwise
fusion but retains the verified storage planner.

| Workload | Optimized median | Unfused median | Ratio across runs |
|---|---:|---:|---:|
| Elementwise chain, 1024x1024, row bias | 0.0149–0.0154 ms | 0.0492 ms | 3.20–3.29x |
| Softmax, 128x257 | 0.0297 ms | 0.0317–0.0328 ms | 1.07–1.10x |
| Matmul, 127x259 @ 259x131 | 0.0133 ms | 0.0133 ms | 1.00x |

All compared outputs matched exactly in these samples, before and after timing.
CPU comparisons are covered separately by the GPU test plans. These samples
are evidence for these workloads only, not a general equivalence proof.

The elementwise case goes from five kernels to one. Softmax goes from five to
four; its reductions remain serial per output, so reduction optimization is the
next material opportunity. Matmul has identical generated code in both variants;
its equal medians are a same-code control, not a performance change. Earlier
exploratory runs before the code commit showed wider variability (elementwise
ratios 2.15–3.42x and softmax 1.07–1.83x). The recorded three consecutive runs are
from the committed, rebuilt implementation. Do not treat their tighter interval
or the best ratio as a guarantee of stable desktop performance.

Limitations: timings are device-stream spans (including gaps between launches),
with warm caches after input restoration. GPU clocks were not locked and this
is a desktop GPU. Compilation, transfer and setup costs appear in separate raw
fields; this table excludes them, graph extraction and host snapshot creation.
No Triton or cuBLAS comparison has been made yet. The remote measurements below
extend hardware coverage; larger working sets and external baselines remain
required before wider performance conclusions.

## Stable parallel maximum, revision 8e7be374

Raw records: [RTX 4060 Ti parallel-max run](2026-10-15-rtx4060ti-parallel-max.jsonl).
The benchmark now includes `max(x, -1)` for a 128x4097 tensor and explicitly
disables parallel maximum in its reference policy. Its three optimized medians
were 0.03520, 0.01987 and 0.01955 ms, versus 0.09046, 0.07485 and 0.07450 ms for
serial maxima: 2.57–3.81x within these paired measurements. All outputs matched
the reference before and after timing. This does not authorize reordering sums.

This revision also fixes transfer ordering using pinned, same-stream uploads.
Consequently cross-revision wall-clock differences cannot be attributed solely
to maximum reduction. Desktop timing remains variable: the identical-code
matmul control ranged from 0.92 to 0.99x in this run. Compare paired samples and
retain the raw records; do not interpret small differences as proven speedups.

## Remote build notes

The baseline revision 75776da8 built and passed 35/35 host/GPU/tensor tests on
an RTX 3090 (device 3) on the Linux build host. It used an isolated source
checkout, the pre-existing Conda Clang 23.1.2
toolchain and build virtual environment (CMake 4.4.3, Conan 2.33), and CUDA 12.3.
The Conda sysroot required a private `CPLUS_INCLUDE_PATH` exposing only the
system Python multiarch configuration header directory. No system toolchain or
existing checkout was changed. Standard `npm run build` produced the binaries.

The same isolated directory was rebuilt at revision 8e7be374. All 38 host,
GPU and tensor checks passed on device 7, covering special values, grid-stride
reduction, large uploads and the three execution engines. The first attempt on
device 3 passed 34/38: the other four processes failed while retaining a CUDA
context with `CUDA_ERROR_OUT_OF_MEMORY`, before computing outputs. Device 3 had
22,870 MiB in use by shared workloads. Manual reruns passed, then the full suite
and benchmark were moved to device 7, observed at 18 MiB used and 0% utilization.
No other workloads were stopped.

An archive benchmark can record its verified source revision with
`--source-revision <full-commit-id>`; the runner also records a source fingerprint.

## RTX 3090, revision 8e7be374

Raw records: [RTX 3090 parallel-max run](2026-10-15-rtx3090-parallel-max.jsonl).
The source fingerprint matches the RTX 4060 Ti run exactly. This is Linux,
driver 580.95.05 (CUDA driver API 13000), NVRTC 12.3, target compute_86, device 7.
Reproduce after the standard build with the toolchain environment above:

```sh
python test/tools/benchmark-tiles.py --runs 3 --devices 7 \
  --source-revision 8e7be37400c6e67707f7f3d07a217126d0896486 \
  --output tiles-rtx3090.jsonl
```

| Workload | Optimized median | Reference median | Paired ratio across runs |
|---|---:|---:|---:|
| Elementwise chain, 1024x1024 | 0.01331–0.01408 ms | 0.06144 ms | 4.36–4.62x |
| Softmax, 128x257 | 0.03482–0.03494 ms | 0.04192–0.04208 ms | 1.20–1.21x |
| Matmul, 127x259 @ 259x131 | 0.01162–0.01206 ms | 0.01155–0.01210 ms | 0.99–1.01x |
| Maximum, 128x4097 | 0.00662–0.00701 ms | 0.09136–0.09149 ms | 13.05–13.79x |

All 12 reports matched exactly before and after timing (21 samples per variant,
five warmups). The reference disables fusion and parallel maximum, while matmul
is an identical-code control. These are internal schedule comparisons, not
comparisons against Triton or vendor libraries. Clocks were not locked; this is
a shared server, and an idle-device observation does not reserve the GPU or
exclude later interference. The larger maximum speedup than on the desktop is
not evidence that every reduction or every GPU will benefit equally.

## What belongs here

Keep reproducible performance comparisons, benchmark samples and relevant device
conditions here. Routine test output belongs in local `tmp/` or CI artifacts;
record the command and result in the functional commit description. Do not add a
separate dated report for each successful test run.
