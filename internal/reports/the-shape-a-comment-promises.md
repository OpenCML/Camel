# The shape a comment promises has to be reachable

## What happened

`portDeclarationViolation` (src/execute/pass.cpp) opens with a promise:

> Every operator node still matches the declaration it names: an operator the table knows, ordinary
> ports that are live nodes with a type, and no more of them than the declaration's operands. A port
> the declaration does not have, **a port pointing at a node that is gone**, or a port without a type
> is a call nothing can run.

The middle case had never been exercised. Reaching it, the code was written this way in
`test::dangle_port`: an operator's first port set to `kInvalidNodeRef`. The result was not the
diagnostic the comment promises:

```
test::dangle_port: operator node 2 port 0 now reads a node the graph does not have
bash: Segmentation fault (core dumped)     # exit 139
```

The transaction that carries the graph into a version reads the node at each port, and there is no
node at that reference. The commit crashed, and the guard the comment describes never ran. So the
promise covered a shape that could not exist as far as that function was concerned: any graph with a
dangling port dies in the encoder first.

## What was done

`RuntimeGraphDraftSession::validateDraftPorts` runs at the top of `RuntimeGraphDraftSession::commit`
and refuses the draft with a plain exception, which the pipeline reports against the pass that made
the graph:

```
error[RuntimeDiag::PassInternalError]: Pass 'test::dangle_port' failed with an internal error: an
ordinary port of operator node 2 of graph 'main' reads port 0 of a node the graph does not have
(4294967295).
```

Exit code 1, no crash, the pass named, the graph named, the node and the port named. The comment on
`portDeclarationViolation` now says which half it sees (a dead node that is still referenced, a node
without a type) and which half is refused earlier, by the commit, with the reason.

## The rule

**A comment that promises a shape the code cannot be handed is a false comment**, and it is worse
than no comment: it tells a later reader the case is handled when the case is a crash. The check is
cheap -- write the shape and see what happens -- and it has to be part of proving a guard, not an
afterthought to it. Two other findings of the same family, from the same round:

- A message that no case prints is a message nobody has read. Both `PassBoundaryNodes` and
  `PassBoundaryPorts` printed broken sentences (`... whose graph has no graph 1 of it has no exit
  node ...`) until a case made them print, and the arguments had been wrong in the source the whole
  time.
- A name that appears in a `switch` label is not a producer. `case RuntimeDiag::FileSystemError:`
  kept a dead code looking alive in the producer scan; the label classifies a code, it does not
  construct one.

The common shape: **the thing that reads a promise is the test, not the comment.** Coverage counted
by name does not read messages; a producer counted by any occurrence of a name does not read labels;
a guard counted by existence does not read its own preconditions.