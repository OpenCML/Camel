# 嵌套分支停在 `jvp`，不是 `split`

**日期**：2026-11-02。**状态**：开路（下一片）。**量的是**：`split.cpp:938` 那条 deferred（"一只臂的
半边没有类型"）今天走到没有。

## 最小程序

```cml
func f(x: float): float {
    return if x > 0.0 then (if x > 1.0 then x * x else 2.0 * x) else 3.0 * x
}
```

读 `vjp(f)(2.0)`（推导式：`vjp = transpose . split . reinterp<jvp>`）。

## 今天停在哪一行

```
REINTERP jvp function=f skip=branch
(vjp)…/stdlib/vjp.cml:56:35: [Error]: An error has occurred: transforms: jvp has no forward transform
for this function.
```

**停在 `jvp`，比 `split` 早一级**，所以 `split.cpp:938` 这一片今天根本走不到。

## 缺的是哪一半

`jvp` 的分支那一支（`jvp.cpp` 的 `Brch` 处理）要求**每只臂的结果**都已经有切向：

```cpp
if (arm == nullptr || arm->result == rt::kInvalidNodeRef ||
    !copied[arm->result] || tangentOf[arm->result] == rt::kInvalidNodeRef) {
    complete = false;
    break;
}
```

嵌套 `if` 的臂，其结果是**内层的那只 `Brch`/`Join`**；而 `tangentOf` 只给"被复制过去的值"配切向，
没有给**一个 `Brch`/`Join` 本身**配切向的地方——分支那一支只处理**最外层**的那只 `Brch`。于是
`tangentOf[arm->result]` 是 invalid，`complete = false`，读到 `skip=branch`。

所以缺的那一半是：**"臂里的臂"要能递归地长出它自己的 `(value, tangent)` 对**——即 `jvp` 的分支处理要
对"臂的结果本身是分支"再做一次同样的处理，而不是只认"臂的结果是一个值节点"。这一半补齐之后，
`split.cpp:938` 那句话（"一只臂的半边没有类型"）才会真正轮到：那时臂的结果是一个 pair 类型的内层
`Join`，`split` 要决定它的半边类型是什么。

## 不做什么

- 不在这里改 `total`/分母，也不把这条形状的用例移进 `test/cases/modules/autodiff`（那是人的决定）。
- 不给嵌套分支编一个"混合两只臂"的答案：暂停的条件是**内层分支实际跑了哪只臂**，那是一个值，不是
  一个可以近似的东西。

## 想补这一半时撞到的东西（2026-11-02 停在这里）

按"给内层 `Brch`/`Join` 配切向"去改，第一步就发现它**不是给已有的值加一个字段**，而是**要一个新的值**：

- `jvp.cpp` 的分支那一支（`for (ref…) if (kind == Brch)`）已经把**每只臂的结果**替换成 `liftedPair(copyOf[arm->result], tangentOf[arm->result])`——一个 `(value, tangent)` 的二元组，注释说"臂的出口和循环体的出口都是这一对，所以只在一处制造"。
- 于是分支的 `Join` 的 `withInputs` 是**一堆 pair**，而它的 `dataType` 仍是标量。**它的切向不是某个现成节点的一半**：哪只臂真的跑了，切向就是那只臂的 pair 的第二元；要把它取出来，只有两条路：
  1. **再造一个 `Join`**（同一只 `Brch`、同样的臂、输入是各臂 pair 的第二元），把它的结果记成 `tangentOf[join]`；
  2. 给 `Join` 的 pair 增加一个"取第二元"的节点。
- 两条路都要在分支这一支里**新增一处 `tangentOf[join] = …` 写入**，并且**新增节点**（一个镜像 `Join`，或一个取元节点）。这已经超出"把已有规则记到调用节点上"那类改动的形状：它是**在规则展开里再走一遍同一只分支**，第二次读"哪只臂是哪只"。

按 15:50 的硬要求（**只有规则一处来源**），我停在这里没有动代码：**这一处该不该是第二处 `tangentOf` 写入点，是作者的话**。要问的具体一句是：

> 分支（`Brch`/`Join`）的切向，是"同一只 `Brch` 上再 `Join` 一次各臂的切向"（镜像 `Join`，O(1) 新节点），还是"把臂的 pair 当值、由 `Join` 自己产出 pair"（改 `Join` 的类型与解构）？前者更像今天的形状，但它确实在分支这一支里**多一处切向写入**；后者要把 `Join` 的 `dataType` 改成提升后的类型，动的是 `Join` 的语义。

在作者答这句之前，嵌套分支保持 `skip=branch`（**有名字、有理由**），不拿"近似"或"混合两只臂"顶上。
## 作者的裁定已实现（2026-11-02 晚，`jvp.cpp` 走通，停在下游一处）

作者 17:30 的裁定是：**按区域从内到外递归，代码只有一处**。照此改了三处（同一处分支处理的三个点，不是"嵌套"另写一支）：

