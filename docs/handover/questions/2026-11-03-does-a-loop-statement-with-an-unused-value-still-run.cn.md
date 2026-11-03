# 一个值没人用的 `loop` 语句，还算不算一条要执行的语句？

日期：2026-11-03　状态：**已裁定 (a)，落点待量**

## 问题

`loop acc = 0 for v in count(3) do sync { println(v); acc + 1 }` 写在 `main` 里，循环的
值（`acc`）没有任何用户。语句本身是写着的，体里有 `println`。它该不该跑？

设计原则 3.4 说"同步块里的语句都执行，按书写顺序"，`loop` 语句是语句，所以答案是**该跑**。
当前实现**不跑**：体的值门在图上没有任何使用者，整块连同体的效应一起被判死。

## 现象（三个用例，同形）

`test/cases/compile/stream/generators_loop.cml:27,29,30`，三处 `for v in count(n) do sync {…}`。
诊断对三处各报一条 `EffectNotExecuted`（"the sync block it is in is not used"），输出期望的
`0\n1\n2\n-\n0\n1\nend` 一个字符也没有；`std::gir` 里 `main` 连一个 `:io/println` 都不剩。

三处走法完全一样（`CAMEL_P02_WALK=1` 的打印原文，`generators_loop.cml`）：

```
P02WALK graph=main start=26 startKind=11(Oper) stopGate=31 from=30
        syncUsers=0 ctrlUsers=0 syncInputs=1 ctrlInputs=1 normInputs=1 visited=3
        (另两份：26/31、69/74、111/116)
```

从 `println`（26）出发，沿控制与同步使用者向上：

1. 到 **GATE 30**——链位。`chainPlaceGates_` 认得它（`chainPlaceForNode` 登记过），所以
   **穿过去**，不在这里停。这一步说明"集合作用域"没有问题：诊断跑在同一个 `Builder` 实例上。
2. 到 **GATE 31**——停。它 **`syncUsers=0`、`ctrlUsers=0`**（一个顺链使用者都没有），却有
   1 个数据输入、1 个同步前驱、1 个控制前驱。

## 为什么停点是"块值形状"而不是漏登记的链位

按 `docs/design/effects/edges.cn.md` 3.1 的两句：**链位 `GATE` 必有下文，块值 `GATE` 是链的
最后一站**。31 号没有下文，所以它是块值形状——它把块的值交给链尾，自己就是终点，不是漏登记的
链位。全语料读音也一致：2858 个链位全部有后继，3546 个块值全部没有。

所以 `inUnusedSyncBlock` 停在这里**是对的**：图上确实没有任何东西用这个块的值。诊断没有错，
错的是上游——循环语句的值被丢掉了，体跟着丢。

## 两条可选修法

- **(a) 建图时让循环节点成为体区值门的使用者。** 循环每一轮都要消费体区的值，体区的值门本来
  就是给那台循环用的。这是把已有的事实写进图里。
- **(b) 在活跃性分析里加一条"任何 `loop` 语句的体都算要执行"。** 那是给一个特例开后门，以后
  每个类似形状都会想加一条。

## 裁定：(a)

理由是结构上的，不是偏好：(a) 让图说出它本来就有的事实，(b) 让检查认识一个特例。作者一贯反对
"现有机制够用时另发明一个"。

## 落地状态

**形态确认（推翻本页初稿的一句）**：`for v in <生成器> do …` 与 `loop … while …` 走的是同一个
`src/compile/gir/builder_control.cpp#Builder::visitLoopNode`；`visitStaticForNode` 管的是另一条。
本页初稿写"没有 `LOOP` 节点"是**错的**——那是 grep 输出被 `head` 截断造成的误读。完整的
`std::gir` 里三个循环节点都在：

```
N94 [label="LOOP while", type="LOOP", origin=7107, span="26:4-26:66", value_type="(int.., bool, int)"]
NAD [... span="28:4-28:66" ...]   NC5 [... span="29:4-29:80" ...]
```

**量到的形状**（只看 `main` 自己的节点与边）：

| | 在 | 不在 |
|---|---|---|
| 循环节点 | `N94` / `NAD` / `NC5` | |
| 体区算出来的值 | `N8E :op/add_l`，经 `FILL`/`BRCH` 接进循环 | |
| 体内 `println(v)` | | 不在 |
| 链位 `GATE` | | 不在 |

即：体区把**纯值**交给了循环，链那一支（`println` → 链位 → 块值 `GATE`）没有使用者，被剪掉。

## 落点在 `visitBodyNode` / `visitExecNode` / `setArmResult` 这一条线上

- `Builder::visitBodyNode`（`builder_control.cpp`）取 `visitExecNode(gct, tailPositionType())` 的
  返回值，它就是体区交回去的东西；
- `visitExecNode` 收尾（`builder.cpp` 1820–1845）有两条路：值节点为本块独有 → `res` 原样（自己当
  链尾，`linkSyncStatement(lastSyncedNode_, res)`）；否则造一个 `GATE`（`res = gate`）并
  `writeSyncStatement(lastSyncedNode_, gate)`。本用例走的是**造 GATE 那条**；
- `Builder::setArmResult` 把 `arm.output = result`、`arm.value` = 该 `GATE` 的数据输入（注释写明
  "the GATE is what the arm gives"）；
- `buildArm` 建区域时 `synced_ = false; lastSyncedNode_ = {}`（1552–1553 行），区域自己的链从空开始；
- 之后 `visitLoopNode` 里体区的结构被拆成分量时（`if (bodyPart->output != bodyPart->value)` 一带，
  3536–3541 行）只写了一条 `writeControlLink(bodyPart->output, result)`（**顺序**），再把
  `bodyPart->output = result`。区域的结果改成 `result` 之后，那个 `GATE` 就没有任何使用者了。

**还差的条件**：体区交回去的必须是**链的最后一站**，而不是它转发的纯值——要么 `setRegionResult`
仍然指向那个 `GATE`，要么循环节点直接成为它的使用者。现在的代码只把顺序交给了 `result`，
顺序边不进活跃性，所以链那一支被剪。

这两条与"链首不造 `GATE`"不冲突：链首那条管的是链**之前**没有语句的位置，这里要交回的是链的
**最后**一站，那里有语句、有前驱。

## 待办

1. 按上面的"还差的条件"改一处，复跑 `feat/compile/stream` 三个用例；判据是 `main` 的 `std::gir`
   里重新出现体内的 `:io/println`，并且 A 的 34 条掉到 0（逐名报）。
2. **分支臂的同一条线先不动**：`buildArm` / `setArmResult` 也服务分支臂，臂的那一半要用例说话，
   先量再改。