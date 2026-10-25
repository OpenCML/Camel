# The profile allocation counts move when a type stops being allocated per query

Question raised by the lane `handover/h499-type-identity` (H499) on 2026-10-25, on its tree tip
`235c90df7` (the type-identity ruling of 2026-10-24 evening, items 1-3, implemented in
`c92e27373`, `0b9c300cd`, `15807f113`, `3def8e150`, `235c90df7`). Everything below is verbatim from
that tree. The lane changed no expectation and re-recorded no golden: this file is the measurement
and the question.

## 1. What is red, and what moved

The full gate (`-Plan smoke,test/plans/feat -Tier fast -Jobs 8`) on this tree reads
`[SUMMARY] total: 6218, pass: 6190, fail: 9, skip: 11, known failures: 8`. Six of the nine reds are
the base's own (`lrit`, `mnist`, `nn.lrit`, `nn.mnist`, `declaration_evaluator`,
`print_and_recompile`), one is the stale graph floor recorded in
`docs/handover/questions/specialize-growing-shape-graph-floor-reads-five.md`, and two are new:

    [FAIL] profile.alloc_step                                   282.73 ms
        [INFO] stdout does not contain 'profile allocations: PASS'
        [INFO] output contains 'FAIL'
        [stdout] ok: exit code 0 (got 0)
        [stdout] ok: aggregate: 9 block payloads (got 9)
        [stdout] ok: aggregate: 36864 bytes (got 36864)
        [stdout] ok: one block payload per step (got 1.0)
        [stdout] ok: 4096 payload bytes per step (got 4096.0)
        [stdout] ok: the rows sum to the aggregate (9 vs 9)
        [stdout] FAIL: the text step row reports one 4 KiB block (got '2.0 calls (1.0 block
                  payloads, 4.0 KiB payload, 2.0 µs in the allocators)')
        [stdout] ok: the text report prints the aggregate allocation row

    [FAIL] fusion_group.profile.none                            330.36 ms
        [stdout] ok: exit code 0 (got 0)
        [stdout] FAIL: the report is the base commit's report, byte for byte apart from the
                  measured numbers
        [stdout] ok: the JSON has no fusion group
        [stdout] ok: the JSON has no operator-executions key
        [stdout] FAIL: the JSON is the base commit's, apart from the measured durations

`test/cases/trans/profile/profile_alloc.py:118-120` is the first one's assertion:

    line = text_value(out, "allocations per step")
    check(
        line.startswith("4.0 calls (1.0 block payloads, 4.0 KiB payload"),
        f"the text step row reports one 4 KiB block (got {line!r})",
    )

`test/plans/feat/modules/fusion_group.plan.toml` is the second one's: it compares the whole report
to `test/cases/modules/tensor/fusion_group_none.profile.golden.json` (and `.txt`), captured from
the base commit `b43dd5c68`, after every duration, share and byte count is replaced by a
placeholder; its own `desc` says "a new line on this path, or a changed count, fails".

## 2. What the diff is: allocation **calls**, not the block the program asks for

The lane ran the case's own comparison on the host (its `normalize_text` / `normalize_doc`,
against its goldens). Every differing field is an allocator-**call** count; nothing about the
program's one block per round moved:

    --- golden
    +++ tree
     "allocations": {
      "blocks": 0,
      "bytes": 0,
    -  "calls": 115,
    +  "calls": 54,
      "inside_blocks": 0,
      "inside_bytes": 0,
    -  "inside_calls": 101,
    +  "inside_calls": 40,
      "inside_ns": 0,
      "ns": 0
     },
    ...
    -   "alloc_calls": 54,   "calls": 18,   (tensor:multiply)
    +   "alloc_calls": 18,   "calls": 18,
    -   "alloc_calls": 15,   "calls": 5,
    +   "alloc_calls": 5,    "calls": 5,
    -   "alloc_calls": 9,    "calls": 3,
    +   "alloc_calls": 3,    "calls": 3,
    -   "alloc_calls": 6,    "calls": 2,    (tensor:zeros)
    +   "alloc_calls": 3,    "calls": 2,
     "allocations_per_round": {
      "blocks": 0.0,
      "bytes": 0.0,
    -  "calls": 27.0,
    +  "calls": 9.0,
      "ns": 0

`calls` (operator executions), `blocks`, `bytes` and the printed values of the program are
unchanged; `profile.alloc_step`'s own substantive checks ("one block payload per step (got 1.0)",
"4096 payload bytes per step", "the rows sum to the aggregate (9 vs 9)") all still pass.

## 3. Why the counts moved

`std::profile` counts entries into the allocator, attributed to the operator that is running, not
just the payloads a program asks for. Two of those entries per operator execution were the type
objects of the query:

- at `b43dd5c68` / on this lane's base, `Type::create(TypeCode)` (src/core/type/base.cpp) built a
  **new** object on every call, so every `Type::Float32()`, `Type::Int64()` and every dtype a
  module or a kernel states allocated; and `ArrayType::create` / `ArrayType::make`
  (src/core/type/composite/array.cpp) allocated an `ArrayType` per call, so `Tensor<f32, [3]>`
  written twice was two objects.
- under the ruling (item 1: one type per structure, all types through the table) both are entries
  of the type table: the second and every later query of the same type returns the object already
  there and allocates nothing. Two allocations per operator execution therefore disappear, which
  is the drop the numbers show: 54 -> 18 on 18 calls, 15 -> 5 on 5, 9 -> 3 on 3, 6 -> 3 on 2.

The lane reads this as the intended effect of the ruling rather than a defect: the program's own
allocations (the loop's one 4096-byte block per round, the aggregate 9 blocks / 36864 bytes) are
untouched, the operator-call counts are untouched, and the profile's allocation rows now count the
allocators the program actually enters.

## 4. What the lane did not do, and the question

Not done, on purpose: neither expectation was changed, neither golden was re-recorded
(`--update-golden` was not run), no `known_fail` was added, and the two cases were not moved. They
are expected to be red in the lane's gate run until this question is answered.

The question: are the expected allocator-call counts of these two cases (the hard-coded `4.0 calls`
prefix of `profile.alloc_step`, and the `calls` / `alloc_calls` / `allocations_per_round.calls`
fields of the two `fusion_group_none` goldens) to be re-recorded from this tree -- and, if so, by
which lane, since one of them is documented as captured from the base commit `b43dd5c68` -- or is
the fall of allocator entries an effect the author wants measured differently, for instance by
counting only payload allocations (`blocks` / `bytes`) in the report?