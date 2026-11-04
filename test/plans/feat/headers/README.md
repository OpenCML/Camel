# This directory holds no plan file

`fixture/` is the corpus `../docs_headers.plan.toml` runs its two cases against (it names
`test/plans/feat/headers/fixture` as `--root`). The plan is beside this directory, under another
name.

So `-Plan test/plans/feat/headers` (or `feat/headers`) selects no plan and is refused, by design.
Name a `.plan.toml`, or a directory that holds one.