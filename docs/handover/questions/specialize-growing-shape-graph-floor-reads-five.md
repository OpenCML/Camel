# `specialize_growing_shape.fold_specialize.bounded`: the floor `stats.graphs > 5` reads exactly 5

Question raised by the lane `handover/h499-type-identity` (H499) on 2026-10-25, on its tree tip
`15807f113` (the type-identity ruling of 2026-10-24 evening, items 1-3, implemented in
`c92e27373`, `0b9c300cd`, `15807f113`). Everything below is verbatim from that tree. The lane
changed no expectation: this file is the measurement and the question.

## 1. What is red

`test/plans/feat/opt/generic.plan.toml:824-840`:

    name = "specialize_growing_shape.fold_specialize.bounded"
    desc = "The program after std::opt::fold std::opt::specialize holds one type specialization of
    the recursion (99 graphs in all: the branch arms and what they call; a recursive call inside an
    instance of the function it names keeps the caller's parameters instead of taking a copy, so no
    extent makes the copies grow without end), and still prints the zero-pass output. The count is
    no longer a bound: the eight-copy limit this used to assert was truncation stops nothing now
    (the ruling of 2026-10-24 evening, item 3), and the reading is the fixed point -- one
    specialization, not a shape cut to a count."
    args = ["${case}", "std::opt::fold", "std::opt::specialize", "std::stats", "std::fvm"]
    verify_script asserts:
    assert(stats.specializations === 1, `expected one type specialization, got ...`)
    assert(stats.graphs > 5 && stats.graphs <= 120, `unexpected graph count: ${stats.graphs}`)
    assert(out.endsWith('[12, 2]\n36.0'), ...)

The plan runner, on this tree (`test/plans/feat/modules/h499probe.plan.toml` is the case copied
verbatim, so that a manual reading is affordable; that scratch plan is not in the final tree):

    [FAIL] h499probe.specialize_growing_shape.fold_specialize.bounded 127.05 ms
        [INFO] verification exited 1
        [INFO] verification output: Error: unexpected graph count: 5
        [stdout] GRAPH_STATS {"graphs":5,"nodes":52,"edges":55,"opers":13,"direct_calls":4,
                  "indirect_calls":1,"closures":0,"branches":3,"loops":0,"bytes":1972,
                  "specializations":1}
        [stdout] [12, 2]
        [stdout] 36.0

`stats.specializations === 1` holds and the printed output is the one the case expects; only the
floor `stats.graphs > 5` fails, and it fails by one (`5 > 5` is false).

## 2. The readings

The same scratch plan, before the fix (the base of this lane, `3d4d418b4`; the runner prints stdout
only for a failing case, so the baseline number is not in its log -- the case is `PASS` there):

    [PASS] h499probe.specialize_growing_shape.fold_specialize.bounded 232.74 ms
    [PASS] h499probe.export_fold 10.03 s (timeout)
    [FAIL] h499probe.rules.tensor_saturation_keeps_values 284.86 ms

After the fix, run by hand against `out/latest` on the host (Release, this tree), one pass
pipeline at a time -- `test/cases/opt/generic/specialize_growing_shape.cml`:

    std::stats std::fvm
      GRAPH_STATS {"graphs":3,"nodes":26,"edges":29,"opers":9,"direct_calls":2,"indirect_calls":1,
                   "closures":0,"branches":1,"loops":0,"bytes":1028,"specializations":0}
      [12, 2] / 36.0
    std::opt::fold std::stats std::fvm
      GRAPH_STATS {"graphs":3,"nodes":26,"edges":27,"opers":7,"direct_calls":2,"indirect_calls":1,
                   "closures":0,"branches":1,"loops":0,"bytes":988,"specializations":0}
      [12, 2] / 36.0
    std::opt::fold std::opt::specialize std::stats std::fvm
      GRAPH_STATS {"graphs":5,"nodes":52,"edges":55,"opers":13,"direct_calls":4,
                   "indirect_calls":1,"closures":0,"branches":3,"loops":0,"bytes":1972,
                   "specializations":1}
      [12, 2] / 36.0

## 3. Why the floor is stale, in the plan's own words and in the numbers

- The program has 3 graphs before specialization and 5 after; the one specialization adds the
  instance and what it calls (2 graphs), and **nothing grows with the extent**: `graphs = 3 + 2`
  with `specializations === 1` is the fixed point the `desc` describes. A count that no longer
  grows cannot have a lower bound other than the pre-specialization count.
- The `desc` itself states the count "is no longer a bound ... the reading is the fixed point -- one
  specialization, not a shape cut to a count", and it cites **99** graphs for the same reading.
  `> 5` and `99` cannot both be the intended number; the floor was a sanity floor for a count that
  has since moved, and the fixed point now reads 5.
- Before the fix the same case passed (`graphs > 5` held); after it the reading is exactly 5 and
  every other assertion of the case (one specialization, `[12, 2]` / `36.0`) still holds. The
  companion case `specialize_growing_shape.fold_specialize.nvm` is unaffected: it asserts only the
  printed output.
- The lane that owns the count (`H493`, `docs/handover/reviews/questions-2026-10-24-evening.md`,
  the type-identity ruling) predicted this reading for the fixed tree: "exactly 5 after the fix,
  while `specializations === 1` still holds".

## 4. What the lane did not do, and the question

Not done, on purpose: the expectation was not changed, no `known_fail` was added, the case was not
moved out of its tier or plan, and nothing was deleted. The case is expected to be red in the lane's
gate run until this question is answered.

The question: is the fixed point of `std::opt::fold std::opt::specialize` on this program
`graphs = 5` (3 before specialization + 2 for the one instance), so that the expectation should
read the fixed point (`stats.graphs === 5`, or a bound that admits it) rather than a floor of 6 --
or is the fall of the count from the truncation-era reading to 5 itself a defect of the
specialization under the new type identity, which this lane should bring back up before the
expectation is touched?
