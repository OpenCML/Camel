# 少一次计数：哪一处是真的，哪一处只是写在那儿

- 日期：2026-11-16
- 车道：`lane-refhook`（工作树 `D:\Projects\Camel-wt\refhook`，基线 `2bc0ed775`）
- 前情：作者审查 `54b501f3a` 第二件第 4 条（「并行调度器有一条路径不执行计数」）；
  `internal/reports/2026-11-15-prl-counted-boxes-return.cn.md`
- 依据：原则 10.5；`include/camel/runtime/exec_plan.h`「The references that come back」
- 状态：三笔——计划侧漏交回一份（`2746e4025`）、十个调用算子的位置收成一个钩子、判据从 `live`
  换成计划侧的表。**这一页记的三条比那几十行代码值钱。**

## 1.「有一处不执行计数」不是「有一个形状会因此少计数」

作者原句：并行调度器有一条路径不执行计数，准备好的节点走 `runMarkedOperator` 直接返回，不调
`applyRefEntriesForNode`。前半是**代码事实**，成立；后半——有程序因此少一次——**不成立**：

- 那一处的三个调用者都到不了 Oper：`executePreparedBranchArm` 的逐节点循环、
  `buildRuntimeBranchJoinRegion` 的 JOIN 任务、`executeLinearNode` 的 `default`（只落 `Loop`，
  而 `TaskflowExecSchedPass::executePreparedNode` 没有 `Loop` 这个 case，走不到 Oper）；
- 全树唯一根是 `TfDumpPass::apply → buildAndDump → instantiate_graph_instance_generic`，它只
  `mainFlow_.dump(ss)`、不跑任务；
- 实测：带 marked 算子的分支臂在 `std::prl` 上只出 `marked-linear`，`marked-prepared` 一次都不出。

⇒ **只接那一处，读数一格不动。** 仍然接它，是为了让「`applyRefEntriesForNode` 的唯一调用者是一个
钩子」成立——不是为了修某个形状。可测的那一步在别处（第 3 节）。

## 2.`live` 收尾容得下一次真漏计数

同一个程序（`foreach` 捕获一个 `state::Ref`），`--gc-check foreign`，每格 3/3 次相同：

| 后端 | 跑法 | released | live | disposed |
|---|---|---|---|---|
| nvm / prl | 无压测 | 1 | 0 | 0 |
| fvm / jit | 无压测 | 0 | 1 | 1 |
| fvm | `--gc-stress` | 1 | 0 | 0（finalized 1） |

`live` 量的是「跑完之后还剩多少」，而收集器在跑完之后把账抹平：**一条断言在压测下与不压测下给出
两个答案，它就不是判据，是运气。** `ref.boxes_made_per_call_ten` 上的 `live <= 3` 正是这样容下了
一次真漏计数。

判据换成两条，都不靠 `live`：① `test::plan_refs` 的表——每一处 marked 算子有没有对应的 `give` 行
（没有就照实报数）；② 同一个程序四个后端在**同一位置**报同一件事，且不依赖 `--gc-stress`。

## 3. fvm/jit 的「对」是少还了一次账

`reduce` 返回它的累加器，所以 `initial` 传 `state::Ref<int>` 时，节点的值**就是** `initial` 的盒子：

- nvm / prl 在节点位置把 `initial` 那一份交回 → 盒子提前归零 → `state.get: the value is not a
  reference`；
- fvm / jit 打印出对的值，只因为 SCHD 一处行都不执行——它**少还了一次账**。

两个缺陷互相抵消，错的是计数的那一侧，而它的错被另一个掩盖了；谁只看 fvm/jit，就会得出「nvm/prl
有 bug」。修法：最后一次使用接走产出那一份，节点把自己的值交出去时也接走——**按对象判定**
（计划看不见元素函数返回什么，所以判定放在行跑的时候，
`src/execute/exec_plan.cpp#applyRefEntriesAtPosition`）。修后四个后端同一组读数：同一个值，
`released=1 live=0`。

## 4. 现在的形状

- `include/camel/execute/operator_call.h`：`OperatorCallSite{plan, frame, node}` + `callOperator`；
  十个调用算子的位置都经它（nvm 2、fvm 的 OPER 一处与两个解释器体的 SCHD、jit 的解释器出口、
  prl 3），`executePreparedNode` 的 Oper 分支交给 `executeLinearMarkedOperator` 而不是自己再跑一遍。
- `casevm.cpp` 一个字节都没有被跑过，也没有被编过：`ENABLE_FASTVM_COMPUTED_GOTO` 默认 `ON`
  （`cmake/Options.cmake:63`），默认构建只编 `gotovm.cpp`；把选项关掉它**根本编不出来**——它自己的
  `#include "camel/utils/opperf.h"` 找不到（文件在 `src/passes/sched/fastvm/opperf.h`），往后
  `Frame`、`TypeCode`、`Object` 也不认了，`-fsyntax-only` 在第二十个错误处停下。这一半是写在孪生
  位置旁边、靠读对齐的：**没被跑过就写没被跑过，没被编过就写没被编过。**