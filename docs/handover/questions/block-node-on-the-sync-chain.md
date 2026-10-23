# 同步链上的 `BLOCK` 节点：图模型里还没有这个节点

## 问题

负责人已在 `docs/handover/reviews/questions-2026-10-23.md#inline-order-after-body`（含同日更正）里定了内联之后
函数体区域的挂法：**经它的所属节点**——同步块里一条调用语句被内联后，函数体的区域由一个 `BLOCK` 节点
（一个数据端口读区域的结果）代替原来的调用挂在链上（前一条 → `BLOCK` → 后一条），与 `BRCH`/`JOIN`、`LOOP`
挂链的方式统一；`docs/design/effects/edges.cn.md` 第 5 节第 7 条同时写明"边的端点只有节点"、"挂在链上
（有 `BLOCK` 所属节点）的区域不溶解"，并指明 `7ca495665` 里那条顺序边要删掉。

施工前必须先回答第一件事：**这个节点今天不存在。**

## 实测：没有 `BLOCK` 节点

- `include/camel/runtime/graph.h#GCNodeKind` 有 14 个程序可见的种类，`Loop` 是最后一个；没有 `Block`。
  `Bind`/`Dref`/`Dead` 不是程序能看到的节点，`modules/graph/nodekind.h#nodeKindTag` 正是按这条线划分的。
- `include/camel/runtime/graph.h#GCRegionKind` 里有 `Block = 5`，但它是**注解区域**（注释写着"带标签或装饰器的块"）：
  `include/camel/runtime/control_regions.h#ControlRegions::isControl` 只认 `Arm`/`LoopCond`/`LoopBody`；
  注解区域被后续遍看穿（`src/runtime/draft_regions.cpp#GraphDraft::inheritRegion` 会把读它的节点搬进控制区域），
  并且可以被溶解。
- `include/camel/runtime/graph.h#GCRegion` 已经有 `owner` 字段（注释："控制区域：`BRCH` 或 `LOOP` 节点"），
  `src/runtime/draft_regions.cpp#GraphDraft::setRegionOwner` 也已存在；但内联区域从建出来到被挂上，走的都是
  `src/runtime/draft_regions.cpp#GraphDraft::addInlineRegion`，它只记 `callSite`（被替换的那条调用的节点），
  `owner` 一直是空的。也就是说：**区域从来没有挂在一个"自己的节点"上**——臂与循环体挂的是别的语句节点
  （`BRCH`/`LOOP`），内联区域挂的是一个已经被删掉的调用节点。
- 今天的拼接（`src/runtime/draft_inline.cpp#inlineCallableInDraft`）把调用节点 `eraseNode` 掉，用
  `src/runtime/draft.cpp#GraphDraft::closeChainOver` 让后一条语句接上调用原先的前驱，并留一条顺序边
  （`appendOrderDependency`）表达"函数体在后续语句之前"。这条顺序边今天在树上**完全不可达**
  （builder 不写同步边，`syncUsersOf` 恒空），所以删控制边之前必须有答案。

## 裁定没有定的两件事

1. **区域必须变成"执行/控制"的区域。** 判词要求"挂在链上的区域不溶解——它承担排序"，而今天"是否被执行"
   与"是否可溶解"是同一个判定：`include/camel/runtime/control_regions.h#ControlRegions::isControl`
   只认 `Arm`/`LoopCond`/`LoopBody`。一个承担排序、又要求"区域里要执行的节点都在其所属节点的同步前驱之后"
   的区域必须走进这一组，否则任何一遍都能把它溶解掉，排序随之消失。这是新裁定还是现有规则的推论，需要确认。
2. **节点自己的执行与编码没有定义。** "一个数据端口读区域结果"意味着它的值就是区域结果，但它在三个执行器里
   怎么跑（`src/passes/sched/nodevm/nodevm.cpp`、`src/passes/sched/fastvm/compile.cpp`、
   `src/passes/sched/taskflow/taskflow.cpp`）、怎么编码（`src/compile/gir/graph_encode.cpp`）、
   怎么导出（`src/passes/trans/cpp/program_emitter.cpp`、`src/passes/trans/json/gir.cpp`、
   `src/passes/trans/dot/graphviz.cpp`、`src/passes/trans/cml/cml.cpp`）都要写。读区域结果这件事本身有先例：
   `JOIN` 读臂的结果、`LOOP` 读两个部分的结果，`src/runtime/draft_regions.cpp#GraphDraft::inheritRegion`
   把它们作为明确的例外对待。

