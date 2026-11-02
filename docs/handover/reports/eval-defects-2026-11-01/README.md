# Defects found by the dry run (pin adb67e081)

> 来源：一次独立的评估试跑（pin `adb67e081`，11-01 早上的 develop），每条 3/3 复现。决策者 11-01 20:40 转交。**先在今天的 HEAD 上复跑**，已经修好的划掉；没修的按 `reviews/questions-2026-10-29.md` 同日 20:40 那条的排序处理。文中 `E2`/`E6` 等是评估内部的实验编号，可以忽略；`results/adb67e081/...` 路径对应本目录下的 `minimal/`、`e9_reduced/`。

Every entry below was re-run three times on the pin by `scripts/verify_defects.py` (`results/adb67e081/defects_verify.json`)
and reproduced 3 of 3, except where an entry says otherwise. Nothing here has been triaged by the development side. "Stage" is
the pass or phase that fails; "back end" is the back end of the failing command. Commands run as
`CAMEL_HOME=/work/pins/adb67e081 /work/pins/adb67e081/bin/camel <file> <args>`. Minimal programs are in
`results/adb67e081/minimal/` and `results/adb67e081/e9_reduced/`. IDs are local to this file.

## A. Compiler and pass defects (Camel)

### A1. `std::cml` crashes with SIGSEGV
Stage `std::cml` (printer), no back end. Found by E2 (`test/cases/compile/lambda_inference/positions.cml`), reduced by `scripts/ddmin.py` to 9 lines (`minimal/cml_segv.cml`).
Command `camel cml_segv.cml std::cml`. Observed: killed by signal 11. Expected: the printed source, or a `CmlUnsupportedConstruct` refusal.

```
func adder(n: int): (x: int) => int { return (x) => x + n }
func main(): int sync {
    let g: (x: int) => int = (x) => x * 3
    var h: (x: int) => int = (x) => x - 1
    h = (x) => x + 100
    println('let', g(14), h(1))
    let add5 = adder(5)
    return 0
}
```

### A2. The compiler crashes with SIGSEGV on a field access that does not type
Stage front end (`std::jgir canonical=true`, no pass). Found by E2: `std::cml` printed `test/cases/compile/static_expansion/fields_and_access.cml` into a program the compiler then crashes on; reduced to 4 lines (`minimal/recompile_segv.cml`).
Command `camel recompile_segv.cml "std::jgir canonical=true"`. Observed: signal 11. Expected: a diagnostic (`p.1` indexes a struct `{ x: int }` by a tuple position, and `nested` returns nothing).

```
func nested(t: { p: { x: int }, q: (int, int) }): string sync {
    let p: { x: int } = t.p
    let v: int = p.1
}
```

Two defects in one: the compiler must not crash on this input, and the printer wrote an ill-typed program for the `static for ... in fields` body (see A7).

### A3. The front end builds a graph that its own validator rejects (no pass at all)
Stage: graph construction; run on `std::nvm` with no rewrite pass. Found by E2 on generator programs (seeds 241, 374, 1718 of 2000, reported there as `no_compile`), reduced with `progen.py reduce --cell default`. Observed: `camel: internal error: Runtime graph ... breaks its edge invariants`, "This is a defect of the compiler". Expected: the program runs (the reference evaluator has it print nothing and exit 0). 10 of 10 runs of each written file reproduce.

Seed 374 (`minimal/progen_default_374/seed374.cml`): a `var string` passed to a parameter of type `string?`. Observed: `graph 'main' node 6 (10): passes a string as ordinary argument 0 of 'u46', which takes a string?`

```
func u46(a47: string?, a48: string): string {
    return 'm';
}

func main(): int sync {
    var v68 = '';
    u46(v68, '{},{}'->format<true, 2>);
    return 0;
}
```

Seed 1718 (`minimal/progen_default_1718/seed1718.cml`): the same with `bool` and `bool?`. Observed: `graph 'main' node 3 (10): passes a bool as ordinary argument 0 of 'u39', which takes a bool?`

```
func u39(a40: bool?, a41: bool): bool {
    return true;
}

func main(): int sync {
    var v72 = false;
    let n79 = u39(v72, 3 > 0);
    return 0;
}
```

Seed 241 (`minimal/progen_default_241/seed241.cml`): a lambda that calls a nested function writing a captured `var`. Observed: `Runtime graph '__g_51__' breaks the branch-arm invariant: ACCS 7 runs between BRCH 6 and its JOIN 5 and belongs to two arms, 0 and 1`.

