# §4.3.1: which layer decides in place now, and does `:var/take` keep its own judgment?

Status: open, for the language author. Raised by the lane that owns the in-place/memory-planning
item (handover/h474-in-place-planning, based on `0b1be856d`, the var-version ruling replayed on
`origin/develop`).

## What the ruling asks

`docs/handover/reviews/questions-2026-10-24-evening.md`, §`var-version-node` 补二/补三:
the in-place criterion is `docs/design/arrays-memory/README.cn.md` §4.3.1 (line 142), and

> 决定只在内存规划阶段做、写进执行计划。函数结果、捕获端口、导出端口都是"别的观察者"。
> 由此 `string_plus_assign`（`abab`）这一族：`:var/take` 把仍有观察者的数组交出去原地追加，
> 违反 4.3.1，是缺陷——SSA 之后由内存规划按 4.3.1 判定，**不在 `:var/take` 里各自判断**。

§4.3.1 (line 154) is explicit about the shape of the change: *"原地与否由优化之后的内存规划阶段
决定，写进执行计划（某个更新节点"复用输入的存储"），所有后端照计划执行，不各自判断"*.

## What the tree has (measured on `0b1be856d`)

| §4.3.1 | landing point today |
|---|---|
| the execution plan | `ExecPlan` (`include/camel/runtime/exec_plan.h:111`, built once per graph version by `buildExecPlan`, `:247`) — order, slots, calls, arms, loops, tail calls. **No field says "this update reuses its input's storage".** |
| 第 1 层, the compile-time part that exists | the lowering that picks the in-place form: `Builder::inPlaceRebindingOf` (`src/compile/gir/builder_operators.cpp:1000`) with `OperatorTable::setInPlaceForm` (`include/camel/core/operator_table.h:352`); the "no other observer" proof on the graph: `Builder::markNonRetainingReads` (`src/compile/gir/builder_diagnostics.cpp:552`), which turns the read that everything orders before the next write into `:var/peek` |
| 第 1 层, the order edges (§4.3.1 "内存规划把它们排在更新之前") | ORDER/control edges and their reduction: `reduceOrderDependencies` (`include/camel/runtime/control.h:39`); "this value is written in place" as a property of a node: `isWrittenInPlace` / `WrittenSlots` (`include/camel/runtime/draft_writes.h:55,62`) |
| 第 2 层 (count == 1) | `Array::canWriteInPlace` (`src/core/rtdata/array.cpp:989`), `Array::reusableBlock` (`:958`), asked by the in-place bodies (`src/builtin/operators/struct.cpp:376`, `src/builtin/operators/array.cpp:282`, `src/builtin/operators/set_value.cpp:170`) |
| 第 3 层 / the copy fallback | the clone in `:var/take` (`src/builtin/operators/ops.cpp:248`) |

But the decision itself is **not** made by a memory-planning stage: it is made per update site by
the lowering, and then confirmed at run time by a mark on the `var` cell —
`Tuple::holdsUnsharedArray` (`include/camel/core/rtdata/tuple.h:135`), which `:var/get` clears
(`ops.cpp:208`), `:var/peek` leaves alone (`:217`), `:op/assn` clears (`:226`), `:op/assn_owned`
sets (`:264`), and `:var/take` reads to hand over the array or a clone (`:253`). There is no pass
that computes the decision, and nothing in `ExecPlan` carries it.

## The questions

1. Is the runtime mark to go away — `:var/take` always handing over what it is given, with the
   in-place form emitted only where the compile-time proof holds and the decision carried in
   `ExecPlan`? If so, which layer's proof replaces the mark for an **escaping `var`** (a `var`
   parameter's cell is a port: the builder sees the callee, not the callers, so it cannot prove
   "last use")? §4.3.1 line 150 speaks of "跨函数按参数推断'消费'或'借用'", which does not exist.
2. `test/plans/feat/linear/var_reference.plan.toml` test `element_assigned.in_place_form`
   (line 1301) pins the lowering *shape* (`:op/assn_owned(arr, :op/set_index_in_place(:var/take(
   arr), 0, 9))`) and its comment (line 1297) names the mechanism: *"Whether the array is copied is
   decided by the cell's 'not shared' mark when :var/take runs"*. Is that shape to stay with only
   the decision moving, or is the shape to change too (so that test needs re-deriving)? The
   counted tests `path_assigned.allocs` (line 1334) and `element_assigned_copy` (line 1358) pin
   only the observable rule (30 unshared writes allocate nothing; a write after `let kept = v`
   allocates one and `kept` is unchanged), which any layer arrangement must keep.
3. Which stage is "优化之后的内存规划" — `std::opt::order` (`OrderReductionPass`,
   `src/passes/opt/generic/generic.cpp:899`), a new pass, or `buildExecPlan` itself? The answer
   decides where the "reuse" flag is computed and what `std::profile` (line 160) reports from.

## What this lane did instead

The five red `modules/onnx/export_var_*` tests were the ones hanging on this: the exporter read a
`var` read's data port as the cell, while after the ruling it is the version the last write
produced, so every read of an ordinary local variable was refused as "outside the function that
declares it" and the growing-array refusal was never reached. The lane teaches the exporter to
follow the version chain back to the declaring `:var/new` (`declaringCellOf`,
`modules/onnx/exporter.cpp:468`) and to take the *value* type of a write (`valueTypeOf`) for the
compound path; all 139 tests of `test/plans/feat/modules/onnx.plan.toml` pass. It did **not**
touch the in-place judgment, per "拿不准的写问题文件，别自定语义".

The same lane's measurements otherwise, on the gate command
`-Plan smoke,test/plans/feat -Tier fast -Jobs 8`: the four families of the item
(`string_plus_assign` ×4, `path_assigned.allocs` ×4, `path_assigned`, `element_assigned_copy`,
`nested_array_type` ×2) are **green** on the base, and are not reddened by this change.