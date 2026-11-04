# This directory holds no plan file

It is golden output, not a plan: `examples/` belongs to `../examples.plan.toml` and
`tutorial_examples/` to `../tutorial_examples.plan.toml`, which name their files as
`golden/examples/...` and `golden/tutorial_examples/...`.

So `-Plan test/plans/feat/golden` (or `feat/golden`) selects no plan and is refused, by design.
Name a `.plan.toml`, or a directory that holds one.