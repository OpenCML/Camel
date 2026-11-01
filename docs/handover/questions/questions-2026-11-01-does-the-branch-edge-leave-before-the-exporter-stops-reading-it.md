# 分支那条边，是不是该等导出器不再读它之后才走

> **问什么**：S1 的收尾是删掉 `src/compile/gir/builder_control.cpp:2864`（`linkArmEntries` 里的
> `linkNodes(LinkType::Ctrl, brchNode, node)`，从 BRCH 连到臂入口的那条 ctrl 边）。接链修法已经落了
> （`e6bfe5d09`），`positions.cml` 在删边后回到 10 行、次序不变——但**删边之后导出器坏了**，而且坏法
> 不是形状。
> **堵着谁**：S1 的收尾（这条边删不删）。
> **为什么我不自己定**：这是"这条边是不是接口的一部分"的问题，不是改法的问题。

## 一、现象（全部实测，今天的树 + 只删那一行）

删掉 `2864` 之后，不涉形状、只涉行为的两条读数：

- **模型里没有 Loop 了。** `ort_loop_loaded_weights` 打出
  `FAIL: the model holds no Loop: ['Add', 'Cast', 'Concat', 'Expand', 'Identity', 'If', 'Less', 'MatMul',
  'Mul', 'ReduceSum', 'Reshape', 'Shape', 'Sub', 'Tanh', 'Transpose', 'Unsqueeze']`——
  该是一圈 `Loop` 的地方，导出成了 `If`（这次运行约 1.3 s，没超时）。
- **有的程序导出跑不完。** `camel test/cases/modules/onnx/export_loop.cml std::macro`：`timeout 30` →
  **exit 124**，卡在程序自己的导出调用里；把边留着，同一条命令打 `exported`、exit 0。`std::opt` 单跑、
  `std::macro std::opt` 组合同样 124。
- **这不是接链修法引起的**：把 `e6bfe5d09` 的接链修法回退、只留删边，`export_loop.cml std::macro` 仍然
  124。所以是**这条边本身**在支撑着什么。

**那条边今天被谁读**（符号名，按"读的人"排）：

| 位置 | 读什么 |
|---|---|
| `modules/onnx/exporter.cpp:2728`、`:3768` | 一个分支的臂是 `withInputsOf(brch)`（**数据** kind 的边），不是被删的这条 ctrl 边 |
| `modules/onnx/exporter.cpp:2446` `evalLoopNode`、`:2027` `translateLoop` | 尾递归认不认成 `Loop`，看的是被调用者那张图 |
| `src/passes/sched/fastvm/compile.cpp:409` | `plan.branchArmEntriesOf(i)[armIndex]` → `brchTargetMap`（字节码 VM 从**计划**取臂的入口） |
| `src/execute/graph_runtime_support.cpp:826` | `BranchArmEntries`：臂在计划里没有成员时回落到 `body->join` |
| `src/runtime/draft_inline.cpp:2325` | 内联时"调用等哪条语句"取自 `ctrlPreds`（`controlFrontier` 走 ctrl/sync 输入） |

**要点**：导出器**不读** `branchArmEntriesOf`，也不读被删的那条 ctrl 边。删边是通过**改掉 `std::opt`
交给导出器的那张图**（内联的 place 变了）让导出失败的。所以"让导出器从区域取臂入口"这句在今天
**没有可动的读数**——它已经是从数据边/区域取的。

## 二、两条路的代价

- **甲：先让三处读者都不依赖这条边，再删。** ①内联的 place 从区域取（`ctrlPreds` 这条）；
  ②导出器认递归/臂从区域取；③字节码 VM 只从计划取（`brchTargetMap` 那条已经只从计划取，缺的是
  臂空的时候回落到 JOIN 的合法性）。代价：**三笔，每笔都要一条自己的读数**；今天能拿到的第一条读数
  是"边删掉、内联 place 从区域取、`export_loop.cml std::macro` 仍 `exported`"。
- **乙：承认这条边是接口的一部分**，把裁定里"今天就删"改成"等甲落地后删"。代价：S1 的收尾停在读数上
  （`REGION … in-plan == members` + 552 已进计划），边留到甲完成。

## 三、我方默认建议

**甲**，而且**第一笔改内联的 place，不改导出器**：今天唯一还会被这条边改掉的东西是"调用等哪条语句"
（`draft_inline.cpp:2325`），导出器和字节码都只是下游。这等于让"臂的入口/内的位置"由**区域**说，
而不是由一条 ctrl 边兼职——这正是"架构上避免"，而不是把预期改掉。

## 请作者答一句

**甲（先改内联的 place，再删边）还是乙（认定这条边是接口的一部分，收尾停在这里）？**
若选甲，请确认第一笔就按"内联的 place 从区域取、边先不删、判据是 `export_loop.cml std::macro` 仍
`exported` 且 `ort_loop_loaded_weights` 仍打出 Loop"来做。