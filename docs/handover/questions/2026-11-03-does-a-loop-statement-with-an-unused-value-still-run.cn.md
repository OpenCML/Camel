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

`loop … while …` 形态把体区出口交给循环节点的那一处在
`src/compile/gir/builder_control.cpp#Builder::visitLoopNode`（`linkNodes(LinkType::With,
bodyPart->output, loopNode)`，第 3640 行一带）。

但本页这三个用例的形态是 `for v in <生成器> do …`：`std::gir` 里 27/29/30 行只剩两个节点
（`"-"` 与 `:io/println`），**没有任何 `LOOP` 节点**。所以这条形态不走 `visitLoopNode`，体区值门
该接的那个"循环节点"是别的节点，**落点必须先量出来再改**，不能按 `while` 形态的那一处顺手改。

## 待办

1. 量出 `for v in` 形态里"循环节点"是哪个（体区的值应当成为它的 `with` 输入）。量法：在
   `for` 语句建图处打印体区出口的使用者，或在 `std::gir` 里找该语句留下的 26 个节点中承接
   体区结果的那一个。
2. 接上之后复跑 `feat/compile/stream` 三个用例，判据是 `main` 的 `std::gir` 里重新出现
   `:io/println`，并且 A 的 34 条掉到 0。