```
func r14(a15: int, a16: string): string {
    return '';
}

func e21(a24: bool, var c23: string, var c22: int): float sync {
    func g26(a27: string): int sync {
        c22 *= 3;
        return 3;
    };
    let h36 = (p31: int): int => sync {
        let n32 = g26('');
        let n35 = r14(if false then c22 else c22, 'yz');
        return 0;
    };
    return 1.0;
}

func main(): int sync {
    return 0;
}
```

### A4. E9: crashes and internal errors of the optimisation pipeline, found by differential testing
138 programs ran in the matrix; 10 programs fail somewhere, none by a silent wrong value. Six representatives, reduced by the reducer of `progen` (`e9_reduced/`), localised by pipeline prefix (the first prefix whose run on `std::nvm` fails names the stage; where `std::nvm` is clean in every prefix the fault is in the back end). Each reproduces 5 of 5 (reducer) and 3 of 3 (verify). Programs print nothing and exit 0 without the pass; the reducer may shorten the pass list, the command column is the reduced one.

| id | file | command (after the file) | stage | back end | observed | expected |
|---|---|---|---|---|---|---|
| A4a | rc00_seed27 | `std::opt std::nvm` | std::opt (3 programs: seeds 27, 112, 116) | std::nvm | SIGSEGV | no output, exit 0 |
| A4b | rc01_seed47 | `std::opt::inline std::fvm` | pipeline prefix up to std::opt::fold (3 programs: 47, 111, 142) | std::fvm (also jit, prl) | SIGABRT: `Region 2 yields node 8 and its owner 5 is in the execution, but that node is not` (graph_runtime_support.cpp:216) | no output, exit 0 |
| A4c | rc02_seed6 | `std::opt std::fvm` | std::opt fixpoint (1 program) | std::fvm (also jit, prl) | SIGABRT: `Guarantee: brchTargetMap.empty() ... BRCH nodes have unmatched control outputs without corresponding JOIN nodes` (fastvm/compile.cpp:648) | no output, exit 0 |
| A4d | rc03_seed30 | `std::opt std::nvm` | std::opt (1 program) | std::nvm | `PassInternalError`: `breaks the branch-arm invariant: DATA 30 runs between BRCH 7 and its JOIN 6 and belongs to no arm` | no output, exit 0 |
| A4e | rc04_seed72 | `std::opt::inline std::nvm` | std::opt::inline (1 program) | std::nvm | `PassInternalError`: `lies on a cycle of inputs` | no output, exit 0 |
| A4f | rc05_seed104 | `std::opt::fold std::nvm` | std::opt::fold (1 program) | std::nvm | `PassInternalError`: `GATE 28 reads OPER 21 (:mark/filter_arr), which lies in an arm of BRCH 24 that may not be taken` | no output, exit 0 |

A4a (`e9_reduced/rc00_seed27.cml`)

```
func f1(a2: (a0: int, a1: string) => string, a3: float, a4: bool): string {
    return 'k9';
}

func r10(a11: int, a12: string, a13: int): string {
    return f1((p16: int, p17: string): string => '', 2.5, true);
}

func k21(a22: int): (a0: int) => int sync {
    return (p24: int): int => sync {
        return 0;
    };
}

func main(): int sync {
    k21(3);
    return 0;
}
```

A4b (`e9_reduced/rc01_seed47.cml`)

```
func u19(a20: string?, a21: string): string {
    return '';
}

with <k36: string>
func w35(a37: bool, a38: string): string {
    return u19(u19(if false then 'x' else null, 'a'), '');
}

func main(): int sync {
    return 0;
}
```

A4c (`e9_reduced/rc02_seed6.cml`)

```
func r6(a7: int, a8: int): int {
    return if a7 <= 0 then 1 else r6(0, (match a8 {
        case 5 => int(0.25)
        case _ => -a8
    }) % 97);
}

func main(): int sync {
    var v65 = (0, 0).0;
    var v67 = r6(8, v65);
    return 0;
}
```

A4d (`e9_reduced/rc03_seed30.cml`)

```
func f10(a11: (int, bool[]), a12: int): string {
    return '';
}

func q43(a44: int, a45: bool): bool sync {
    return false;
}

func main(): int sync {
    let n57 = '';
    let n58 = true;
    let n81 = if n58 then sync {
        let n76 = match n57 {
            case 'cd' => sync {
                return '';
            }
            case '' => sync {
                return '';
            }
            case 'ab' => sync {
                return f10((0, [false]), 0);
            }
            case _ => ''
        };
        let n77 = q43(0, false | false);
        return '';
    } else sync {
        return 'a';
    };
    return 0;
}
```

