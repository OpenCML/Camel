# Core operator instantiation in Tiles (2026-10-16)

Source: `4ea9b8cd`.

Graph extraction now calls `core::sem::instantiate(OperId, InstanceContext)`
instead of Tensor's private `instantiate(OpDef, InferContext)` entry. The same
path supplies layouts and synthetic matrix primitives introduced by gradients.
The current core declaration therefore supplies the maps and validates their law
indices; extraction cannot silently bypass it through the registry's old callback.

Specialization supplies concrete input types and original constant slots. These
slots are borrowed only during extraction under its no-collection scope. The
resulting Program still owns its data and index instances and retains no GC
pointers. Tensor type inference and supported-operation selection remain in the
compiler; generic graph-fact reuse and arbitrary declared-operator lowering are
not claimed by this change.

Validation uses standard builds followed by:

```text
node scripts/test.js test/plans/feat/modules/tiles.plan.toml test/plans/gpu
```

The existing suite exercises layout constants, scalar arguments, gradients,
runtime shape specialization, resident inputs, lexical permissions and moving
GC. A manual row-region run preceded automated checks. This is a protocol-routing
refactor; no performance benchmark or full-project suite was run for this batch.

Final result: **87/87 passed on each GPU**, with no failures or skips.

- [Windows RTX 4060 Ti](2026-10-16-core-instances-rtx4060ti-tests.txt)
- [Linux RTX 3090, device 4](2026-10-16-core-instances-rtx3090-tests.txt)
