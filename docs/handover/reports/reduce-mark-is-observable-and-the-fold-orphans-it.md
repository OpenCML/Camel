# `:mark/reduce_arr` is observable by its declaration -- and the fold leaves it with no readers

## What the declaration says (criterion 1, answered from the words)

`src/builtin/executors/builtin.cpp:655-660` registers the higher-order marks together:

```cpp
    // The higher-order operators and function composition run a function the program passes
    // in: what they do is what that function does, and the passes that look at them look at
    // that function. exit and sleep are the process module's and are built in as well.
    for (const char *uri :
         {":mark/map_arr",
          ":mark/filter_arr",
          ":mark/reduce_arr",
          ":mark/foreach_arr",
          ":mark/unordered_reduce_arr",
          ":mark/unordered_foreach_arr"}) {
        registry.set(uri, {.pure = false, .callsGraphs = true});
    }
```

`pure = false` is the declaration's own word for "this may have effects", and the comment above the
table in the same file says what an absent declaration would mean: "an operator without a declaration
is treated the same as one declared to have effects, but only a declaration says that somebody
looked." So `:mark/reduce_arr` is **observable by declaration** -- the fold may not simply drop it
because its value is unread. Criterion 3 is the branch that applies: it needs **a place in the
chain**.

## What the node actually is, measured

The refusal names node 42 of `__root__` in
`test/cases/opt/progen/findings/inline_crash_function_argument.cml` after `std::opt`. Probing the node
where the violation is built (one build, probe removed afterwards):

```
NODE42 origin=4918 normIn=1 withIn=2 op=':mark/reduce_arr'
  input=39 op=':struct/concat_arr'
```

and the credential counts from the earlier round, on the same node:

```
norm=0 with=0 syncUsers=0 syncPreds=0 ctrlUsers=0 ctrlInputs=0 orderUsers=0 orderPreds=0 tag=0
```

Read together:

- **Its inputs are intact** -- one normal input (the array, `:struct/concat_arr`, i.e. the
  `concat([...])->map(...)` chain) and two `with` inputs (the reducing function and the initial value
  `0`). Nothing stripped what it consumes.
- **It has no users and no place** -- not a data reader, not a sync or control successor, not an order
  edge in either direction, no tag.
- `origin=4918` is the same origin the `std::opt::inline` dump shows for the mark nodes in the function
  graphs (`span="12:99-12:138"`, the `reduce` on line 13), so this is **the source reduce moved into
  `__root__`**, not a node fold invented.

So the shape is: **fold replaced the reduce's result with a constant and took away its last reader,
leaving the node that performs the call with its inputs still attached and no place to run from.**
The value is dead; the call is not, because its declaration says it may have effects.

## What this means for the fix, and why this commit stops

Criterion 3 says "connect it into the chain -- do not leave it both present and unconnected", and that
is what has to happen; criterion 2 (delete) is **not** open here, precisely because the declaration
says `pure = false`.

What is not yet established is **where** fold should re-attach it, and that is the fold rewrite's own
decision, not something to be guessed at the boundary: re-attaching it wrongly would make the program
run a call at a different point in the chain than the source put it, which is worse than a refusal
that names the node. No helper for this exists yet (`ensureEffect*`, `placeInChain`, `attachEffect`,
`effectPlace`: zero hits over `src` and `include`), so the fix is a new decision inside
`src/passes/opt/generic/generic.cpp`, not a call to something already there.

**This is a partial delivery and it says so.** What is delivered is the answer to criterion 1
(observable, quoted) plus the measurement that localises the defect to the fold's treatment of a
reduced-to-constant higher-order call. The code is unchanged: the tree is at `31ee71694`, and

```
git grep -c "yields node" src        -> src/execute/graph_runtime_support.cpp:1
git grep -c "brchTargetMap" src      -> src/passes/sched/fastvm/compile.cpp:5
```

are the same as before this round -- no check was widened, and the `Guarantee` was not touched.

## The location defect fixed in the previous commit was the same node

`31ee71694` (sentence said `:12:99`, caret said `13:100`) was found through this same refusal: the
span is `12:99-12:138` in `Range::start`'s 0-based coordinates, i.e. `13:100` for a reader.

## Correction, next round: it is not fold that takes the reader -- it is inline

The bisect says it plainly (same program, same binary, one pass at a time):

| pipeline | result |
|---|---|
| `std::opt::fold` | **rc = 0**, no output |
| `std::opt::inline std::opt::fold` | **rc = 1**: `Pass 'std::opt::fold' failed ... graph 'main' has effect node 36 (':mark/reduce_arr') ...` |
| `std::opt::inline` | rc = 0 |
| `std::opt::dce` | rc = 0 |

So `fold` alone is innocent: it neither folds the `pure = false` call (it cannot -- `isPureOper` refuses
it at the top of the loop) nor takes its reader. **`inline` produces the shape**: it splices `f1`'s body
into the caller, the body's tail is the `reduce`, and in the destination that tail has no reader and
**no chain place carried over**. `fold` is only the first pass whose boundary looks at the result.

A guard was implemented and measured against this and it does not fix it -- `isFoldable` refusing to
fold "the last reader of an observable call" leaves the node exactly as orphaned as before, because
the reader was already gone when fold started. It was reverted; the tree is unchanged.

**The next entry is `src/runtime/draft_inline.cpp`**: when a spliced body's tail effect has no reader
in the destination, its place in the chain has to come across with it (or the call site's own
consumer has to be reused). That is the same question as "who gives an unread effect its place",
which the builder already answers when it builds a block -- the inliner is the one place that does
not.

Note the graph name differs by route (`main` after the two listed passes, `__root__` inside
`std::opt`), which is the same node seen in the graph that holds it at that point, not two nodes.
