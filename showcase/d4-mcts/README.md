# D4 — Monte-Carlo tree search (tic-tac-toe)

A real MCTS: `--arg playouts=N` playouts, each walking from the root to a node it can expand,
expanding one child, finishing the game with a light policy, and carrying the result back to the
root; the move is then chosen from the visits and the means, and the root's statistics are printed.

What this example shows is **a tree written with value semantics**: `Node` is a struct and its
children are a `Node[]`, and `visit` returns a new tree rather than modifying one, so the same
arguments (a seed and a playout count) print byte-identical output on every back end — which is
what lets "the search is right" be an assertion instead of a look at the numbers.

## Scope

D4 in H13 (`docs/design/showcase/README.cn.md`) reads **a simplified AlphaZero: MCTS + a
policy/value network + self-play**. This example does only the **deterministic half** of that, and
does it completely:

- done: UCT tree search, playouts, the light policy (win if you can → block → otherwise a place
  from the generator), reproducible on all four back ends;
- not done: the policy/value network and self-play. That is a narrowing of **scope** (compute and
  length), not a language gap: `modules/nn` and autodiff can already train a small network and the
  search half would be reused as is; growing the example until it needs training is what does not
  belong in a showcase that runs in a fast plan. The search half also needs no runtime conversion of
  an array into a tensor (H13's Q7 / H17's P2–P4), so it does not depend on those steps landing.

## Layout

```
showcase/d4-mcts/
├── search.cml                the Camel program (341 lines, MCTS)
├── baseline/mcts.py          the Python counterpart (the same search, stdlib only)
├── compare.py                runs both, diffs them line by line, times them, writes RESULTS.md
├── RESULTS.md                measured results of the `small` profile
├── README.cn.md
└── README.md
```

## The search

A position is `State = { board: int[], to_move: int }`: `board` holds nine cells, `0` empty, `1`
X, `2` O. The eight winning lines are in `LINES`.

- **A playout** (`playout`): while the position is not over, play `policy_move`; then return
  `result_for`: 1.0 for a win, 0.5 for a draw, 0.0 for a loss, always from the root player's point
  of view.
- **The light policy**: first a line the player can complete now (`completing_move`), then one the
  opponent could (block), and only otherwise a place chosen by the generator,
  `places[draw % len(places)]`. It is good enough that the search finds a one-move win and the only
  move that does not lose without enumerating anything.
