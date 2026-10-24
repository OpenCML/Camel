# h444-stepped-range — 带步长 `range` 的剩余工作（融合 + 张量 `range` 删除）

本文件记录 `handover/h444-stepped-range` 这一批**没有做完**、需要下一批接手的两件事，以及为什么。
依据：`docs/handover/reviews/questions-2026-10-24-evening.md` 的 `## h426-interval-step-form（带步长的整数序列）`。

## 已做完并已提交（不要重做）

1. **`range(a, b, step): int[]`（Python 语义）**：`src/builtin/modules/globals.cpp` 的 `:struct/range`
   规则改为一条 `{-1, {}}`（参数个数不受约束）的规则，`norm.size()` 为 2 或 3、每个都是 `int` 时成立；
   `__range__`（`src/builtin/operators/struct.cpp`）本来就按 Python 语义算元素和个数。
   `stdlib/signatures/globals.cml` 里的 `#op<':struct/range'>` 改成
   `export func range(start: int, end: int, step?: int): int[]`。
2. **常量 0 步长 = 编译期错误**：新诊断码 `SemanticDiag::RangeStepZero`（`0x0001E6`，`error`），
   在调用处、光标指在 step 实参上；非 0 的常量步长不做折叠。
3. **非恒定 0 步长 = 运行期 fault**：`__range__` 的 `RuntimeDiag::InvalidArgument`（本来就有的那条）。
4. **名字查找顺序**（裁定第 3 条，"builtin wins 是缺陷"）：`src/compile/gir/builder_expr.cpp` 的
   `visitDRefNode` 现在按 局部 > 本模块 > 按名字导入的 > `using namespace` > 内建 取**第一个有这个名字的层**；
   `using` 层现在排在"合并层"之前（此前一个名字只要内建里有，`using` 层永远够不到）。
   `src/core/module/module.cpp` 的 `importedBindingsForRef` 不再让前奏（无名的内建模块）通过一个
   空前缀的 namespace 绑定污染 `using` 层。
5. **`std::cml` 打印 3 输入的 `:struct/range`**：`range(a, b, step)`（旧代码在那里直接 `fail`）。

## 未做完之一：`for i in range(a, b, step)` 的融合（裁定第 1 条后半）

