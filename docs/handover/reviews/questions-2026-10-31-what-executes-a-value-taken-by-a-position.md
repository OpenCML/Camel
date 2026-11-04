# 被位置取走的值，怎么拿到执行的理由（不靠 ctrl）？

> **裁定：待裁。** 一天里的第三次：`ctrl` 有两种，一种替一条本该存在的数据边，一种**就是**那条依赖本身。作者说的"停造 ctrl"删的是前者（`531477399`）；后者删掉之后，值不会报错，它变成 `0.0`。

## 一、现象

`src/compile/gir/builder_operators.cpp#Builder::visitLinkNode` 与 `:4474-4479`，两处同一形状：一次写（或一次 `void` 调用）的**值被位置直接取走**——

```cpp
node_handle_t value = valueOf(normInputs[1].node);
node_handle_t gate  = currGraph_->addGateNode(nodeTypeOf(value));
linkNodes(LinkType::Norm, value, gate);
linkNodes(LinkType::Ctrl, targetNode, gate);   // ← 这一条
```

**这条 ctrl 不是"排在某次写之后"，是"gate 依赖那次写"**：`gate` 的值被用 → `gate` 必须等 `targetNode` → **那次写因此执行**。`targetNode` 的版本没有别的读者，**这条依赖是它唯一的执行理由**。

## 二、量过的三种挂法（`var_reference` 整张，259 条）

| 挂法 | 结果 | `var_param.*` / `var_tensor.*` |
|---|---|---|
| **保留 ctrl**（今天） | **259 / 259 绿** | 各 33 条全绿 |
| **`LinkType::Order`** | **193 绿 / 66 红** | 各 33 条全红，输出 `0.0` |
| **`LinkType::Sync`** | **192 绿 / 67 红** | 各 33 条全红，输出 `0.0` |

**红的是值，不是诊断**：`var_param.default` 的报文是 `unexpected output: 0.0 | 0.0 0.0 | 0.0 0.0`——**那次写没人要，它就不跑，值成了 0.0**。

## 三、结论（一句）

**`order` 与 `sync` 说的是"谁在谁之后"，`ctrl` 说的是"我依赖你"。位置把值递出去时，唯一让那次写非跑不可的理由就是这条依赖；换成"之后"的关系，写就没有读者。**

所以"停造 ctrl"不能按字面执行到这两处：`orderAfterLastWriter` 那条是**替身**（替一条本该存在的数据边，已删），这两条是**依赖**（那条理由本身）。

## 四、要作者答的一句

**被位置取走的值，除了"让 gate 依赖那次写"，还有什么能给它执行的理由？**

- **① 保留这两条 ctrl**，并在原则里写明"这里是依赖，不是顺序"（今天的实现；`531477399` 之后的 HEAD 就是这个形状）；
- **② 另给一条依赖**：例如让 `gate` 同时依赖那次写产出的**版本**（单元）——但 gate 的类型是值的类型，把单元接进去会改变它的类型，**这是类型上的另一个问题**，需要一句裁定；
- **③ 位置不要"直接取走值"**：让位置改走一次 `:var/get`，于是读与写之间自然存在数据边——代价是这两处的"值不经过单元"这一性质没了（注释里写着它是刻意的：*one data edge, no read of the cell and no new anchor*）。

**我方默认建议 ①**：它是今天的实现、259 条全绿，且不改变任何值的类型；但它意味着"停造 ctrl"这条要收窄成"停造**替身** ctrl"。

## 五、今天还堵着谁

- **②a 停造 ctrl**：这一页是它唯一剩下的堵点——按字面停造，这一处就红 66 条；
- **作者 02:30 那句"做 ① 的块外那一半、停造 ctrl"**：块外那一半（`D:\Temp\edges-block-outside.patch`）**整份依赖这条 ctrl 被删**，所以它今天不能上。