A4e (`e9_reduced/rc04_seed72.cml`)

```
func r1(a2: int, a3: int): int {
    return 0;
}

func k14(a15: int): (a0: int) => int sync {
    var v16 = r1(3, 0);
    return (p17: int): int => sync {
        v16 /= -2;
        return 5;
    };
}

func main(): int sync {
    let n62 = (if false then null else true) ?? (if false then false else null) ?? false;
    let n75 = k14(len([1.0]));
    return 0;
}
```

A4f (`e9_reduced/rc05_seed104.cml`)

```
import { get_env } from env

type P = { x: int, y: int }

func main(): int sync {
    let n31 = int((get_env('PROGEN_I0') ?? ''));
    let n54 = if false then sync {
        return [''];
    } else (loop s51 = ['ab', 'cd', 'pq', 'a'], c52 = 0 while false do (['']->filter<(p53: string): bool => false>, 0)).0;
    return 0;
}
```

### A5. `onnx::export` raises an internal error instead of refusing an impure program
Stage `onnx::export`; command `camel f.cml std::macro std::opt "onnx::export out=m.onnx"` (`minimal/export_internal.cml`). Observed: `PassInternalError: ... the ONNX export met a graph that belongs to no program` ("a defect of the compiler"). Expected: `TranslationRefused` at the `println` (the documented refusal for an impure operator). 11 programs of the corpus end this way (E6, outcome `other_error`, raw: `e6_export.jsonl`).

```
func sum(n: int, acc: int): int {
    return if n == 0 then acc else sum(n - 1, acc + n)
}

func main(): int sync {
    println(sum(100000, 0))
    return 0
}
```

Other internal errors in E6, not reduced: 8 programs with `Pass 'std::opt' failed with an internal error` (branch-arm invariant, LOOP state, effect order); find them in `e6_export.jsonl` (`outcome` = `other_error`, `detail` contains `internal error`).

### A6. E6: `std::macro std::opt [onnx::export]` kills the compiler on 26 corpus programs
Found by E6; re-run three times in place by `repro_checks.py` (`repro_checks.json`, `e6_crash_rerun`): 26 programs die with signal 6 or 11 (25 on all three runs, `consistency/cleanup/exit_zero` on two, the third run timed out; one more, `trans/cpp/exec/strings.cml`, crashed in the E6 run and exited 1 on the three re-runs, so it is not counted). Several already die in `std::macro std::opt std::gir` (enum and `match ... when` programs: signal 6; others: signal 11); the rest die in `onnx::export`. These are NOT reduced: a copy of the same file in a scratch directory does not crash (`ddmin.py`: "the original does not fail"), so the crash depends on the path of the file or the memory layout, which is itself a finding for the owners. Programs: `repro_checks.json`. Expected: a result or a diagnostic.

### A7. `std::cml` prints a program that behaves differently (E2)
Found by E2 on `test/cases`; the printed source compiles to a different graph and, in these 8 cases, to a different behaviour (`std::nvm`, stdout and exit status; `e2_test_classified.jsonl`). The test programs are the minimal programs (5 to 80 lines); the printed source of each is in `e2_failures/`.

| program (test/cases/...) | original | printed |
|---|---|---|
| consistency/fail_never/statement_after_fail | prints `before`, then the fault, exit 1 | prints `before`, `after`, exit 0 (the `fail<...>` statement is dropped) |
| effects/writer_table_through_callee | `0` `41` | `0` `0` |
| linear/runtime/var_read_before_region | `0 2` `0 3` `0 5 1` ... | `2 2` `3 3` `5 5 1` ... (a read of a `var` moved after its write) |
| linear/runtime/var_read_before_write | `int 2 1` ... | `int 2 2` ... (same cause) |
| linear/runtime/order_edges_regions | exit 56 | exit 76 |
| parse/rulings/power | `-9 9 -4 -4 12` | `-9 -9 -4 -4 12` |
| trans/cpp/exec/print_formats | `3-4|1+2` `[5][a]` `0.50` | `3 4|1 2` `5a` `0.5` (format specifications lost) |
| consistency/values/float_special | `     nan` `+nan` | `nan` `nan` (width and sign flags lost) |

