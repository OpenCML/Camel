# 一个成员的端口槽没有人填

**读数树**：`25880f2a8`（原 `lane-splitprimal`，该分支已删；clean，`out/latest` Release；崩溃那一次用 `out/debug` 的带符号构建）。
**程序**：`h(x) = loop s = 0.0 for i in [1,2,3,4] do s + 2.0`，经 `vjp(h)`；对照 `k(x) = loop s = 0.0 for i in [1,2,3] do s + 2.0` 直接 `transpose(k)`。
**一句话**：转置出来的那个成员图里有一个 `Port` 端口没人填，执行到读它的 `Accs` 就崩；而它之所以没人填，是因为"零答案"让这个成员不再声明它需要的那个捕获。

## 一、甲：让零"经过所携带的值"的三次读数

同一个 `h`，同一份树上只改 `modules/transforms/transpose.cpp` 里零答案的构造：

| 零怎么造 | 读数 |
|---|---|
| 裸常量 `0.0`（今日 `transpose.cpp` 的零答案分支） | **exit 139**：`Tuple::size (this=0x0)` ← `nodevm.cpp:548` ← 两条 `call` 帧 ← `apply:857` |
| 尚未改动（守卫 `closurePortOf.empty()` 在） | `TRANSPOSE function=split_of_jvp_of_h_backward skip=state-unread node=2 origin=0`，**exit 1** |
| 经过捕获："`0.0 * len(c)`"（`:op/mul_d` + `:struct/len_arr` + Cast） | **不崩、不 skip**：`h value= 8.0`、`h lin(1.0)= 0.0`，但图被 commit 拒：`graph 'transpose_of_split_of_jvp_of_h_backward' node 12 (2): converts a int to a float, which is forbidden` |

第三条是**被提交时的边不变式挡住的**（`src/runtime/graph_edges.cpp` 的 Cast 检查：`castSafetyFrom(...) == CastSafety::Forbidden`）。它证明"让零经过端口"能让图建起来、能跑，只是构造它的那一步不合法。

## 二、捕获是"元组数组"，不是数

第一次尝试时的实测打印（零答案分支内临时加一行）：

```
TRANSPOSE-DBG zero node=2 closure=1 answerType=float answered=0 cap{0:(float,)[]->0}
```

- `closure=1`：这一半只携带一个闭包值
- 它的类型是 **`(float,)[]`** —— 循环的 tape（元组的数组），不是数
- 所以"`0.0 * c`"类型不成立；要用它，只能读它的长度（`Int64`，转 float 被禁）或读它的下标（空 tape 会越界）

**两条都比拒绝差，所以零答案在今天只能是不读捕获的常量，而常量答案会让捕获变成"没人填的端口槽"。**

## 三、乙：一条判据的 6315 红，和它的单一解释

判据写成"每个 `Port` 节点的 `dataIndex` 必须落在 `参数个数 + 闭包槽数` 之内"，挂进 `src/runtime/graph_edges.cpp` 的 `EdgeChecker`（`checkPortsAreSupplied()`），跑全量：

```
[SUMMARY] total: 8545, pass: 2210, fail: 6315, skip: 10, known failures: 10, killed by a signal: 1
```

**单一解释：`Port.dataIndex` 是数据槽号，不是端口序号**（`GraphDraft::addPortNode(type, dataIndex)` 的第二个参数；NodeVM 里 `Port` 在调度时是空操作，值直接从帧里读）。所以每个图都红，读数是：

```
graph 'f0': port 1 is supplied by nothing -- the graph declares 1 parameter(s) and 0 closure slot(s)
graph 'wide': port 100 is supplied by nothing -- the graph declares 100 parameter(s) and 0 closure slot(s)
```

逐域红名：`feat.perf`（array / lambda / mixed / wide / fanout 全灭）、`feat.mm.writings`（`saturate_draw` 的 gir/rgir/jgir/egraph 四份）、`feat.modules.rules_differential`（整册）、boundary-role（01–08 全部）、类型兼容四例。判据已撤回，树回 clean。