裁定要求：`for i in range(a, b, step)` **像 `a..b` 一样融合，绝不物化**。本批**没有实现**，原因是
实现到一半出现了**一个未定位的挂死**，为了不在深夜把一个坏改动留在分支上，已整体回退。回退掉的
改动（都在 `D:\Projects\Camel-wt\notes\` 的草稿里有全文，也可从本批未采用的设计重新写）是：

- `LoopIterableForm::Interval` 的序列从 2 元组 `(from, to)` 扩成 3 元组 `(from, to, step)`；
- `include/camel/runtime/graph.h`：`intervalBoundsOf` 返回 `IntervalBounds{from, to, optional step}`；
- `include/camel/runtime/exec_plan.h`：`ExecLoop` 加 `intervalStepSlot` + `intervalHasStep`；
- `include/camel/execute/loop_frame.h`：`loopTest` 里按 `from + round * step` 取元素，
  个数按有符号 step 分正负算，step 为 0 时抛 `RuntimeDiag::InvalidArgument`（与数组形式一致）；
- `src/compile/gir/builder_control.cpp`：`isSteppedRangeExpr`（只认内建 `range`，按名字导入的
  `range` 不认）+ `intervalSequenceValue(from, to, step, expr)` 造 3 元组；
- `src/execute/exec_plan.cpp`、`src/runtime/loop.cpp`（校验 2 或 3 个 `int` 分量）。

**可复现的读数（整层，逐字）**，在把上面这一整套一起应用并构建 `BUILD_EXIT=0` 之后：

```
$ pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h444-stepped-range -Plan test/plans/feat/linear/loops.plan.toml -Jobs 8
[FAIL] loops.zero_leaf_state.opt_jit                          20.02 s
[SUMMARY] total: 130, pass: 129, fail: 1
```

`test/cases/loops/zero_leaf_state.cml`（它的 `carried(n)` 是 `for x in 0..n`，2 元组、**不经过**
新加的 3 元组路径）在 `std::nvm`、`std::jit`、`std::opt` 单独跑都正常，只有 `std::opt std::jit`
挂住（`timeout 120` → exit 124）；同一个文件在基线检出 `h446-base`（`bc6cc2732`）上
`std::opt std::jit` 是正常输出 `round 7 / round 8 / {} {} / true`。

已经排除的：优化后的图在两边**逐字节相同**（`std::opt std::jgir` 输出 1727 行，`diff` 无差异），
所以不是优化 pass 产出的图不同；把 `loop_frame.h` 单独换回基线仍然挂（说明不只是 `loopTest` 里的
新算式）。**没有排除**、也是最可疑的一点：`intervalSequenceValue` 里静态模板那一行
`::Tuple::create(2, mm::autoSpace())`（基线）被改成按分量个数构造
`::Tuple::create(components.size(), mm::autoSpace())`，而 2 元组时两者语义应当相同——但
`slot_t empty`/`materializeStaticValue` 这条模板路径需要单独验证。

**建议的下一步**（按顺序，别一次全上）：
1. 先只把 `intervalSequenceValue` 的静态模板那一行改成按个数构造（其余不动），跑
   `test/plans/feat/linear/loops.plan.toml`；这一条就能证明或排除它。
2. 再单独加 3 元组 + `exec_plan` + `loop_frame` 的 step 读取（用一个**只有 `range(a,b,step)` 的
   `for`** 的用例当第一个读数），最后才动 `loopTest` 的通用算式。
3. 融合必须留的回归读数：`a..b` 与 `for i in 0..n` 的 `std::jgir` 读数与基线**逐字节相同**
   （本批已验证优化后 1727 行 `diff` 无差异，可直接复用这个读数）。

## 未做完之二：张量模块 `range` 的删除（裁定第 2 条）

裁定第 2 条说 P4 里把张量模块的 `range` 删掉。**本批没有删**，因为 P4 的终点还没到：

- h438（`handover/h438-tensor-alias`，commit `a55373fa5`）**已经合进** `origin/develop`
  （`merge-base --is-ancestor` 退出 0 可验），但它只是张量类型别名这一步；
- P4 的终点（张量 → `array`、删掉过渡期裸 `Tensor` 类型）**还没做**，
  `docs/handover/questions/tensor-alias-transition.md` 列着 P4 的删除清单；
- 现在 `stdlib/signatures/tensor.cml` 同时带两条臂是**故意的**。

**下一步**：等 P4 那一步合并后，再删 `modules/tensor/ops/creation_ops.cpp` 里 `range` 的 `OpDef`
（`exports = {"range"}`）、`stdlib/signatures/tensor.cml` 的声明与文档，并把
`test/cases/modules/nn/adam_update_bits.cml` 里的 `import { Tensor, cast, range } from tensor` 与
`range(6)`/`range(2)` 改成 `0..6`/`0..2` 或核心的 `range(a, b, step)`。
删除之后本批新增的 `stream.range_import_wins` / `stream.range_using_wins` 两个用例要跟着改
（它们用张量 `range` 当"按名字导入的那一个"），这是**故意的**：它们是名字查找顺序的证据，
换一个模块的 `range`（或换一个同名函数）同样能证明同一件事。

## 本批留下的其它未决项

- `for i in range(a, b, step)` 目前**物化**成 `int[]`（`range_step.cml` 的读数是数组），
  与裁定的"绝不物化"不一致，属于上面第一款。
- 张量那个 `range` 与内建 `range` 同名、且都挂在同一个 `OperatorGroup` 上：本批修好的是**层序**
  （层 3/4 的实体整体胜出），不是"同名不同 URI 的组该不该合并"这条更一般的问题。
  裁定只要求层序，本批没有改合并规则。