Another 36 printed programs have a different graph and the same behaviour (e.g. `compile/rest_params/rest_inline`, where a static expansion is printed in its expanded form: a structure the language cannot state, or a loss no run observes; these are for review). 13 printed programs do not compile (`recompile_fail`: a redeclared name in comprehensions, `float[]` arguments, an array-literal shape, a reserved `__` identifier, an unresolved printer temporary `cml1`, and a warning that fails the recompile); 2 crash (A1, A2). All are listed with their reason in `e2_test.jsonl`.

### A8. The Camel stage `opt::cse` fails where `std::opt::cse` succeeds
Fixed on `lane-lit2` (`44e6d99e8`: `is_hit` asks `is_anchor`). Stage `opt::cse` (stdlib/opt.cml), E3. 36 of 145 compiling programs of the optimisation corpus (`test/cases/opt`) and 143 of 576 compiling programs of the 1-in-4 sample of `test/cases` end with `Invalid argument of 'graph.replace': the node anchors its graph (an exit, an output, a return or the end of a branch arm)`. Minimal (`minimal/camel_cse_anchor.cml`, reduced from `opt/generic/control_chain.cml`): `camel f.cml opt::cse std::gir` fails, `camel f.cml std::opt::cse std::gir` succeeds (control, 3 of 3).

```
func echo(x: int): int sync {
}
func main(): int sync {
    var x = 0
    echo(x)
    return 0
}
```

Expected: the pass merges what it can and leaves anchors alone, as the native pass does. `docs/technical/45_graph_module.md` lists the known gaps of `opt::cse`; this one is not among them as far as the summary table in that file goes.

### A9. The Camel stage `opt::fold` has no constant budget
Fixed on `lane-lit2` (`de37ecf0a`: `constant_bytes` is `constantBytesOf`, and `opt::fold` takes `constant_budget`). Stage `opt::fold`, E3. `camel f.cml opt::fold std::gir` on `minimal/fold_interval.cml` writes 43,890,478 bytes of graph dump; `std::opt::fold` writes 1,789 (`repro_checks.json`, `fold_interval_dump_bytes`). The native pass honours `constant_budget` (default 8 MiB) and leaves the interval as the computation that builds it; the Camel pass materialises the 5,000,000 elements. Two corpus programs (`feat/mm/gc/gc_block_pacing`, `para/core/block_sort_merge`) are affected; their dumps were too large to compare (`too_large` in `e3_*.jsonl`).

```
func main(): int sync {
    let k = 0..5000000
    println(len(k))
    return 0
}
```

### A10. The transpose has no arm for the array-index law, so a gradient of an array parameter stops there

Stage `transpose` (modules/transforms/transpose.cpp), reached through `vjp` (E2/E3). Five shapes, one stop.
`camel f.cml` with the case below prints the forward value `6.0` and then refuses:

```
[stdout] TRANSPOSE function=rulesets.jvp::index_into<float> skip=law rule=idx_arr_t uri=:op/idx_arr
[stdout] (vjp)…/stdlib/vjp.cml:72:47: [Error]: transforms: transpose has no transpose for …
```

The rule exists and is declared (`stdlib/rulesets/transpose.cml:73`, `idx_arr_t`:
`index_into(xs, i) => (ct: T) => scatter_into(zeros_like(xs), i, ct)`), and the pullback already emits the same
`:tangent/scatter_add_arr` (modules/autodiff/pullback.cpp:2146-2161). The transposer's catch-all `else`
(transpose.cpp:1354) knows only the arithmetic laws, so the array element read gets no cotangent at all.

```camel
import rulesets.jvp
import rulesets.transpose
import { vjp } from vjp

func g(xs: float[]): float {
    return xs[1] * xs[0]
}

func main(): int sync {
    let (value, pullback) = vjp(g)([3.0, 2.0])
    println(value)
    let cotangents = pullback(1.0)
    println(cotangents.0)
    return 0
}
```

Expected: `6.0` then `[2.0, 3.0]` (`dg/dxs[0] = xs[1] = 2`, `dg/dxs[1] = xs[0] = 3`). The same stop holds for a
single-element array, for one read out of two, for the sum and for the product, so the wall is the law and not
the shape. Whether to honour it is a design question -- the cotangent of an array element is an array, and the
transposer's cotangent port is a `float` today -- so it is filed as
`reviews/questions-2026-11-02-whether-the-transpose-honours-the-array-index-law.md` rather than changed here.
## B. Defects of the measuring instruments (test tools)

