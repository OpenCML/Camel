# Core phases 3 and 4 integration review

Reviewed `origin/fsh-dev` through `3cbb3328` (21 commits after `4aea57d7`).
The integration is validated on `wzj-dev` before promotion to `develop`.

- LOOP preserves a shared capture interface for its condition and body. Its
  validator checks state, element and capture ports. Backend lowering is detached;
  graph-inspecting operators, including Tiles, receive the original function and
  closure. Tiles still rejects LOOP explicitly; integration is not GPU loop support.
- Graph facts use graph lineage/revision, declaration epochs and target hashes.
  Release builds check stamps. Rewrites propagate changes through graph dependencies.
  Target-specific facts are excluded from portable core persistence. Tiles retains
  its current owned index instances and checked native declaration dependencies;
  it has not yet moved physical plans into the shared fact store.
- Resource declarations describe Tiles device inputs, fresh/possibly aliased device
  outputs and driver effects. Resource conflict queries do not provide CUDA completion
  tracking. Existing synchronous device ownership remains in force.
- Definition graphs are opt-in. Their introduction does not silently replace Tiles
  arithmetic. GPU lowering of generic expanded definitions remains follow-up work.
  Review found that the definition cache compared only semantic keys although the
  registry permits same-version replacement. It now also checks the immutable
  declaration record; the regression replaces a valid definition by a missing
  function at the same version, verifies refusal, restores it and verifies rebuilding.
  Cycle refusals are also rechecked after a declaration epoch changes: removing a
  dependency's recursive definition must make the original definition usable again.
- Slice bounds now fault instead of clamping. Tiles already invokes core inference
  and index instantiation, so it inherits the correction. A manual `source` call
  with extent 3 and stop 9 reports the bounds error even with an invalid GPU device;
  the host regression locks in rejection before CUDA initialization.
- CPU log-softmax now rounds the sum to f32 and uses the tensor logarithm, following
  its definition. Tiles currently has no direct log-softmax lowering.

Initial Windows integration checks: 236/236 passed across loops, graph facts,
resources, definitions, index/operator semantics, tensor bounds, Tiles and GPU
plans. Final broader results and Linux evidence are recorded separately when complete.
