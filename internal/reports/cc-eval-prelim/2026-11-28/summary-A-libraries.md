# RQ3 · library size (`tab:libraries`) — preliminary

`tip fd950804d6` (origin/develop) · build host `zjwei@camel-work` · load ≈40 (1m) at read time ·
2026-11-28 · **preliminary, not final**.

- **What was read.** Line counts per library area, measured with `Get-Content … Measure-Object -Line`
  over `git ls-files` globs (a `wc -l` equivalent; **lizard/cloc were not used**, so the numbers are
  physical lines including comments and blanks — the instrument is stated here as the plan page
  requires). Rule counts are `git grep -c '^export func'` over the ruleset `.cml` files.
- **Numbers.** autodiff C++ 12131 · autodiff rulesets (Camel) 4071 with **43 `export func`** ·
  ONNX export 12880 · rewrite 6399 · `std/cxx` 8304.
- **Direction against the design.** Consistent with the design's claim that the export and autodiff
  libraries dominate and that the Camel rulesets are a small fraction of the C++ they replace
  (4071 vs 12131 = 34% of the lines, at 43 declarations).
- **Unexpected.** (a) `exporter.cpp` alone (5738) is larger than the whole rewrite library;
  (b) two categories in the plan page could **not** be measured in this tree — nothing matches
  `quant`, and there are no `.cml` files under `std/cxx` — recorded as "—" with the reason rather
  than as zero, per the rule "写不出来就记原因、表里写 —".