### B1. `test/tools/cml_roundtrip.py` reports a document as different from itself
In `Document._type` the edge from a type to its declaration is built to the vertex `T:<id>`, but declarations are the vertices `D:<id>`. Any canonical document that holds a named declaration fails: `DIFFERENT: ...: T:10 --decl--> unknown vertex T:d0`, even compared with itself. Reproduce: `camel test/cases/opt/generic/gate_branch_wait.cml "std::jgir canonical=true"`, drop the `GRAPH_STATS` line, run the checker on that file against itself: exit 1. In E3, 136 of the (program x variant) cells per stage (68 programs) are affected. `scripts/run_checker.py --fix-decl` applies the one-line fix in memory (the file in the tree is untouched); the numbers in `summary.md` use it and every row keeps the shipped verdict (`canonical_shipped_rc`).

### B2. The canonical document is not canonical for opaque types
`std::jgir canonical=true` writes for a type of kind `other` (for example `Node<int>`, `Rule`) a `code` that depends on the order in which modules were loaded. `camel f.cml std::opt::saturate "std::jgir canonical=true"` and the same with `opt::saturate` give documents that differ only in these codes (`2684354570` against `2684354568` for `Node<int>`, program `compile/when/rule_when`), though both execute the same Camel pass; the native pass run twice gives equal documents (`repro_checks.json`, `saturate_native_twice`). With the field ignored (`run_checker.py --mask-codes`) 64 (program x variant) cells, 32 programs, change from unequal to equal for saturate alone (`e3_all_stride4.jsonl`, verdict `equal_after_code_mask`).

## C. Observations that are not defects, recorded because the final campaign has to deal with them

- **The generator and the compiler disagree.** The compiler rejects 235 of 2,000 (11.8%) default progen programs (`e2_progen.jsonl`, `no_compile`) and 22 of the first 160 (13.8%, E9) with `NoMatchingFunction` (21) or `GenericFunctionAsValue` (1). One cause is the precedence of `a..b->reduce<...>`: `..` binds weaker than `->`, so `reduce` is applied to `-1` (`minimal/generator_reject.cml`). The progen sources are generator version 6; the pin may have moved past it.
- **`std::cpp` refuses most optimised programs.** 72 to 78 of 138 programs in each pipeline from `std::opt::devirtualize` on are refused with `TranslationRefused: the program has no call of 'main' std::cpp can find` (a located refusal, counted as legal). The cpp column therefore checks the pipelines `none` and `std::macro` almost alone.
- **Silent wrong values: none found.** 138 programs, 13,662 cells, 0 `MISMATCH` (`e9_summary.json`).
- **`std::cml` prints none of the compiling generator programs.** Default features: 0 of 1,765 (first refusals: an operator on a named struct type 1,482, a `dead` region 283). With `-opover,-struct,-tuple,-nullable,-partial,-macro,-comprehension`: 0 of 1,657 (`dead` region 1,040, operators 411, closures 149). The generator half of E2 is empty at this pin.
- **`opt::saturate` and `std::opt::saturate` time out (120 s) on 3 programs** (`both_fail` in `e3_all_stride4.jsonl`).
- **E6 "refused without a position" is 185, all of them files without a `main`** (library modules); it is 0 for the 3,088 files that declare `main` (`e6_summary.json`).

---

### A10. The Camel stage `opt::dce` erases unread statements that may fault
Stage `opt::dce` (stdlib/opt.cml), E3. Found in the per-function differences of `e3_*_functions.jsonl`: the Camel stage erases
`div_l`, `dtol`/`ltod`, `CAST`, `idx_arr` and `tensor:one_hot` nodes that the native stage keeps (e.g. `opt/fault_preservation/dce_chained_division`,
`chained_cast`, `modules/tensor/one_hot_oob`). Not produced by `write_defects.py`; added by hand after two independent manual re-runs on 2026-11-02.
Command `camel test/cases/opt/fault_preservation/dce_chained_division.cml opt::dce std::nvm`. Observed: prints `before`, `after`, exit 0.
Expected (no pass, or `std::opt::dce`): prints `before`, then `DivisionByZero`, exit 1. Likely cause: `value_only` uses `is_pure`, which
`stdlib/graph.cml` documents as "declared pure", not "cannot fault". Unchanged on fsh-dev at b1bd72af6.

```
func check(z: int): int sync {
    let r = 5 / z
    println('after')
    return 0
}

func main(): int sync {
    println('before')
    return check(0)
}
```

## B. Defects of the measuring instruments (test tools)

### B1. `test/tools/cml_roundtrip.py` reports a document as different from itself
