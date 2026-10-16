# Row-region fusion on two NVIDIA GPUs

Source: `40130ac3ee65b7853975b5d8ce63ad7f6d68c39f`.
Windows RTX 4060 Ti (NVRTC 12.6) and Linux RTX 3090 (NVRTC 12.3).
Both final builds passed the Tiles host plan and all GPU plans: **81/81 each**.
The row cases cover shared intermediates, affine broadcasts, grid-stride rows,
small/unsupported widths, cancellation, signed zeros, subnormals, infinities and
NaNs. Raw-bit CPU/GPU comparisons run on NodeVM, FastVM and JIT; arithmetic NaNs
compare classification, because their payload is unspecified. Extrema retain the
separate canonical-bit tests. Native mutations check row membership and launch size.

## Measurements

`python test/tools/benchmark-tiles.py --suite rows --runs 3 --output results.jsonl`
adds the revision and a source fingerprint to each report. All 24 records have
the same Tiles source fingerprint and zero mismatches before and after timing.
The archive runner on Linux supplies the full revision via `--source-revision`.
Each process uses 5 warmups and 21 timed samples with alternating implementation
order and restored inputs. These are CUDA-event spans including launch gaps on
resident data, compared with the same compiler's unfused implementation. Uploads,
downloads, specialization and compilation are outside the device interval.

| Workload | Kernels | 4060 Ti speedup range | 3090 speedup range |
|---|---:|---:|---:|
| softmax 128 x 257 | 1 | 2.31–2.88x | 6.68–7.06x |
| softmax 1024 x 1024 | 1 | 4.52–4.73x | 9.15–9.22x |
| layernorm 128 x 257 | 1 | 1.77–1.89x | 2.83–2.91x |
| layernorm 1024 x 1024 | 4 | 1.45x | 1.45x |

The last workload deliberately keeps the preceding multi-kernel schedule: its two
additive reductions exceed the initial row policy's 1024-element serial-work cap.
It is a fallback measurement, not a benefit attributable to new row fusion.
The comparison is neither Triton nor a vendor library, nor the previous optimized
Tiles revision. Do not interpret it as an end-to-end model speedup.

The server was shared. GPU 4 had 0% utilization immediately before sampling and
about 3 GiB free; other GPUs were busy. Device snapshots are recorded alongside the
raw JSONL and test logs. Repetition controls some noise, not all shared-host effects.

## Numerical and architectural limits

Sum/mean use the declared f64 accumulator. The parallel tree is selected only if
the runtime exponent-range bound proves every intermediate addition exact in f64;
otherwise the ascending chain executes. No reassociation or precision permission
is inferred. Maximum uses the core's exact reduction freedom. Intermediate f32
rounding points and inclusive shared-buffer lifetimes are preserved.

This is one full-graph rank-two output with bounded shared storage and a measured
cost heuristic. General regions, multiple outputs, register layouts, loop bodies,
full structured kernel statements and generic definition expansion are follow-up
work. The row plan structures stages and storage, but CUDA control-flow emission
still prints statements directly.