1. **接线顺序**：把所有 `Brch` 按区域深度**从内到外**排序再接线（`regionDepthOf` + `stable_sort`）。内层 `Join` 先成对，外层臂才读得到它。
2. **`Join` 的诞生位置**：预扫里 `addJoinNode` 改成**在它自己那张 `Brch` 所在的区域**里创建。原来一律建在 region 0，于是嵌套时"区域 0 的 `JOIN` 读臂里的 `BRCH`"，运行时的不变量直接拒绝：`JOIN 3 reads BRCH 10, which lies in an arm of BRCH 7`。
3. **`Join` 的两个半边**：一个**是某只臂的结果**的 `Join`，它的值与切线是这个对的 `.0` / `.1`（与循环状态端口读法同一套 `addAccsNode(type, 0|1)`）；图自己的那只 `Join` 不投影——它本身就是出口要的对。

`skip=branch` 就此消失，`jvp` 真的把嵌套分支建出来了：

```
REINTERP jvp function=f graph=61 liftedFrom=55 type=(float, float) => (float, float) pure nodes=14 kinds=14/5 exit=Tuple{chain,tangent}
REINTERP jvp built graph=61 nodes=31 exit=2 regions=5 function=f
REINTERP jvp callees=0
```

## 于是停在 `split`，而且这一处是"第二处"

```
REINTERP split function=jvp_of_f skip=forward construct=a tangent port uri=- why=the value half reads the tangent of parameter 0, which the forward half does not take
```

原因不是 `split` 有 bug，而是上面第 3 点：给"是臂的结果"的 `Join` 造的**切线半边（`.1`）落在该臂所在的区域里**，而 `split` 的**前向半**拿到的是同一批区域成员，读到 `.1` 就按规则拒绝（"前向半不读切线"）。也就是说：

- `jvp` 一侧**一处代码**确实够了（作者说得对）；
- 但要让 `vjp` 出 4.0 / 2.0 / 3.0，还需要 `split` 知道**"嵌套分支的切线半边不属于值半边"**——那是在 `split` 里加判断，**是我这轮被告知的"第二处"**，所以照嘱咐停在这里，代码已回退到 `1b354bf12`（`jvp.cpp` 无改动），只留下这份读数与形状。

要问作者的一句：嵌套分支里，`split` 的前向半该**不复制 `Join` 的切线投影**（按区域成员的角色筛掉 `.1`），还是让 `jvp` 把切线投影**挪到臂外**（在分支所在区域里对 `Join` 的 pair 取 `.1`，臂内只留 `.0`）？两种都能让前向半干净，前者动 `split`，后者动 `jvp`——后者仍然是"一处"。
## 为什么 (b) 不能只动 `jvp`（2026-11-02 晚，按 (b) 再试一次后的读数）

按 (b) 的意思"切线投影挪到臂外"，我先核对了它的前提，结论是**前提不成立**，所以没有再改代码，先把原因写清：

- **投影已经在分支自己的区域里了**。我上一轮把两个半边都建在 `Join` 所在区域（`function.nodeRegion(body->join)`；`Brch` 在区域 R，它的 `Join` 也在 R）。对嵌套而言 R **就是外层臂的区域**——内层 `Brch`（以及它的三只臂）在更里面。所以"内层臂的区域里只有 `.0`"这条**已经满足**：`.0`/`.1` 都不在内层臂里。
- **泄漏不在投影，在臂的出口本身**。`jvp` 的每只臂交出的**一个**结果就是 `liftedPair(值, 切线)`，那个 `Tuple` 节点**就在臂里**（`ARM … yield=26 members=2 value=23 tangent=24` 那两个成员之一）。要拿它的 `.0`，就得先算这个 `Tuple`，而算它要第二个输入——切线链——一路到 `dx`（`p0`）。于是 `split` 的前向半走到 `p0`，按规则拒绝（**它拒得对**）。
- 这也解释了**为什么非嵌套的作者形状能过**：那条臂的出口同样是 `Tuple`，但 `split` 的半走**没有嵌套区域**时能按"每只臂取 `.0`"重建；`WALK … nested=1` 那一行正是嵌套进入的地方，之后它就退回到"整只臂"这个粒度，把 `Tuple` 连切线一起复制了。
- **而"把 `.1` 挪到臂外"在结构上做不到**：区域外的节点**不能读进臂里**——这正是这一轮修掉的那个不变量（`JOIN 3 reads BRCH 10, which lies in an arm of BRCH 7`）。要让 `.1` 在臂外，只能让臂**不交出 `Tuple`**（只交值），那切线就没有出口了；除非**再给每只臂第二个结果**（一个切线出口），那既不是今天的 `GCRegion` 形状，也是"第二套机制"。

所以 (b) 与"臂的出口是一个对"这条现有形状冲突；真正缺的是 `split` 对**嵌套臂**的半走：它得能"整只臂只取 `.0`"地重建一只内层分支，而不是把整只臂连切线一起复制。这落在 (a) 的地界里，但**不是**"改掉那个对的拒绝"——拒绝仍然对；要补的是**嵌套臂的值化重建**。

写在这里，等作者/你重新裁：是把 (a) 限定为"`split` 的嵌套臂按值重建"（拒绝不动），还是先让 `jvp` 在**嵌套**这一层把臂出口改成别的形状（那要回答"切线从哪只臂出来"，属于语言形状的裁定）。