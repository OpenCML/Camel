# What an effect stands on: three credentials, and a second model that still counts the tag

## The change (P0-3)

`effectStandsOnSomething` (`src/execute/pass.cpp:1921`), the one function the pass boundary asks
whether an effect is still used, now counts **three** things and only three:

1. **a read of its value** -- a normal or `with` input of another node;
2. **the graph's own result** -- `exit` / `output` / `return`, which whoever runs the graph reads;
3. **a place in its block's chain, measured as an edge** -- a sync or control edge *to a node that
   runs after it* (`syncUsersOf` / `ctrlUsersOf`). The migration carries the chain as both kinds at
   once (docs/design/effects/edges.cn.md 5.3), so either is the same fact while both exist.

Two things it used to count and no longer does:

- **the `kGCNodeFlagSyncStatement` tag.** *A tag is a declaration, not a use.* The builder and every
  splice set it when they chain a node; a rewrite that drops the node's place in the chain leaves the
  tag set. Counting it let "this statement is in a block" survive the loss of the edge that makes it
  true -- the credential outlived the thing it stands for.
- **an order edge.** `order` joins statements of one block while the migration runs, but it is an
  order the *writer* put there, not a read. An effect whose only remaining edge is an order edge is
  one no chain makes run -- exactly the case design principles 3.1 refuses. The boundary check now
  refuses it too: `PassInternalError`, naming the pass.

The predecessors were dropped as well: an edge *into* a node says the node before it is placed, not
this one.

## Measured

The whole plan tree stays green with the narrowed credentials:
`[SUMMARY] total: 8354, pass: 8342, fail: 0, skip: 10, known failures: 2`. **Not one program in the
suite relied on the tag or on an order edge as its only credential** -- which is why the
over-generosity survived this long: it was never exercised, so it never showed.

## The acceptance case has no instrument

The case the ruling asks for -- "delete an effect's last reader, leave only an order edge, expect
`PassInternalError`" -- cannot be built from a test pass today. Measured: a pass that removes the
effect from the sync inputs of its successors (`setSyncInputs(user, keep)`) and hands the place on as
an order edge (`appendOrderDependency`) commits to a graph where the effect **still has
`syncUsers 1, ctrlUsers 1`**:

```
test::orphan_effect: effect 3 now has order edges only, onto 5
DEBUG graph 'main' node 3 normUsers 0 withUsers 0 syncUsers 1 ctrlUsers 1 orderUsers 0
```

The encoder rebuilds the chain from the block/region structure, so a whole-list edit of one node's
sync inputs does not survive publication. So the narrowing stands as a **refusal with no case**, and
this page says so instead of shipping a test that cannot fail. A case needs an instrument that can
produce a chain-less effect -- a draft-level "take this node out of its block" operation, which does
not exist.

## The second model -- and the hand-off the guard lane needs

The same question is answered **twice** in the tree:

| where | function | what counts |
|---|---|---|
| pass boundary | `effectStandsOnSomething` (`src/execute/pass.cpp:1921`) | data users, the graph's result, sync/ctrl **users** (as of this commit) |
| liveness | `effectStandingOf` (`src/runtime/effects.cpp:117`) over `EffectLivenessFacts` (`include/camel/runtime/effect_liveness.h`) | `hasDataUser`, `isSyncStatement`, `isWriterWithLaterRead` |

The liveness model's own header documents `isSyncStatement` as "it has a sync predecessor or successor,
**or the builder marked it as one (`kGCNodeFlagSyncStatement``)" -- so the tag still counts there, and
it counts **predecessors** too. Its callers are `fusion_group.cpp:91`, `gates.cpp:28`,
`keepshape.cpp:97`, `draft_inline.cpp:1757`, `graph.cpp:574`, `writings.cpp:1146`. They are not the
same question (liveness decides whether a transform may drop a node; the boundary decides whether a
pass's output is acceptable), but they overlap, and since this commit they disagree on the tag.

**Hand-off, measured, not patched:** with the narrowed credentials, an effect can stand on a
**control edge alone**. Over the whole fast gate that happens **6 times, at 3 distinct nodes in one
program** (`__root__` node 60, `arm` node 10, `main` node 63; the count repeats because the check runs
after every pass). Those effects' last credential is the control edge. If the guard lane's slice 3b
deletes the read side of the control edge, **those effects lose the credential this boundary accepts**
and the boundary would start refusing programs that are fine today. That is why this page stops here
rather than adding a fourth source: the next move belongs to that lane (give those effects a sync
place, or teach the boundary the post-3b spelling), not to a new credential invented here.

## No second write path for the boundary's answer

`git grep` for the boundary's question:

- `effectStandsOnSomething` -- one definition (`pass.cpp:1921`), one reader (`effectCredentialViolation`,
  `pass.cpp:2450`), one caller (`pass.cpp:3165`, the pass boundary). No other place answers it.

The liveness model above is a *different* entry, not a second write path into this one; uniting them
is the job the hand-off describes.