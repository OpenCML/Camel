# This directory holds no plan file

It is the reduced tree `../docs_ai_reference.plan.toml` runs its guard over -- `fixture/docs`,
`fixture/src` and `fixture/include` stand in for the repository the `camel-for-ai` page describes, at
a size whose total can be exceeded on purpose. The directory carries the plan's name because the
fixtures belong to the plan, not because a plan lives here.

So `-Plan test/plans/feat/docs_ai_reference` selects nothing and is refused, by design: the plan of
that name is beside this directory, as `test/plans/feat/docs_ai_reference.plan.toml`.