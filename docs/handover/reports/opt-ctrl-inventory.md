# What `std::opt::ctrl` actually does, item by item

Written before renaming or splitting it, because the name says one thing and the body does seven.
`reduceControlDependencies` is `src/runtime/control.cpp:135-310`; the pass that calls it is now
`std::opt::order` (`src/passes/opt/generic/generic.cpp`, twice: the pass and the simplification
round's finish step). Both call sites also run `reduceOrderDependencies` first.

| what it does | where | whose job in the three-edge model |
|---|---|---|
| 1. Drops waits that order nothing: a `Port` or `Data` predecessor never runs, so waiting for it says nothing | 147-167 | nobody -- a wait on a value that never runs is not a wait. Disappears with the kind. |
| 2. Keeps a GATE's single control input while that input *is* its wait (no sync predecessor) | 160-162, 287-296 | the sync edge: 5.3 gives the GATE a sync predecessor, and a gate with one needs no control input. This is the rule the second `known_fail` asserts. |
| 3. Budgets the sweep from node and edge counts, so a huge graph cannot stall a pass | 168-178 | the same pass, unchanged. |
| 4. Protects boundaries and `LOOP` from the walk; lets a `JOIN`/`LOOP` be a proof's endpoint but never a step | 175-176, 246-248, 271-278 | the order/`sync` reduction keeps the same shape; nothing here is control-specific. |
| 5. **Value-path reduction** (sweep 1): a control edge is redundant when the target's own value inputs already reach its predecessor | 199-306, `reduce(false)` | `std::opt::order`: this is the transitive reduction, read over data edges instead of control ones. |
| 6. **Sync-aware reduction** (sweep 2): the same proof, reading a block's chain as an ordering, skipping `GATE`/`FUNC`/`CALL` targets and any node with a single wait | 199-306, `reduce(true)`, 207-218 | nobody: once the chain is the sync edge, the control edge beside it is not a second ordering to reduce, it is a duplicate to stop writing. |
| 7. **Replaces rather than only deletes**: when the whole set falls away and the target is a gate without a sync predecessor (or has no value inputs), `roots.back()` is put back as the one wait | 291-296 | the builder, which is what knows where a statement's chain ends. **This is the duty that is not garbage**: it moves a wait to a different predecessor, and that moves execution. |

## Why it cannot be renamed in one commit

Measured: deleting the two `reduceControlDependencies` call sites and leaving
`reduceOrderDependencies` alone passes 5,250 of 5,257 and turns five cases red -- four of them
assert the old model (`control_data.graph`: "data-covered controls survived"; `covered_value.graph`:
"unexpected control dependencies"; `control.pipeline_contract`: `sum(kind=="control") == 4`) and
one of them changes what a program prints (`var_read_before_region.opt_nvm`). Items 5 and 6 are
removal and can go with the kind; item 7 is a reordering, and the printing case is item 7.

So the plan is one item per commit, item 7 first: move the reordering into the place that owns it,
and check that `var_read_before_region.opt_nvm` keeps its output on every commit in between.
The four old-model assertions are the record of the old model and are not edited until the new one
stands; a commit that changes them early has deleted the evidence instead of replacing it.