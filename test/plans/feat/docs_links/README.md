# This directory holds no plan file

It is the corpus `.../docs_links.plan.toml` reads -- `fixture/snippets` for the code-snippet cases
and `fixture/symbol` for the ones about a link that points at a symbol. The directory carries the
plan's name because the fixtures belong to the plan, not because a plan lives here.

So `-Plan test/plans/feat/docs_links` selects nothing and is refused, by design: the plan of that
name is beside this directory, as `test/plans/feat/docs_links.plan.toml`.