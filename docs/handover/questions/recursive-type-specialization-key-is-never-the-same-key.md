# A recursive type specialization never meets its own key again: the key holds a `Type *`

Question raised by the lane `handover/h478-facts-and-fold` (H478) on 2026-10-25, on its integration
tree (`h465`'s tip `5116c47e6` merged with `origin/develop` `a7a2badc6`). It reproduces the runaway the
H465 ruling replaced the eight-copy bound with, measures which path makes the key and why the key is
never found again, and asks for a ruling. **No semantics were changed**: no cap, no guard, no
`relaxNames`, no key.

## 1. Reproduced on this tree, verbatim

    $ ./out/latest/bin/camel test/cases/modules/onnx/export_fold.cml
    error[RuntimeDiag::RuntimeError]: An error has occurred: onnx.export_model: the type specialization
    of 'unroll' did not reach a fixed point: 4096 instances, each with a key the one before it did not have.
       --> test/cases/modules/onnx/export_fold.cml:30:5
        |
     30 |     export_model((x: Tensor): Tensor => forward<l>(x), x, get_env('ONNX_OUT'))
        |     ^^^^^^^^^^^^
    help: Check the runtime environment and input data for unexpected issues.

exit 1, wall **235 s** (clean Release build, fingerprint `7ab5704ef875`, no probe compiled in, nothing
else running on the host). The plan entry's `timeout_ms = 10000`
(`test/plans/feat/modules/onnx.plan.toml:31-55`) makes the runner read the same case as a timeout
(`expected exit 0, got null`). H465 measured 256.99 s on its own tree
(`export-fold-has-no-fixed-point.md`).

A lane's reading passed on by the coordinator says the guard's error text is **not** in the `h468` /
`h476` trees, while `refinesType`, `instanceParameterType` and `collectDirectFuncSpecializations` are
verbatim identical there. Those trees report the same divergence differently (the tree H449 measured
still had `kMaxTypeSpecializationsPerRecursiveFunction = 8`, `include/camel/runtime/draft_inline.h:43`,
which stopped it silently). This lane did not check those trees itself: it may not make another
worktree.

## 2. Which path makes the key

`src/runtime/draft_inline.cpp:1022-1048`, verbatim, is the only place a type-only instance is made:

    const bool boundedTypeSpecialization =
        std::ranges::none_of(
            bindings,
            [](const DraftFormalBinding &b) { return b.bindsValue(); }) &&
        onCallCycle(*draft.program(), calleeGraph);
    if (boundedTypeSpecialization &&
        !session.findSpecialization(
            makeRuntimeSpecializationKey(calleeGraph, bindings))) {
        const GraphId root = isValid(calleeGraph->typeSpecializationRoot())
                                 ? calleeGraph->typeSpecializationRoot()
                                 : calleeGraph->id();
        if (session.typeSpecializationCount(root) >= kTypeSpecializationFixedPointGuard) {
            ... throw std::runtime_error(std::format(
                    "the type specialization of '{}' did not reach a fixed point: {} instances, each "
                    "with a key the one before it did not have", ...

A temporary probe at that site (since reverted; `FIXPOINT make` / `FIXPOINT probe` / `FIXPOINT count`
lines under `--log-level info --log-include DraftOpt`) logged 7934 keys in 7934 calls, and **every one
of them is the same key**:

    FIXPOINT make callee='unroll' calleeRoot=-1 cached=false baseRev=2
      key=[kind=0 index=2 value=0 type=f32[..] flags=255][kind=1 index=3 value=0 type={ w: f32[2, 2], b: f32[2] } flags=255]
    ... the same line 7934 times: cached=true never, one distinct key text

`formal[2]` is `h: Tensor` and `formal[3]` is the `with <l: Layer>` parameter. The instance names grow
one per rewriting round, three nonces at a time:

    unroll$tspec4 → unroll$tspec4$spec5 → unroll$tspec7$spec8 → unroll$tspec10$spec11 → ...

and the probe shows the recursion alternating between two call sites:

    FIXPOINT probe callee='unroll'       calleeRoot=-1 callerRoot=-1 relaxNames=false   (71 of 400)
    FIXPOINT probe callee='unroll'       calleeRoot=-1 callerRoot= 2 relaxNames=false   (46 of 400)
    FIXPOINT probe callee='unroll$spec2' calleeRoot=-1 callerRoot=-1 relaxNames=false   (31 of 400)
    FIXPOINT probe callee='unroll$tspec4 calleeRoot= 2 callerRoot=-1 relaxNames=true    (252 of 400, and the same for tspec7, tspec10, ...)

So the keys the guard counts are all made for the **base** graph `unroll` (`calleeRoot=-1`), and on
that path the log says, verbatim:

    FIXPOINT   formal[2] kind=0 declared='Tensor'  actual='f32[..]' instantiated='f32[..]' refines=true
    FIXPOINT   formal[3] kind=1 declared='Layer'   actual='{ w: f32[2, 2], b: f32[2] }' instantiated='{ w: f32[2, 2], b: f32[2] }' refines=true

## 3. Why the key is never found again

`include/camel/runtime/specializations.h:63-72` and `:84-105`, verbatim (the key's own comparison and
hash):

    struct RuntimeSpecializationBindingKey {
        RuntimeSpecializationBindingKind kind = RuntimeSpecializationBindingKind::Norm;
        size_t index                          = 0;
        /// The bound value; NullSlot for a binding that only refines the formal's type.
        slot_t value                 = NullSlot;
        camel::core::type::Type *type = nullptr;
        uint8_t runtimeFlags         = 0;

        bool operator==(const RuntimeSpecializationBindingKey &other) const = default;
    };
    ...
        for (const RuntimeSpecializationBindingKey &binding : key.bindings) {
            const size_t h =
                std::hash<const void *>{}(static_cast<const void *>(binding.type));

The refined type is a `Type *`, compared by address (`operator== = default`) and hashed by address;
`makeRuntimeSpecializationKey` (`draft_inline.cpp:703-736`) fills it from `binding.refinedType` and even
sorts the bindings by `lhs.type < rhs.type`, also an address order. The refined array type is built
**fresh at every call site**: `instanceParameterType` (`draft_inline.cpp:97-164`) reaches

    if (!packBound && element == formalArray->elemType() && extents.size() == formalArray->extents().size() &&
        std::equal(extents.begin(), extents.end(), formalArray->extents().begin())) {
        return formal;
    }
    if (anyRank) {
        return ArrayType::openRank(element, extents);
    }
    if (extents.empty()) {
        return element;
    }
    return ArrayType::create(element, extents);

and for `h: Tensor` with an actual of type `f32[..]` the element is taken from the argument
(`:110-116`), so the early return does not fire and `ArrayType::openRank` builds a new object.

The probe printed the addresses, which is the measurement:

| in the first 1200 logged keys | distinct addresses |
| --- | --- |
| `type=f32[..]` | **1200** |
| `type={ w: f32[2, 2], b: f32[2] }` | 1 |
| `baseRev` | 1 (always `2`) |

The key text is identical 7934 times, the base's revision is the same every time, the `with`
parameter's type has one address throughout — and the refined array type has a different address every
time. `findSpecialization` (`src/runtime/draft_session.cpp:1167-1198`, with
`programKeyOf` at `:1158-1165` putting `baseGraph`/`baseRevision`/`bindings` in the program's table)
therefore cannot find the entry the round before made: the hash differs, so it is not even a candidate
bucket. One new graph per round, 253 a round, until `kTypeSpecializationFixedPointGuard` (4096) throws.

The site's own comment (`:988-992`) says the same thing in the author's words — "the same key again and
again, because every rewrite of a call inside the base invalidates the entry that names the copy" —
but the readings say it is the type's **address**, not the base's content revision, that fails to match
(a second invalidator may also be at work later in the run: the probe logged `baseRev` for the first
1200 keys only, so this lane measured the revision to be constant in the first ~5 rounds and says
nothing about the rest).

## 4. Why the ruling's widening is not reached

The widening is gated on the **callee** (`draft_inline.cpp:599-601`, verbatim):

    const GraphId calleeRoot = calleeGraph->typeSpecializationRoot();
    const GraphId callerRoot = draft.typeSpecializationRoot();
    const bool relaxNames = isValid(calleeRoot);

`callerRoot` is computed and **never used** (the only occurrence of that name in the tree). Every one
of the 7934 keys is made with `calleeRoot=-1`: the call still names the base graph `unroll`, whose
`typeSpecializationRoot()` is invalid, so `relaxNames=false` and the element is taken from the argument
again. Where the widening *is* reached it does work and binds nothing:

    FIXPOINT probe callee='unroll$tspec4' calleeRoot=2 callerRoot=-1 relaxNames=true
    FIXPOINT   formal[2] kind=0 declared='f32[..]' actual='f32[..]' instantiated='f32[..]' refines=false
    FIXPOINT   formal[3] kind=1 declared='{ w: f32[2, 2], b: f32[2] }' ... refines=false

Two things keep the call naming the base graph:
- the recursive call *inside* an instance is repointed by the reuse path
  (`draft_inline.cpp:995-1014`) only when `isValid(instanceRoot) && instanceRoot == calleeGraph->id()`;
  the **base graph's own draft** has `typeSpecializationRoot()` invalid, so that path is skipped there
  and the key path is taken (the 71 `callerRoot=-1` probes);
- the `with` parameter's type is bound **unconditionally**: `instanceParameterType` returns the
  argument for a formal that is not an array (`:101-103`), so no widening can remove the `Layer`
  binding. The unstable member of the key is exactly the array one.

A lane's isolated probes (passed on by the coordinator, not repeated here) say a formal written
`f32[n]` does not even build a key: `take(xs: f32[n])` → 3 graphs, `specializations: 0`;
`take2(f32[n], f32[n])` → 3 graphs, 0; `grow2(f32[n], f32[n], k)` → 3 graphs, 0 — the call site's
`f32[2]` is not a refinement of `f32[n]` there, and an `f32[n] ↔ int[n]` element-type cycle exits 0
with `specializations: 0`. So for a *named* dimension the widening has no key to widen; for the bare
open-rank `Tensor` of this case the key exists and is never found again.

## 5. Inference: what it converges to if the relaxation is connected

Both ways of connecting it point at **one type instance**:

- If the refined type were compared by content instead of address, the first `unroll$tspec` (the key in
  §2) would be found in the program's table on every later round, the count per root would stop at 1,
  and the graph set would stay what round 1 made (`unroll$tspec4` and its value specialization
  `unroll$tspec4$spec5`); the extraction lap then reads `refines=false` for both formals, binds
  nothing, and leaves the call in the graph — the pre-flip green shape (3 MatMul/Relu/Mul, 14 ops).
- If instead `relaxNames` were reached on the base-graph path (that is what the unused `callerRoot`
  looks like it was for), the `f32[..]` binding would disappear — the element stays the
  numeric-unknown, so `instanceParameterType` returns `formal` unchanged and `refines=false` — leaving
  the single `Layer` binding, whose address is stable, and the cache would hit: **1** again.

This does not contradict the ruling's other expectation (`reviews/questions-2026-10-24-evening.md:272-287`,
`grow` → exactly two instances, `f32[2]` and `f32[?]`): `unroll`'s second lap has an **empty** key
because the instance's own formal already is `f32[..]`, so there is nothing more to bind.

## 6. Questions for the author

1. Is a refinement's type meant to be part of the key by address, or by what the type is? The key
   holds `Type *` and both its comparison and its hash use the address, while the refined array type
   is built per call site. (`rewrite-invalid-is-a-type-address-comparison.md`, filed by this same
   lane, is the same defect in `DraftWritings::assertEqualDeferred`.)
2. Is the widening meant to be reached on a call that still names the base graph — i.e. is
   `relaxNames` supposed to read `callerRoot` (or both), and is the base graph's own draft supposed to
   specialize at all when it is already on a call cycle?
3. Should a `with`-parameter's structural type stay a binding under the widening, given that
   `instanceParameterType` binds it unconditionally (`:101-103`)?
4. Is `export_fold` meant to be red while it is diagnosed — the guard is doing what the ruling asked
   (a loud error instead of a silent truncation) — or does the ruling expect the widening plus the key
   to make it converge, in which case this case is the regression test for it?

## 7. What this lane did not do

No change to `kTypeSpecializationFixedPointGuard`, the old `kMaxTypeSpecializationsPerRecursiveFunction`,
`relaxNames`, `instanceParameterType`, the key or its comparison; no `known_fail`; no change to
`export_fold`'s expectations or its `timeout_ms`; the probe above was reverted before the runs quoted in
the lane's report.