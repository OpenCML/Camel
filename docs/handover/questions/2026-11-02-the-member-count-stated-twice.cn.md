# 余量元组在没有图时，该不该有"未知元数"的类型？

**2026-11-02，`lane-ledger2`，树 `18ddc4eb3` + 未提交的一处修（`include/camel/core/rtdata/tuple.h`）。**

> **作者 2026-11-02 05:10 裁定：`split` 不再交出残差元组（改为交出 `(y, lin)`，残差作为 `lin` 的 `with` 参数）⇒ 本页要解决的"同一个成员数被说两遍"在这条路上自然消失；若仍有实例，那时的类型就是真类型。** 下面的 A/B/C 与读数保留为这条问题被裁定前的现场，不再作为等回答的问句。

## 问句

`split` 的余量元组在"没有图在手"时（operator resolver 只看到类型，`modules/transforms/split.cpp:841-843`）被写成
`TupleType::residuals({})`——**成员数为 0**。运行时那个槽里放的却是真的余量元组（**2 / 3 / 5 个成员**，实测）。
类型该不该有一个"元数未知"的答案，而不是"就是 0 个"？

## 让它是问句的读数

1. **两个数并列**：`vjp_arities.nvm` 崩在 `typeAt: index 4, size 0 (residuals)`，同一轮 stdout 写 `residuals=5`；
   `remat.split.nvm` 崩在 `index 2, size 0`，同一轮写 `residuals=5`。⇒ **值 5 个，类型说 0 个。**
2. **读的人是收集器，不是 autodiff**：`gdb` 栈为 `TupleType::typeAt` ← `Tuple::updateRefs` ←
   `scanRelocatedObjects` ← `minorGCUnlocked` ← `safepoint` ← `NodeVMSchedPass::call`。只有
   `--gc-check poison`（每个 safepoint 都搬）才走到，去掉后 4/4 通过。
3. **一路的读数**（直跑 21 条，`--gc-check poison`）：**15 条转绿、6 条仍崩**；21 条**每一条**都打出
   `a tuple value has N member(s) but its type declares 0`（N=2/3/5）。仍崩的 6 条改成
   `MemoryExhausted: an allocation of 121631737.0 MB does not fit`——**没被追踪的成员被当成大小读**。
4. **没有"未知元数"这种类型**：`TupleType` 只有 `create / cell / nullable / fallible / residuals`
   四类用法，`variadic` 属于函数签名，不属于元组类型。

## 另有一条路：在运行期构造点修正类型（**找过了：没有**）

`modules/transforms/split.cpp:1219` 已经拿到了真数（`TupleType::create(residualTypes)`），所以先要找的是"值造出来的那一刻能不能顺手把类型改正"。**找不到，而且是结构上找不到：**

1. **`Tuple` 值不存类型**：`include/camel/core/rtdata/tuple.h:416-418` 的私有部分只有 `uint32_t size_` 与 `slot_t data_[]`；带类型指针的是紧凑视图（`ViewData::type`，只有 `isView()` 才读）。**普通元组的类型永远来自槽声明。**
2. **运行期造这个值的两处都不带真类型**：`modules/transforms/split_operator.cpp:168` 是 `::Tuple::create(2, mm::autoSpace())`（无形参类型）；`modules/transforms/split.cpp:1498-1500` 是 `fillTuple(draft, halves.backward->normTypeAt(0), …)`——**它用的就是编译期那个类型对象**，空占位原样传下去。
3. **`split` 是运行期算子**（`modules/transforms/split_operator.cpp:124` `splitKernel`）：图只有进了内核才拿得到，而调用点槽位上的类型**在编译期就由 operator resolver 定死**（`split_operator.cpp:59` 的空占位）。**内核里的真数改不到调用点的签名。**

⇒ **运行期没有任何地方能把类型改对。** 编译期那处"有真话"的地方恰恰落在"没有图"的一侧：`split.cpp:1194-1196` 的注释自己写着，**签名从类型算不出余量个数**（`f(x) = x` 与 `f(x) = x*x` 的 lifted 类型一样）。**这就是"只能选 A 或 C"的直接证据**：要么类型能说"未知"，要么 resolver 只能拒绝。

## 三条路与代价

| 路 | 代价 |
| --- | --- |
| **A. 给元组类型一个"元数未知"的答案**（余量用它） | 新 TypeCode 加上每个读 `TupleType` 的地方（`codes()`、`typeAt`、打印、相等、interning）都要回答"未知"；收集器要有一条"未知时怎么扫"的规则，这一条正是安全策略，得作者定。**上一节已确认没有更小的路：运行期改不了类型，编译期那一侧算不出数** |
| **B. 原地兜住**（本轮已做：按类型上界扫 + 每类型报一次，不中止） | 不越界读，但**漏追踪变成常规路径**：6/21 仍崩（换了句子），且"少扫"安全与否取决于"漏掉的成员里有没有引用" |
| **C. 让两半永远由图来定型**（去掉 resolver 的空占位） | operator resolver 没有图，只能改成"拒绝"；今天能靠签名通过的用例会变成拒绝 |

## 默认（按默认先做，不等回复）

**默认 A**：**"不知道"和"零个"是两件事**，类型必须能说"我不知道元数"，否则每一次"没有图在手"都在对收集器撒谎。
在 A 落地之前，**B 作为权宜留在树上**（它至少把静默改成点名，并且不再越界读），但**不许把 B 当成正解**。

## 附：同一条判据的第二个产生者（不是本页的问句）

`nn` 的 `gru_lite_large_bytecode` / `residual_mlp_large_bytecode` 是**闭包元组被函数签名类型描述**
（`Tuple@… 11 槽`，owner 类型是 `(x0: f32[..], …) => float pure`）。**同一个判据**（"类型说的成员数 = 值有的成员数"），
但**走另一个追踪器**：栈是 `markObject` ← `markPhase` ← `collectGraphSpace`，**不经过 `Tuple::updateRefs`**，
所以本轮加在 `rtdata/tuple.h` 的检查**不会点名它们**（实测：两条仍只打 `GC poison: a raw pointer …`）。
两族要合成一条，检查得同时加在图空间标记那一处——那是 `h-a3` 的活。