# 体内造出的闭包，谁负责把它的捕获配成对

**日期**：2026-11-21 13:05（+08:00）
**来源**：`jvp-closure` 车道落地 `3fc9bb01d` 第 1 条时，四条路各撞一次，停手报。

## 一、要落的那一句

`3fc9bb01d` 第 1 条：

> 被变换的函数体**里面造出来的**函数值（体内的闭包构造节点、`split` 前向一半里造 `lin` 的那一步）：它的捕获是这一层算出来的值，切向就是这一层正在传播的切向。提升**这个构造节点**时，**新闭包**捕获 `(x, dx)`，代码对参数和捕获一起求导。

同一条答还写「`transpose` 现在从 `Lift(lin)` 读残差的切向 —— 那就是路线 (b)，**全在 `jvp.cpp`**」。而车道的实测是：**这条规则在 `jvp.cpp` 里落不下去**，原因是它的落点归属不在那里。

## 二、为什么落不下去（结构性读数）

`split` 的后向半图 `split_of_jvp_of_sq_backward` 的闭包布局 `(f32,)`，是 **`split` 这一趟在它自己的图里定下的**（捕获是那一层算出的残差）。而：

1. **`jvp.cpp` 里能设的闭包类型只有本图自己的。** `draft.setClosureType(...)`（`include/camel/runtime/draft.h:680-684`）设的是**这张 draft 自己**的 `closureType_`，即「这张图**通过自己的 Port** 读到的捕获布局」。它**不是**「某个构造函数值出来的值」的布局设置器。运行期 `Function::tupleType()` 读的是**成员图那一份**，所以两边必然对不上。
2. **对成员图再提升一次会二次提升。** 实测报错逐字：
   `Runtime indirect call norm-arity mismatch: caller graph 'jvp_of___g_362__' node 4 has 2 args, target graph 'jvp_of_jvp_of_split_of_jvp_of_sq_forward' has 4 norm ports`
   —— 因为 `copyOf[member]` **已经是** `jvp_of_split_of_jvp_of_sq_forward`，再提升一次就得到 `jvp_of_jvp_of_…`。
3. **「这个成员是不是已经被提升过」在构建期只有一个读数可读。** 两半判据是「有 `liftedFrom`」+「类型已是 `Lift<F>`」：
   - **类型**可读（`NodeFace::dataType()` 与 `jvpSignature(F)` 都是现成的）；
   - **`liftedFrom` 读不到** —— 它是 `GCGraph` 上的属性（`include/camel/runtime/graph.h:912`），而构建期手里只有 `GraphDraft` 与 `NodeFace`，draft 的节点没有「我属于哪张图」的句柄。**要读它只能在运行期上下文**（那里有 `::Function*`，可 `graph()->liftedFrom()`），而**闭包构造 Fill 的成员配对发生在构建期**。两个时机不在一处。

**⇒ 所以「两读数同时成立才不重复提升」这条判据在构建期不可执行。** 用只有类型那一条会变成「悄悄不提升」，而它没有反见证兜底 —— 那是这条规则最坏的失败形态。

## 三、四条路，各有具体撞点

| 路 | 撞点 | 读数 |
|---|---|---|
| (a) 在 `split.cpp` 的后向半图构造处按捕获的切向配对 | 现红线禁止碰 `split.cpp` | 未做 |
| (b) 类型层 `FunctionType::withClosureType` | `func.h:14-16` 成文声明与之反向；且 `intern` 键（`func.cpp:197-257`）无闭包位置，只加方法会让两个只差闭包类型的签名被 intern 成同一个 | 已作废 |
| (b′) 图级 `setClosureType` 重标成员图 | 设的是**本图自己**的捕获布局，不是它造出来的值的 | 未做即撞墙 |
| (c) 对成员图再提升一次（「新闭包」） | 二次提升：`jvp_of_jvp_of_…` 4 端口 vs 调用点 2 实参；且「是否已提升」构建期只有一半可读 | 新 `PassInternalError` |

## 四、要裁的那一件事

**`split` 造出来的那个后向半图，它的闭包布局该由谁决定？**

决定这一选择的关键在于 `func.h:14-16`：

> Captures are not part of a function type: which names a graph captures is a fact about the
> graph (GraphDraft::captureNames, GCGraph::captureNames), not about the type of its values.

**若布局是「关于图的事实」，那么决定它的应当是**造那张图的那一趟**（`split`），而不是**提升它的那一趟**（`jvp`）。** 提升本不该替别的 pass 决定捕获布局。

**但同一条答写着「全在 `jvp.cpp`」** —— 若那句是说「这条修法不碰 `transpose.cpp`」，它与上面的结论不矛盾（`transpose` 确实不该动）；**若它是说「连 `split.cpp` 也不碰」，那么它与上面的归属读数冲突，需要裁定。**

## 默认建议（写死，本轮按它开工）

**走路 (a)，并把那条红线**收窄**为：**

> **允许改 `split.cpp` 的**后向半图构造处**：在那里按捕获的切向把闭包配成 `(x, dx)`。**
> **不许改 `split.cpp` 的任何判词（`skip=…` / `cause=…` / 拒绝理由）、不许改它的前向半图、不许改 `transpose.cpp`、不许动两个 guard 的实现。**

理由：

1. **它合 `func.h:14-16`**：布局是关于图的事实，造图的那一趟决定它。
2. **它是唯一不需要「跨时机读数」的路**：`split` 构造后向半图时，它**知道**自己的捕获是不是这一层算出来的值（`3fc9bb01d` 第 1 条正是按「闭包在哪里造」区分的），不需要读 `liftedFrom`。
3. **它不引入新 API**，也不改类型身份。

**执行口径**：
- `split` 后向半图的闭包从 `(f32,)` 变 `(f32, f32)`；**根的捕获不变**（`floatgate.f32_closure_member_ports` 逐字守）；
- 判据：`jvp_vjp_f` → `1.0 / 2.0 / 3.0`；`grad(grad(x ↦ x⁴))(2) = 48`；`4296/4291/0/5`；`36/9/0/27`；`split` 的判词逐字不变。

**若走 (a) 发现 `split` 的判词必须跟着变（即「配对」改变了它拒绝/接受哪些函数）⇒ 那说明这条路的代价超出「只改构造」，停手报出，回到本页。**

**另一条本文要记的教训**：一条「既然 X 记在图上，就该由造 X 的那一趟决定」的归属推理，比「哪个文件里有现成 API」可靠 —— 后者会引向 `withClosureType` 那种**看起来能用的入口**，而它与成文设计反向。