- **UCT**: `score = mean + 1.4142135623730951 * sqrt(log(parent.visits) / visits)`. A child's
  `score` is kept from the point of view of whoever moves after it (the parent's opponent) and the
  sign is flipped on the way back, so the root player always sees its own expectation.
- **Expansion order**: children are built strictly in `moves` order (empty places, smallest
  first); while `len(children) < len(moves)` the next unbuilt one is expanded, and once they are
  all built the visit descends by UCT.
- **The move**: the child of the root with the highest mean, ties to the smallest place
  (`best_move` scans forward with a strict `>`).
- **Determinism**: the numbers come from a counter-based Lehmer generator,
  `next_key(k) = (k * 48271) % 2147483647`, and every node has its own key (a child's is derived
  from its parent's), so there is no global state, no clock and no hash order: the same arguments
  are necessarily the same tree.

In numbers: 5000 playouts on the empty board build 4929 nodes and all 5000 are counted in
`root_visits`; the centre has the highest mean (0.662403) and the search picks it.

## Running it

```powershell
$env:CAMEL_HOME = "out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel showcase/d4-mcts/search.cml --arg seed=42 --arg playouts=2000 --arg position=XX.OO.... std::nvm
camel showcase/d4-mcts/search.cml --arg seed=7 --arg playouts=5000 --arg position=......... std::jit
python showcase/d4-mcts/compare.py --profile small        # run both, diff every line
python showcase/d4-mcts/compare.py --profile standard     # writes RESULTS.md
```

`position` is nine characters, `X`, `O` or `.` (empty), X to move; `--arg to_move=` defaults to
the side implied by the marks (equal counts mean X). Those are the only three arguments it reads.

## Results (`small`: 2000 playouts, 5000 on the empty board, seed 42)

Camel and the Python counterpart print **the same lines** — this is not a tolerance comparison:
the tree is a value and both compute the same tree.

| case | position | best | mean of the chosen child | Camel s | Python s |
|---|---|---|---|---|---|
| a win now | `XX.OO....` | 2 | 1.000000 | 0.209 | 0.108 |
| the only block | `.X..O.X.O` | 0 | 0.967624 | 0.315 | 0.220 |
| a fork | `X..OX...O` | 2 | 0.944802 | 0.438 | 0.151 |
| the empty board | `.........` | 4 | 0.662403 | 1.606 | 0.494 |

The child that wins now is worth exactly 1.0: the position is over once you move there, so every
playout through it is the game's result. The fork is worth about 0.94: the tree also explores
moves below the fork, and those playouts are worth less — that is how MCTS values a move, not a
mistake (`test/plans/feat/showcase_d4.plan.toml` asserts both the 1.0 and the "below 1.0").

The four back ends (`std::nvm`, `std::fvm`, `std::jit`, `std::prl`) print byte-identical output on
all four search cases; the full tables are in `RESULTS.md`.

**No time is a conclusion here**: those seconds are whole-process wall time and are reported for
information. Python is the faster side (0.151 s against 0.438 s on the fork) because a Python node
is one `__slots__` object and a board is a `list` copy, while Camel copies the whole `State` (a
nine-int array and an int) on every `apply_move` and goes through the graph scheduler. What this
example is here to show is that the same search is the same tree on both sides, not who wins.

## Side by side

| thing | Camel | Python |
|---|---|---|
| a position | `type State = { board: int[], to_move: int }` | `(board, to_move)` |
| a node | `type Node = { key, state, place, visits, score, moves, children }` | the same fields, `__slots__` |
| children | `children: Node[]` (a recursive struct) | a `node.children` list |
| a move | `var next = state; next.board[place] = …` | `played = list(board); played[place] = …` |
| expansion | `grow(node, child)` returns a new node | `node.children.append(child)` |
| a playout | `loop current = state, draw = key while … do { … }` | `while win == 0 and not full(...)` |
| the move | `loop chosen = 0, index = 1 while … do { … }` | `for index in range(1, …)` |
| the generator | `(key * 48271) % 2147483647` | the same expression |

In Camel's own idiom the four helpers that change a tree (`apply_move`, `record`, `grow`,
`put_child`) write their path assignments in a sync body (`: T sync`), so their callers just
follow the written order (probe-verified). `visit` and `search` are pure: they return a new tree, which is why the four
back ends produce one output.

## What went wrong

- **`[]` inside a struct literal** (the one language defect this example met): when a struct
  literal has a field whose value is a **variable** of a named struct type next to an `[]`, the
  `[]` is inferred as `void[]` and the literal is no longer of the declared type:

  ```camel
  type State = { board: int[], to_move: int }
  type Node = { state: State, children: Node[] }

  func main(): int sync {
      let state: State = { board: [0, 0, 0, 0, 0, 0, 0, 0, 0], to_move: 1 }
      let node: Node = { state: state, children: [] }   // <-- this line
      println(node.state.to_move, len(node.children))
      return 0
  }
  ```

  ```
  [Error]: This value has type '{ …, children: void[] }', which is not the declared type 'Node'
      (name=SemanticDiag::…, on the line of the `Node` literal)
  ```

  The trigger the bisection found is that sibling field: `{ value: 4, children: [] }` (all
  constants), `{ value: 1, moves: [1, 2], children: [] }`, and a six-scalar-field struct all
  compile; so does holding the empty array in a typed local (`let none: Node[] = []`) and writing
  that in. The example does the last of those in `open_node`: **that is a workaround**, and it does
  not dodge the defect itself.

  A review has already decided the rule that applies here: an empty array literal `[]` takes its
  type from context, like any other literal (5.1), and that one need not wait for H23. The
  literal-only forms above do get `Node[]`; the position with a variable sibling does not and
  becomes `void[]`, so this is that rule not reaching this position rather than a missing feature.
  The fix is for `[]` in a struct literal to be inferred from the declared field type throughout.
- **A `return` inside an `if`/`match` arm does not leave the function**
  (`SemanticDiag::ExitInUnusedBranch`): three `return`s in `visit`'s arms produced two warnings;
  one `return if … then … else if … then … else …` expression is clean.
- **A path assignment belongs in a sync body**: otherwise
  `The call of '<__assn__>: (var State, State) => State' is not in a sync block`.
  The four helpers write it in a `: State sync` body. The order inside a sync body does not infect
  its callers (probe-verified), so `playout` needs nothing extra.
- **A `loop` state cannot be written as an empty array literal**: `loop plays: int[] = [], …`
  fails with `Expected '=' before ':'` (no type annotation there) and `loop plays = []` fails with
  `LoopStateWithoutType` (`[]` has no element type). No loop in the tree needs one; the probe
  collected the moves as text instead.
- **Scope**: no policy/value network and no self-play, for the reason above. Nothing in
  `search.cml` is weakened to dodge a defect — the search is a search: 5000 playouts build 4929
  nodes.

## Tests

- `test/plans/feat/showcase_d4.plan.toml` (fast): the four search cases on all four back ends plus
  three refusals (no playouts, an unreachable position, a finished position), 19 tests. The whole
  standard output is pinned, and `verify_script` asserts the properties that move if the search is
  wrong: the winning move is worth exactly 1.0 and no other move is, the children's visits sum to
  `playouts`, the centre is the highest-valued move on the empty board, and a search that examined
  nothing must exit non-zero and **not** print a move.
- `test/plans/perf/showcase_d4.plan.toml` (slow): runs `compare.py --profile small`, about eight
  seconds; one differing line fails it.

## Not committed

`artifacts/` and `__pycache__/` are excluded by `.gitignore`; `RESULTS.md` is committed.