## 选项与代价

- **(a) 新增 `GCNodeKind::Block`。** 判词字面上的形状，语义最干净，与 `BRCH`/`JOIN`/`LOOP` 完全一致。
  代价：加一个节点种类在设计上是"打断编译"的改动——`modules/graph/nodekind.h#nodeKindTag` 的注释写明
  "每个 `GCNodeKind` 都在这个 switch 里，新加的种类会让编译在这里停"。实测：提到 `GCNodeKind` 的文件 99 个、
  显式写 `Kind::Loop` 的 54 个，另有 7 处独立的 `kindName()` 表（`tools/debugger/src/core/program_index.cpp`、
  `src/compile/gir/debug_json.cpp`、`src/runtime/branch_arms.cpp`、`src/passes/trans/gir_metadata.cpp`、
  `src/passes/trans/cpp/program_emitter.cpp`、`src/passes/test/loop_inputs.cpp`、
  `src/passes/sched/fastvm/bytecode.cpp`），加上三个执行器、全部导出器，以及 `graph` 模块自己的
  `NodeKind` 枚举与表（`modules/graph/nodekind.cpp`）；还有程序可见种类会出现的 goldens
  （`std::gir`/`std::jgir`/`std::stats` 快照）。
- **(b) 把内联的那条调用节点留下，让它当所属节点。** 调用节点已经在链的位置上，区域也已经有 `callSite` 指回它：
  留下它、`setRegionOwner` 指过去、把区域结果接成它的一个数据端口，再用已经实现并测过的
  `src/runtime/draft.cpp#GraphDraft::takeStatementPlace` 把链位交接过去。代价：必须说清它**保留哪个种类**——
  保留 `Call` 不诚实（它不再调用任何东西，但三个执行器与导出器都会按 `Call` 去取被调图），改成 `Block`
  就回到 (a) 的成本。收益：不必在 `draft_inline` 里新建节点，链位与区域所属关系都是现成的。
- **(c) 不新增种类，用现成的 `JOIN` 当所属节点。** `JOIN` 已经是"读区域结果、挂在链上、不被溶解"的节点。
  代价：`JOIN` 的语义是"从多个已执行的臂里选一个结果"，而内联区域没有臂、也不需要选择，语义面被撑大；
  且 `JOIN` 的第 0 个端口必须来自同一区域的 `BRCH`（`docs/design/effects/edges.cn.md` 4.6），要先改 4.6。
  收益：0 个新种类、0 个新执行器分支。成本最低，但语义最脏，**不予推荐**。

## 施工方建议

**(b) 的骨架 + (a) 的种类**：把内联的那条调用节点留下、`setRegionOwner` 到区域、区域结果做它的数据端口、
用 `takeStatementPlace` 交接链位；它的种类按 (a) 新增 `GCNodeKind::Block`（`JOIN` 的存在说明"读区域结果、
挂在链上、不被溶解"的节点在模型里本来就有位置，缺的只是种类名）。请先确认上面第 1 件（区域进 `isControl` 组），
它决定这条链是否真的承担排序；第 2 件（执行与编码）可按 `JOIN` 的既有约定照做。

## 来源

- 裁定：`docs/handover/reviews/questions-2026-10-23.md#inline-order-after-body`（含同日更正）；
  `docs/design/effects/edges.cn.md` 第 5 节第 7 条。
- 模型：`include/camel/runtime/graph.h#GCNodeKind`、`#GCRegionKind`、`#GCRegion`（`owner`/`callSite`/`result`）。
- 区域：`src/runtime/draft_regions.cpp#GraphDraft::addInlineRegion`、`#GraphDraft::setRegionOwner`、
  `#GraphDraft::inheritRegion`；`include/camel/runtime/control_regions.h#ControlRegions::isControl`。
- 拼接：`src/runtime/draft_inline.cpp#inlineCallableInDraft`、`src/runtime/draft.cpp#GraphDraft::closeChainOver`、
  `#GraphDraft::takeStatementPlace`。
- 测量（两套边的探针 + 校验器，fast 层）：判词假定的节点核对结果为"不存在"；那条顺序边占
  `order-edge-on-pure` 538 行，其中 `std::opt` 303、`std::opt::inline` 151、`std::opt check_screens=true` 70，
  挡住 269 个测试（133 个只因它失败）。探针总数：拒绝 1106 图（`std::opt` 824、`std::opt::inline` 205、
  `check_screens` 119、`std::inline` 103、`std::macro` 64）。