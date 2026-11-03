# loop 那一半的转置要的值，在调用点存在吗？在——但那是一个循环的值

## 问什么

`vjp_loop.rounds_are_undone_in_reverse` 到今天仍停在同一句上（`017003247` 上实测的原文）：

```
TRANSPOSE function=split_of_jvp_of_g_backward skip=call-value-missing param=0 node=23 arg=2
error[RuntimeDiag::RuntimeError]: transforms: transpose has no transpose for this function:
  the callee's transpose was built, but the parameter at position 1 reads a value this call does not carry.
```

`param=0` 是 0 起算（句子里的 "position 1" 是同一格的 1 起算写法）。这一句有两个可能的因：
调用点**没有**这个值（那就要先给调用点补一个值，是更大的事），或者调用点**有**、只是没搬过去。
这一页给出的是：**有**，它在 `node 23` 的 `inputs[0]`，也就是 `ref 2`；而它搬不过去的原因既不是"按类型猜"、
也不是"没按身份"，是 `forward` 不认识那种节点。

## 读数（临时探针，读完即撤；`git grep PROBE-` 现在是空的）

```
PROBE-NEED function=split_of_jvp_of_g_backward callee=rulesets.jvp::index_into<float> idx=0 \
  node=23 arg=2 kind=15 type=float[] argumentAt=0 calleeParams=2 isCall=0 \
  calleeType=(x: float[], i: int) => float pure inputs=[2:15:float[] 22:0:int]
PROBE-FWD function=split_of_jvp_of_g_backward ref=2 kind=15 type=float[]
```

拆开看：

- `idx=0` 是 `state.calleeNeeds[index_into]` 的内容，也就是"成员的**体**自己问了参数 0 的值"。参数 0 是
  `x: float[]`，不是那个 `int`：`y = x[i]` 的转置要把余切写进 `zeros_like(x)`，而一个 `T[]` 的长度
  不在它的类型里，只在它的值里。这正是 `split.cpp` 那句注释说的形状。
- `argumentAt=0`、`inputs=[2:Loop(float[]) 22:Data(int)]`：`inputs[0]` 就是参数 0。**序号是对的**，
  身份是 `node 23` 的 `inputs[0]` → `ref 2`。站 1/2 的"按身份补"这条路没有走错格。
- `ref=2` 的 `kind=15` 是 `GCNodeKind::Loop`（`include/camel/runtime/graph.h:126`），类型 `float[]`。
- 所以 `forward(2)` 拒绝的原因在 `modules/transforms/transpose.cpp:1017`：
  `if (node->kind != rt::GCNodeKind::Oper) { complete = false; return kInvalidNodeRef; }`。
  `forward` 认识的只有 `Port`、`Fill`、`Accs`、`Call`、`Func`、`Oper`，**没有 `Loop`**。

## 那个 tape 是不是本来就在闭包里？（D 路，已排除）

`residuals=1` 确实说明后一半只带了一个残差，所以先问"那个值是不是已经在手边"。第二个探针把 `ref 2` 的
形状打全了：

```
PROBE-CLOSURE function=split_of_jvp_of_g_backward ref=2 kind=15 type=float[] members=1 \
  [0:1:(float, float[], float, float, float)[]] ports=1 isMember=0 hasPort=0 closureType=none \
  norm=[0:1:(float, float[], float, float, float)[] 1:1:float[] ] with=[20:4:float[] 21:1:int ] regions=2
```

读出来两件事，一件成立、一件不成立：

- **tape 在闭包里——成立。** 唯一的残差就是 `ref 0`（`kind=1` `Port`），类型
  `(float, float[], float, float, float)[]`，正是那条 `tape=5`；而且它就是 `ref 2` 的 `norm[0]`
  ——这个循环是**遍历 tape** 的那个循环。
- **"所以 `forward` 答闭包端口就行"——不成立。** `isMember=0`、`hasPort=0`：`ref 2` 不是闭包成员，
  它是这个循环的**输出**（`with=[20:Fill(float[]) …]` 是它自己的区域结果 + 轮端口，`regions=2`）。
  闭包里躺着的是循环的**输入**，要的是循环的**结果**：从 tape 到那个 `float[]` 之间隔着"把循环跑一遍"，
  也就是抄一个循环。省不掉。

还有一条更硬的理由：即使把规则放宽成"凡是闭包给得出的都答闭包端口"，这里给的也会是**类型不对**的东西
——`ref 0` 是 `(float, float[], float, float, float)[]`，参数 0 要的是 `float[]`。D 若照这个写法落地，
不是少抄一个循环，而是把一个 tuple 数组当数组交给 `zeros_like`。

## 结论

这个值在调用点**存在**，身份也**已知**（`split_of_jvp_of_g_backward` 的 `node 23`、`inputs[0]`、`ref 2`），
缺的不是"把实参搬过去"——搬的那条路是对的——也不是"读闭包端口"（D 已排除）——而是 `forward` 能把一个
`Loop` 节点连同它的 `LoopCond` / `LoopBody` 两个区域抄进转置图。**那是"抄一个循环"，不是"补一格值"。**

三条路：

