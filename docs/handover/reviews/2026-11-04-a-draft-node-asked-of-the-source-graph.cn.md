# 余切类型问的是哪张图：造图节点被当成源图节点来问，回退值该不该继续静默说 `float`？

## 问什么

`modules/transforms/transpose.cpp#buildTransposeMember` 里有一个本地 lambda
`cotangentTypeOf(ref, fallback)`：它拿 `ref` 去问**源图**（`function->node(ref)->dataType()`），
再用 `camel/core/tangent_space.h#tangentSpaceOf` 求余切空间，问不出就返回 `fallback`（调用点
一律传 `Type::Float64()`）。

问题在于 `ref` 有两种来源：**源图的节点**（合法），和**本次 draft 造出来的节点**（`function->node`
对不属于自己的 ref 返回 `nullptr`，于是静默走回退）。2026-11-04 修掉的两处都是后者：一个 loop 节点
和它旁边 branch 的 join 节点被这样问，回退替它们答了 `float`，出口就成了 `(float,)`——而它包着的
值是一个 `float[]`。调解码 `RuntimeDiag::ExitTypeMismatch` 在
`vjp_loop.rounds_are_undone_in_reverse` 上报的正是这一句（`(float,)` 对声明 `(float[],)`）。

这一轮我按"调用点改成读造图时已经算出的类型"修掉了两处，**没有动那个回退**。要不要把这种问法
本身变成一次拒绝，是可以两条路都自洽、代价只有作者能权衡的事，所以写成这一页。

## 读数（可复核）

- 修前的实测（在本树上临时打点，读数来自 `transpose.cpp` 的两处 `os`）：
  - `PROBE member name=split_of_jvp_of_g_backward source=(float[]) => float pure`
    `signature=(float) => (float[],)` `params=1` —— 声明是 `(float[],)`，来自
    `transposeSignature` 对**源图参数类型** `float[]` 的读法，即声明本身是对的。
  - `PROBE result=loop node=63 loopType=float[] ct=float` —— 造出来的 loop 节点类型是
    `float[]`，而包它的那个一元组按回退答了 `float`。
  - `PROBE exit result=66 type=(float,) declared=(float) => (float[],)` —— 不符落在**值这一侧
    的类型**上，运行时那一格确实装着 `[1.0, 1.0]`（该用例的 `grad= [1.0, 1.0]` 修前修后逐位相同）。
- 同一个 lambda 的 19 个调用点里，只有上面两处传的是 draft 的 ref；其余都传源图节点
  （`regionPorts`、`members[i]`、`entries`、参数端口）。也就是说"静默回退"这次只有两处被触发，
  但触发方式是**任何以后在 draft 节点上顺手问一句的人**都会踩到。
- 修后本树（`6f7ba033a`，develop `34b659107` 之上）：`test/plans/feat/compile` 2243 条
  `pass: 2230, fail: 2, known failures: 11`，与基线逐项相同；`test/plans/feat/transforms`
  `total: 29, pass: 21, fail: 0, known failures: 8`，与基线逐项相同。带调解码的合并树
  （`integ2/rules-batch2` 尖端 `ffa27bba1` + 这一笔）上 `compile/transforms` 由
  `pass: 107, fail: 11` 变成 `pass: 108, fail: 10`。

## 三条路，各带代价

**A. 维持回退，按调用点修（本轮已落地）**
- `cotangentTypeOf` 不动；两处调用点改读造图时已经算出的类型（`loopType`，以及为 join 从源图
  join 值读一次的 `joinSpace`）。
- 代价：下一个人仍然可以把 draft 的 ref 传进来，症状还是"很多层之后才在出口类型上显形"，
  而且显形的位置（`RuntimeDiag::ExitTypeMismatch`）指向的是**用例**而不是**这行代码**。
- 好处：零行为变化——本轮 2243 条与 29 条读数逐位未动就是证据。

**B. 让问不出来的情况拒绝**
- `cotangentTypeOf` 在 `function->node(ref) == nullptr` 时记一个
  `TransposeRefusalCause`（例如 `Kind`/`State` 之外新增一个词），让这个成员在**建图时**停下并说出
  是哪个 ref，而不是带着 `float` 走到出口。
- 代价：新增一个拒绝词就要在 `transposeRefusalWord` 与相关用例里交代；而且"节点在自己图里存在、
  但类型没有切空间"的合法回退（`()` 位、`int` 值）与"ref 不属于这张图"必须分开判，
  判错会把今天能转的函数拒掉——`transpose_loop_escape` 那两条已经是红的，风险落在它们附近。
- 好处：错误在**产生处**显形，读日志的人直接看到 `skip=<word> ref=N`，不用去反推出口类型。

**C. 让这种问法写不出来**
- 把 `cotangentTypeOf` 的入参从 `gc_node_ref_t` 换成"值 + 它的类型"（或者干脆换成 `Type*`），
  于是 draft 的 ref 根本传不进来——"节点属于哪张图"这个问题在签名上就不存在了。
- 代价：19 个调用点全要改一遍，其中若干处（`entries`、`members[i]`）本来就要顺手取一次
  `function->node(..)->dataType()`，改动量在本文件里，但会盖住 `git blame` 的一大片。
- 好处：与设计原则 9.9（答案的形状来自节点的类型，不靠现场重算）同一条读法，且这一类缺陷
  **不再可能**复发。

## 默认（本轮按这个开工，不等回复）

**这一轮走 A，下一个动这个 lambda 的人走 C。**

理由是可量的：A 的代价今天为零（读数逐位未动），B 会在已经红着的 `transpose_loop_escape`
附近动拒绝词，C 是唯一能让缺陷不再复发的改法但会重排本文件 19 处。所以顺序是
**先把这条用例救回来（A），再把签名收紧（C）**；B 只在 C 落下之前、有人又要在这个 lambda 上
加调用点时当作临时闸门。

作者若裁 B 或 C 优先，`lane-exitfix` 只回退上面两处调用点那一笔即可，其余不受影响。