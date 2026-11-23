# 「不是常量」与「越界」现在是同一个答案
2026-11-23 ｜ 来自 `lane/view-surface`（`C-1123-59` 的 d 项）｜ 对象：`kernel/graph/graph_view.h#constantOf`、`rewrite/fact.cpp#compilePattern`

## ① 事实：是，同一个返回值 `std::nullopt`

两个适配器都这样写（`gc_graph_view.cpp#FrozenGraphAdapter::constantOf`、`draft_view.cpp#DraftGraphAdapter::constantOf`，两份逐行同形）：

```cpp
if (node == nullptr || node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
    return std::nullopt;                    // ① 不是常量（含：不是节点、不是 DATA、有运行期槽）
}
const auto index = static_cast<std::size_t>(-node->dataIndex);
if (index >= graph_.staticSlots().size()) {
    return std::nullopt;                    // ② 索引落在静态区之外 —— 与 ① 同一个答案
}
return slots[index];
```

于是 `constantOf` 的 `nullopt` 覆盖**四种**情形：不是节点、不是 DATA、DATA 但落在运行期槽（`dataIndex >= 0`）、DATA 的静态索引越界。前三种是常态，第四种是「这张图坏了」。

**判据要的那个区别现在有没有别的出口？** 没有值层面的出口。唯一想区分它的读者是 `rewrite/fact.cpp#compilePattern` 的 Data 那一支（`586-604`）：

```cpp
if (node->dataIndex >= 0) {
    err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, "fact.pattern",
        "the pattern reads a value that is not a constant");
}
const auto index = static_cast<size_t>(-node->dataIndex);
if (index >= graph.staticSlots().size()) {
    err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, "fact.pattern",
        "the pattern names a constant the graph does not have");
}
```

两条**同一个诊断码**（`RuntimeDiag::InvalidArgument`）、同一个 `"fact.pattern"` 作用域，只在消息文本上不同。也就是说：它想要的那个区别，连它自己也只是「两种措辞」，不是两种可判的值——而这正是它绕开视图、直接读 `node->dataIndex` 与 `graph.staticSlots()` 的理由。

## ② 现在怎么绕过去

同一段三步读（判 `dataIndex >= 0`、取 `-dataIndex`、查 `staticSlots()`）在树里有 **5 份**，其中只有两份在视图上：

| 位置 | 形状 |
| --- | --- |
| `gc_graph_view.cpp#FrozenGraphAdapter::constantOf` | `optional<slot_t>`，两种情形合一 |
| `draft_view.cpp#DraftGraphAdapter::constantOf` | 同上 |
| `rewrite/fact.cpp#Source::constant`（`209-226`） | `bool` + 出参，两种情形合一 |
| `kernel/runtime/writings_match.cpp#DraftWritings::constantOf`（`122-140`） | `optional<pair<slot_t,Type*>>`，合一，还多一个 `takesPart` |
| `stdlib/transforms/node_operator.h#constantOf`（`336-346`） | `const slot_t *`，合一 |
| `stdlib/onnx/passes/export.cpp#constantOf`（`89`） | 同上 |

另有约 **60 处**只写第一句 `dataIndex >= 0` 的「是不是常量」判断（`stdlib/std/cml/cml.cpp` 一个文件就 12 处）。

## ③ 加上之后哪些调用点简化，不加的代价

会简化：

- `fact.cpp#compilePattern` 的两条拒绝可以改问视图，且两种情形各有各的答案（不是措辞）；
- `fact.cpp#Source::constant`、`writings_match.cpp#DraftWritings::constantOf`、`node_operator.h#constantOf`、`onnx/passes/export.cpp#constantOf` 四份合一，`Source` 又少一个三目；
- 那 60 处「是不是常量」可以问同一句话。

不加的代价：

- 读者拿不到「索引本身」，就分不出「这张图坏了」和「这不是常量」——一个坏图会安静地走非常量那条路；
- 5 份实现里每一份都是「静态区的布局」（`-dataIndex`、`staticSlots`、`staticSlotTypes`）的一份副本，改布局要改五处；
- 阶段 3a 迁这几个文件时没有替代品，只能继续读表示。

## ④ 一个必须先说的读数：`dataIndex` 的正负是**两件事**

`graph_view.h` 现在写着「`dataIndexOf` 哪个槽……是执行计划的（第 13 节），不是图的内容」。这句话只对**正的一半**成立：

- `dataIndex > 0`：运行期架子里的位置，是计划分的，视图确实不该答；
- `dataIndex < 0`：**静态区的下标** `-dataIndex`，是图自己存的内容——`constantOf` 就在读它，`graph_view.h` 同一段里也写着「a constant is the node's kind and its inputs, which is what `constantOf` answers」。

所以这一格不是「该不该答索引」，而是**这一个人的字段装了两件事，文档按一件事读**。§① 的「两种情形一个答案」是同一形状的第二处。

关于越界那一半再补一句：冻结图在编码时就拒了 `dataIndex < 0 && -dataIndex >= staticSlotCount`（`graph.cpp:761-770`，「static slot outside static-data size」）。所以**越界在冻结图上不可达**，它能出现的地方是草稿（草稿没有这条编码期检查）与手工造的图。既然如此，今天 `constantOf` 把越界读成「不是常量」，代价不是「常见路径答错」，而是「一张违反不变量的草稿被读成一张正常的非常量图」——正是「两种情况看起来一样」最贵的那一种。

## ⑤ 我的默认：加两条只答一格的查询，`constantOf` 不动

```cpp
/// The static-area index a node's constant sits at, `std::nullopt` when the node has none:
/// not a node, not a DATA node, or a DATA node on a runtime slot (`dataIndex >= 0`, which is
/// the execution plan's, section 13). The index is the graph's own content; the value is what
/// `constantOf` answers.
virtual std::optional<std::uint32_t> staticIndexOf(NodeRef ref) const = 0;

/// How many slots the graph's static area has. A `staticIndexOf` at or past this is an index
/// the graph does not have -- the encoder refuses one on a frozen graph (graph.cpp), so a
/// reader that sees one is looking at a draft that broke the invariant.
virtual std::uint32_t staticSlotCount() const = 0;
```

两个理由：

1. **不动 `constantOf` 的签名**，`loop.cpp:189`、`consistency.cpp:205-206` 这些既有读者一行不改；`constantOf` 之后可以写头文件里的一条定义（`staticIndexOf` 有值且在 `staticSlotCount()` 之内时才取值），把两处实现收成一处。
2. 两条各答一格，`staticIndexOf` 的 `nullopt` 只剩「没有静态位置」一个意思，越界由「索引 ≥ 槽数」表达——不再是同一个 `nullopt`。

若裁为「不加」，请顺便定 §④ 的那句文档怎么写：`dataIndexOf` 的正负两半要不要分开写，以及 `constantOf` 的注释要不要明说它把越界读成 `nullopt`。