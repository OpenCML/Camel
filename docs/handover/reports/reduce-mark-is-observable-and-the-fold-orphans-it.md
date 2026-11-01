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


## Second correction: the splice does carry the place -- fold drops it at the replacement

The previous section is wrong in its load-bearing sentence ("fold neither takes the reader nor produces
the shape"). Dumping the graph after \`std::opt::inline\` alone shows the truth: the tail effect **does**
get a place, and it is the splice's own gate:

\`\`\`
N56 [label=":mark/reduce_arr" ...]   N56 -> N5A [label="0|0", type="NORM"]
N5A [label="GATE", type="GATE"]      N5A -> N43 [WITH]      N47 -> N5A [label="5|0", type="CTRL"]
\`\`\`

That is \`spliceChainAnchor\` (draft_inline.cpp:1392) doing exactly what criterion 2 asked for: the body's
tail feeds a GATE, and **the call's chain place is written onto that gate** (the CTRL edge from N47).
The splice is not the defect.

After \`std::opt::fold\` the gate is gone and the reduce has **no** control input and no users -- so the
place was lost **at the replacement of the gate**, not at the splice. \`replaceNode\` (rewrite.cpp:314)
already has the machinery for this shape and says so in its own comment:

> "That is the whole shape of \`let r = (println(5) + println(6)) - 1\` with \`r\` unread: the sum is the
> block's value, the block's GATE waits on it, and the prints are reached only through the sum. The
> GATE is the end of the chain, not an unused node, and dropping the sum must leave it waiting on the
> prints themselves."

That is the same sentence as this defect, one level down: dropping the **gate** must leave it waiting on
the call. The \`inheritedWait\` / \`handOverSyncWait\` path is what carries a wait onto the producers when a
node is removed; the gate-collapse path in \`foldDraft\` reaches the node through \`isFoldable\` and
\`replaceFoldResult\`, and on this program the control input does not arrive at the call.

So the earlier \`isFoldable\` guard was aimed at the wrong node: it watched the call being folded (which
never happens) instead of the gate whose removal drops the wait. That guard stays reverted; the fix
belongs beside \`inheritedWait\` in \`replaceNode\`, and it is "a wait that a removed GATE carried goes to
what the gate's value came from", which is the paragraph the file already contains.


## Third round: which branch is missing the call -- and the reading that says "it never enters that branch"

Criterion 1 asked whether the \`inheritedWait\` machinery is **not called** or **called with the wrong
target**. The reading below says the first: the removal of this gate never goes through
\`replaceNode\` at all.

What \`replaceNode\` would have done with it, if it had been called:

- \`carriesOrdering\` (src/runtime/graph.cpp:1295) for an Oper is \`!nodeIsFreelySchedulable(draft, node)\`,
  which is true for \`pure = false\`;
- \`isObservedRun\` (rewrite.cpp:196) for an Oper is the same test, so true;
- therefore \`lastReadsGoingOut\` (rewrite.cpp:217) lists the reduce, \`keepThroughValue\` (rewrite.cpp:252)
  wraps the last reader through \`\:op/keep(x, v)\`, and \`inheritedWait\` (rewrite.cpp:367-384) hands the
  gate's control input on to it.

Every one of those says the reduce is an observed run. So if \`replaceNode\` had been the path, the node
would have kept a reader and the wait. It did not keep either, so the gate disappeared through a path
that is not \`replaceNode\`.

**The hypothesis that fits every measurement so far** (stated as a hypothesis, not as a reading):
\`pruneBranch\` (generic.cpp:1181) dissolves the branch whose condition is static, keeps the taken arm,
and erases the nodes of the other one with \`eraseNode\` (the calls at generic.cpp:858-862 are the same
family). The taken arm's value is the reduce; the arm's exit gate goes with the branch; and because
\`let n61\` is never read, the reduce is left with nothing. \`eraseNode\` closes the chain but has none of
\`keepThroughValue\`'s protection -- that lives in \`replaceNode\`.

**To settle it:** put the same one-off print in \`pruneBranch\` and in \`eraseNode\` (node kind, whether it
was a Gate with a control input, what its value source was) and run the two pipelines; whichever prints
for the gate tells which path to fix. That measurement was not taken in this round.

## The rule, as ruled: written here and in the design note

> **Folding away a GATE does not lose the value -- it loses the wait the gate was holding for someone
> else, and that wait has to land on the gate's producer.**

Where it belongs in the code is beside \`inheritedWait\` in \`replaceNode\`, or -- if the erasing path is
\`pruneBranch\`/\`eraseNode\` -- in that path, by routing it through the same protection.


## Fourth round: the print, and the one measurement still missing

Two one-off prints were added and removed (\`pruneBranch\` entry; every \`GraphDraft::eraseNode\` with its
kind, normal-input count and control-input count), then the two pipelines were run on
\`inline_crash_function_argument.cml\`.

- \`std::opt::inline std::opt::fold\` calls \`pruneBranch\` six times (BRCH nodes 2, 18, 29, 8, 4, 19) and
  erases **kind 13 (Gate)** nodes 39, 40, 41, 42 -- **all with \`ctrlIn=0\`**;
- \`std::opt::inline\` alone prints no \`pruneBranch\` and no erase for those nodes.

That \`ctrlIn=0\` is the reading that matters: a gate whose control input had already been unlinked when
it was erased. \`replaceNode\` unlinks the control users before erasing (rewrite.cpp:418-420), so the
order of events is consistent with the gate having gone **through \`replaceNode\`** after all -- the third
round's hypothesis (\`pruneBranch\` erasing the arm) is not what the print shows.

And then the predicates say the protection should have fired: \`orderedSources\` (rewrite.cpp:152) walks
**normal and with inputs** and keeps those that \`carriesOrdering\`, so the reduce is in \`lastReadsGoingOut\`;
\`keepThroughValue\` then wraps it through \`:op/keep\` unless the replacement already reaches it.

**The one measurement not yet taken** is inside that last step: for this (removed gate, reduce) pair, what
\`lastRead\` holds, whether \`reaches(source, value)\` was true, and what \`stood\` was. That print goes in
\`keepThroughValue\` and answers it in one build.

## Why this round did not change code

The fix is one line away **only if** the answer is "\`lastRead\` was empty" or "\`reaches\` was true".
Either answer changes which of the two branches to touch (the scan, or the value-reaches test), and a
guess would be a fourth wrong certainty. The rule stands and is unchanged:

> **Folding away a GATE does not lose the value -- it loses the wait the gate was holding for someone
> else, and that wait has to land on the gate's producer.**

## And the contract sentence, as ruled

> **Deleting a node has two paths, and only one of them keeps an observed run -- that is the shape of
> today's bug.**
\`eraseNode\` (src/runtime/draft.cpp:1674) removes a node and closes the chain; it does not look at what
the node was the last reader of. \`replaceNode\` (src/passes/opt/generic/rewrite.cpp:314) does. Every
pass that reaches for \`eraseNode\` directly is one that gets no protection.


## Fifth round: the gate is not removed by replaceNode either -- the print in it never fires for one

`replaceNode` was given an entry print (id, kind, replacement, sync-user count) and `eraseNode` a print
naming a gate's value source, then the failing pipeline ran again. `replaceNode` fires **four times in
the whole run, and not one of them is a Gate (kind 13)**. So the gate does not go through either path
that was suspected: not the `pruneBranch` dropped-arm hypothesis of round three, and not
`replaceNode` as round four concluded from the `ctrlIn=0` reading.

What the prints do establish: the four Gate erasures happen inside `pruneBranch` (the only place that
erases without `replaceNode` in this run, and it runs six times), and `pruneBranch` **closes the chain
over the nodes it drops but never asks what they were the last reader of** -- the same sentence as the
third rule below, now with the exact function.

The protection that is missing in `pruneBranch` is the one `replaceNode` has had all along:

```cpp
// An effect this node was the last reader of keeps running where the node stood ...
const auto lastRead = lastReadsGoingOut(transaction, removed, alsoGoing);
```

`pruneBranch` already has the mirror image of it for the **dropped** arm ("An effect among those reads
that only the dropped arm used runs where the branch stood", generic.cpp after the erases); what it does
not have is the same handoff for the **kept** arm's exit gate, whose value source keeps running only if
something still reads it.

## Handing over

This is the fourth diagnosis and no code change: `std::opt::inline std::opt::fold` still returns 1, and
per the parent's ruling this is the point to hand the live scene to another hand rather than a fifth
certainty. The scene, in the order it was earned:

1. `:mark/reduce_arr` is observable by declaration (`pure = false, .callsGraphs = true`).
2. `inline` gives its tail a place -- a GATE carrying the call's control edge (`spliceChainAnchor`).
3. The fold does not fold the call (`isPureOper` refuses `pure=false`); the gate is what disappears.
4. `replaceNode` would protect the last read (`keepThroughValue`, `inheritedWait`), and it never sees
   this gate: four calls in the whole run, none of them a Gate.
5. The erasure is `pruneBranch`'s, and that path has no "is this an observed run" question.

## Third rule, as ruled

> **`eraseNode` closes the chain but does not ask whether the node was somebody's last reader;
> `replaceNode` asks. Any pass that reaches for `eraseNode` directly gets no protection.**