**这一条的价值在它自己身上**：写这条检查的人如果不知道 `dataIndex` 是槽号，会以为自己写对了、以为"今天有一大批图不合法"，然后去追 6315 个假案。

### 换成"槽号对参数个数"再量一次，红数一样

判据改成"每个 `Port` 的 `dataIndex` 必须小于该图声明的参数个数"，再跑全量：

```
[SUMMARY] total: 8545, pass: 2210, fail: 6315, skip: 10, known failures: 10, killed by a signal: 1
graph 'f0': port writes slot 1 and the graph declares 1 parameter(s)
graph 'wide': port writes slot 100 and the graph declares 100 parameter(s)
```

**同一个红数，同一条解释**：一个图的帧除了参数还有别的槽（至少是答案的那个），所以 `slot == parameters` 是常态而不是缺陷。日志里这条断言出现了 **11796** 次，红遍 `feat.compile.*` 到 `feat.perf.*`。

**结论（这条要记住）**：按图内 `Port` 的槽号做判据，抓不住"没人填的那个槽" —— 崩的那一次，出问题的槽（`srcIdx=3`）在帧布局里是合法的、写它的节点也在，**只是调用方运行时没传值**。所以这是**调用侧的事实**，不是单图能判的事实；单图能判的只有"这个图声明的参数个数 == 它图里 `Port` 的数量"这一类形状一致性，而它和这次崩溃之间还隔着"签名是否把捕获算作需要"这一步。

## 四、缺口：调用点只传 3 个

崩的那一半是 `transpose_of_split_of_jvp_of_h_backward`，它的调用点装配在 `modules/transforms/transpose.cpp`：

```cpp
std::vector<gc_node_ref_t> callArgs = arguments;   // 2 个转发进去的值
callArgs.push_back(local[ref]);                    // + 1 个余切 = 3
draft.setNormInputs(made, std::span<const gc_node_ref_t>(callArgs));
```

被调方 266（`transpose_of_autodiff::add_tangent`）声明 `(float,float,float) => (float,float)`、真出口 `(float,float)`、调用节点类型 `(float,float)` —— **三处全相等，出口类型清白**。缺的那一个是**捕获**：零答案不读它，`transposeSignature` 就不把它算作"成员需要的值"，调用点也就不传；而成员图里那个读它的 `Accs` 还在。执行时端口槽是空的，`Accs` 读到 `Tuple* = 0x0`（Debug 栈：`i=5`、`en.ref=4`、`normCount=1`、`srcIdx=3`、`node(3)` 是 `Port`、元组下标 0）。

## 五、默认：带闭包的半边维持拒绝

- **`transpose.cpp` 不动**：`closurePortOf.empty()` 的守卫留着，`h` 经 `vjp` 得到 `skip=state-unread`（exit 1），而不是一个静默的错数或一次崩溃。
- **要落的不变式不是"端口有没有人填"这一条写法**：本页量过的两种写法（`dataIndex` 对 `参数+闭包`、`dataIndex` 对参数个数）都红 6315 条、抓不住崩的那一次；崩的那一次是按帧布局合法、调用方运行时没传值，属于调用侧的事实。
- **可落地的形状一致性只有一条**：一个图声明的参数个数等于它图里 `Port` 的数量（`transposeSignature` 与成员图要对上）。
- **不写"放宽 `checkReturnValueType`"**：它只对 `T?` / `T!` 出口生效是有理由的（`void` 声明的函数合法地交出它算出的值），放宽会红掉现有绿图。

## 六、读数可复现

```
# Release
CAMEL_HOME=<root>/out/latest LD_LIBRARY_PATH=<libstdcxx>:<root>/out/latest/libs \
  <root>/out/latest/bin/camel <程序>.cml
# 崩溃那一次（带符号）
gdb --args <root>/out/debug/bin/camel <程序>.cml   # 崩在 nodevm.cpp:548，frame 1 里读 i / en / srcIdx
```

`h` 的三个状态（139 / `skip=state-unread` / 建成但 Cast 不变式拒）都在本页第一节的表里，第三次那行诊断是逐字抄的。