- **A：给 `forward` 加 `Loop` 分支**：抄 payload（`collectionCount`、区域引用、端口）、抄两个区域。
  代价是 `forward` 从此要能抄区域，而它今天连 `Brch`/`Join` 都不抄。
- **B：让 `idx_arr_t` 的转置不读 tape 的值**：现在它靠 `zeros_like(x)` 拿长度，改成由余切/形状之外的
  东西决定长度，是算子契约的改法，要作者裁。
- **C：把"loop 那一半的 tape"划出这一站**（默认建议）：这一站做到"数组那种形状按身份走通"，loop 那种
  形状连同 `docs/handover/questions/2026-11-03-does-a-one-member-closure-prove-the-slot-identity.cn.md`
  的校验收紧**同一个动作**做。理由：A 要 `forward` 学会抄两个区域，B 要改算子的转置怎么拿形状，两者都是
  架构改动，该由作者裁；而这条用例现在是 `known_fail` 而不是新红，留着比赌一个架构改动安全。

默认是 C，但我不替作者选。要提醒的一点：`docs/handover/questions/2026-11-03-does-a-one-member-closure-prove-the-slot-identity`
里说的"后面还有一道墙挡着"，那道墙就是这一页的 `forward` 缺 `Loop` 分支；也就是说，选 A 或 B 的人等于
同时拆掉那道墙，那时单成员闭包的误过校验就会走到出口——两件事必须一起做。

## 同一句诊断，两种成因（这一页要留下的东西）

`skip=call-value-missing` 是**一句诊断盖住两件事**，读的人必须知道它今天有哪两种：

| 成因 | 要的值 | 一句话 | 今天 |
| --- | --- | --- | --- |
| 数组那种形状 | 参数**自己**的值（`zeros_like(xs)` 要长度） | 站 1/2 之后按身份搬过去 | 已修，8 条数组用例逐名绿 |
| loop 那种形状 | 循环**算出来**的值（`zeros_like(tape 的收集结果)`） | 要抄一个循环 | 未修，停在本页 |

`56aa32a42` 之后我说过"`call-value-missing` 不再出现"。那句只对第一行成立：两种形状碰巧共用同一句
诊断，所以那句话读起来像"修好了"，其实只覆盖一半。**下次读到这句诊断，先问"它要的是参数自己的值，
还是体自己算出来的值"，再看是哪一条。**

## 两个词之后（`86b21fe7e`，这张表的两种成因现在各有一个词）

上面那张表是历史读法。`86b21fe7e` 之后，同一句诊断拆成两个词，读的人不必再猜：

- `skip=call-value-missing` —— 调用点没带**参数自己的值**；
- `skip=call-value-computed-by-the-body` —— 要的值是**体自己从循环或分支里算出来的**，拿到它要转置整块体。

判据换了地方：`forward` 在它"遇到不认的 kind"的兜底处记 `stoppedAtARegion`，两个搬不过值的地方读这个
标记，而不是从手上那个节点的 kind 去猜——因为手上拿到的可能是一个区域结果的**投影**（`Accs` 投影循环
的 cell），不是区域本身。诊断因此记的是量出来的事实。

`vjp_loop.rounds_are_undone_in_reverse` 现在报第二个词，逐字：

```
TRANSPOSE function=split_of_jvp_of_g_backward skip=call-value-computed-by-the-body param=0 node=23 arg=2
```

外加一句：

```
transforms: transpose has no transpose for this function: the callee's transpose was built, but the
parameter at position 1 reads a value the function's own body computes out of a loop or a branch, so
carrying it means transposing that body
```

### 可达性（这是这一节真正要留下的）

`feat/compile` 全层的 2186 个 stderr 逐文件 grep：两个词一共命中 **1 次**（就是上面那条 `vjp_loop`），
`call-value-missing` **一次都没出现**。原因是清楚的——曾经到达第一个词的那种形状正是 `34788b554`
修好的（参数原值作为残差进闭包）。

所以：**第一个词今天没有程序能到。** 保留它的理由写在 `modules/transforms/transpose.h` 的枚举成员上——
它命名的是 walking 仍可能进入的一个状态，不是因为有一道活的检查守在那里。要给第一个词一条读数，得造
一个图级用例；那属于新用例而不是修，这一站没有做，也不该为了让表看起来齐而造。

### 这一页与那两行注释的来历

本节的读数是 `lane-looptape`（`86b21fe7e`）量的；页上这一节与枚举上的两行可达性注释是**合并车道代写**
的，车道本身没有再提交一次文档——它把原文交了出来，落笔由合并这一步完成。以后若发现这两处措辞与车道
的话不一致，以车道当时的原文与这里逐字引用的读数为准。

## 文件与读数

- 拒绝点：`modules/transforms/transpose.cpp:1017`（`forward` 的兜底返回）。
- 需要值的那一侧：`modules/transforms/transpose.cpp` 的 `calleeNeeds` 循环（`state.calleeNeeds[callee]`）。
- 案例：`test/plans/feat/compile/transforms.plan.toml#vjp_loop.rounds_are_undone_in_reverse`（`known_fail`，
  原文见上）。
- 车道读数：`feat/compile/transforms` 80 = 77 过 / 2 红 / 1 known；`split` 19/19；`remat` 6/6。
