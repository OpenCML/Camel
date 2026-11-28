# Finding: `main`'s runtime graph is refused by the edge invariants for a cycle of inputs

`23b0204cf9` (`lane/progen`) · build host `zjwei@camel-work` ·
`[load]` 39.50 → 32.43 (1m) during these runs · 2026-11-28 ·
**program legal, front end accepts it, the runtime graph breaks a documented invariant of the store.**

- **Seed**: `39`, default feature set, `generate.VERSION` 9.
- **Where it fails** (the compiler's own check, not a pass): runtime graph `main`, reported as
  `InternalDiag::UnknownInternalError` at `1:1` (the invariant is not located in the source).
- **Rule**: the edge invariants, `docs/design/graph-store` 5.4 — "no cycle of inputs". The check is
  `src/kernel/runtime/graph_edges.cpp:800` (`EdgeChecker::findCycle`), which reports through
  `src/kernel/runtime/graph_edges.cpp:811` and the message at
  `src/kernel/runtime/graph_edges.cpp:1004`.
- **Outcome**: `RUN_ERROR` on **every** one of the seven stage prefixes measured
  (`none`, `macro`, `inline`, `fold`, `cse`, `algebra`, `dce`, each ending `std::nvm`): the graph is
  already wrong when it is first committed, so no pass is implicated.

## The diagnostic, verbatim

```
InternalDiag::UnknownInternalError at 1:1 | An error has occurred: Runtime graph 'main' breaks its edge invariants (1 violation(s)):
  graph 'main' node 7 (11): lies on a cycle of inputs: 7(11) <- 27(8) <- 25(4) <- 19(10) <- 17(4) <- 7.
```

Node kinds above: `11`, `8`, `4`, `10`, `4` are the `GCNodeKind` numbers of the five nodes on the
cycle; the first node is both where the cycle closes and the one the message names.

## The program, verbatim (21 lines, the reducer's output)

Shrunk by `test/tools/progen/reduce.py` (the tree's own reducer, `progen.py reduce`) until no single
further removal reproduced the same signature. The `import` and the `type` declaration cannot be
dropped: removing the import turns the outcome into `UnresolvedReference: 'get_env'`.

```
import { get_env } from env

type P = { x: int, y: int }

func q37(a38: int, a39: bool) => bool sync {
    return false;
}

with <k44: int, k45: string>
func w43(a46: float) => int {
    return 0;
}

func main() sync {
    let n55 = int((get_env('PROGEN_I0') ?? ''));
    let n57 = w43<0, ''>;
    if n57(-0.5) >= 0 then println('');
    var v58 = n55;
    let n62 = q37((if false then null else v58) ?? 0, false);
    println('L4 {}'->format<v58>);
}
```

## How to reproduce

The program is written out above and needs no generator; with `PROGEN_I0` unset it still fails.

```powershell
$env:CAMEL_HOME = "D:\Projects\Camel\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
camel --diag-format json seed39.cml std::nvm
```

From the tree, the same program regenerates deterministically at `generate.VERSION` 9
(`test/tools/progen/generate.py:65`); a later bump re-points the seed, which is why the source is
inlined here:

```powershell
python test/tools/progen/progen.py show --seed 39
python test/tools/progen/progen.py reduce --seed 39 --cell default --out <dir>
```

## Reading

- **The program is legal.** The front end type-checks it (`main` declares no return value, the arms
  of the `if` agree, `w43<0, ''>` fixes both context parameters), so this is not a refusal of a
  shape the language does not have.
- **The engine says its own store is broken.** "breaks its edge invariants" is the store's
  self-check (`src/kernel/runtime/graph_build.h:214`: both checks run "so that every state is
  checked by this one"), not a back end reporting that it cannot run something.
- **The cycle is among `:op` nodes inside `main`'s body.** `node 7`...`17` with kind `11`/`8`/`4`
  closing on `7` means a sync (ordering) edge loops back: the input of one operator is, through the
  chain, its own input. A node whose input is its own input cannot be ordered, which is why a back
  end that ran it would hang or read a stale value.
- **Nothing in the generator asks for this shape.** The program is drawn from the ordinary
  productions (`if` with a value, `??` on an optional, a `var` read through it, a `with`-function
  call with a float argument, `format`); it does not use anything the records of this directory
  mark as unusual, and the reducer could not remove the cycle by removing any single construct.
- **What is not claimed here.** Whether the cycle is created by the GIR builder
  (`src/compile/src/gir/`), by the encoding step, or by a specific rewriting rule is **not**
  determined: the invariant fires at commit, after whatever produced the graph. Bisecting that is
  the owner's next step, and the 21 lines above are the unit to bisect on.