# 视图要不要直接答「算子的实参次序」（`kernelArgumentsOf`）
2026-11-23 ｜ 来自 `lane/view-surface`（`C-1123-59` 的 b 项）｜ 对象：`kernel/graph/graph_view.h`、`kernel/runtime/operator_instance.h`、`rewrite/fact.cpp#Source`、`rewrite/module.cpp#axisPortsOf`

## ① 事实：它是「图存的两个列表 + 算子声明里的一格」

`runtime::kernelArgumentsOf`（`operator_instance.h:28-40`）答的是 `KernelArguments{norm, with}`：节点自己的普通实参与 `with` 实参，再按算子记录里 `withAfterNorm`（`OperatorTraits`，`operator_table.h:239-243`）这一格，把 `with` 接到 `norm` 后面、`with` 清空。它存在的原因写在注释里：语言把 `with` 放在自己的边上，而 `reshape<shape>(x)` 的核收的是 `(x, shape)`——**这是关于核的实参次序的事实，不是语言的语法**。

于是这个答案完全由视图已有的三样拼出来：`operatorOf(ref)`（拿 URI 去查 `withAfterNorm`）、`inputs(ref, PortGroup::Normal)`、`inputs(ref, PortGroup::Context)`；只有一格要额外判断——**不是存储的 OPER 记录（CAST/COPY/FILL/ACCS）时不做这次拼接**，那正好是 a 项那条查询。也就是说 b 依赖 a，a 有了 b 就是一条写在头文件里的定义。

**谁要它**：今天 19 个调用点、11 个文件。

| 文件 | 处数 |
| --- | --- |
| `kernel/runtime/rules.cpp` | 3 |
| `stdlib/rewrite/fact.cpp` | 3 |
| `stdlib/tiles/src/graph_regions.cpp` | 3 |
| `stdlib/rewrite/module.cpp` | 2 |
| `stdlib/tiles/src/compiler.cpp` | 2 |
| `kernel/runtime/definitions.cpp` | 1 |
| `kernel/runtime/writings_diag.cpp` | 1 |
| `kernel/runtime/writings_match.cpp` | 1 |
| `stdlib/onnx/exporter.cpp` | 1 |
| `stdlib/std/cxx/opt/generic/generic.cpp` | 1 |
| `stdlib/tiles/src/region_extract.cpp` | 1 |

其中三个是这条车道点名的：`fact.cpp#compilePattern`（`Oper` 那一支，`605-620`）、`fact.cpp#Source::kernelInputs`（`194-198`）、`module.cpp#axisPortsOf`（`3869`）。

## ② 现在怎么绕过去

`kernelArgumentsOf` 只认表示：两个重载，一个收 `const GCGraph &`，一个收 `const GraphDraft &`。所以要问它，就必须**手里有表示**。最贵的一处是 `rewrite/fact.cpp#Source`（`170-227`）：

```cpp
struct Source {
    const camel::runtime::GraphTransaction *transaction = nullptr;
    const Program *program  = nullptr;
    const GCGraph *graph    = nullptr;
    ...
    std::vector<gc_node_ref_t> kernelInputs(gc_node_ref_t ref) const {
        return transaction ? camel::runtime::kernelArgumentsOf(transaction->graph(), ref).norm
                           : camel::runtime::kernelArgumentsOf(*graph, ref).norm;
    }
```

`Source` 的每一个查询都是这个三目（`kind`、`typeOf`、`operOf`、`kernelInputs`、`normInputs`、`constant`），因为它要同时服务草稿与冻结图。`kernelArgumentsOf` 是其中一条：**视图已经是「一份只读面」，`Source` 却仍是「两份表示 + 一个三目」**。

其他 10 个文件是同一形状的轻量版：函数签名为了调它而收 `const GCGraph &`（`module.cpp#axisPortsOf`、`tiles/src/graph_regions.cpp`、`onnx/exporter.cpp` 都是），于是这些函数整体留在表示上，不能迁到视图。

## ③ 加上之后哪些调用点简化，不加的代价

会简化：

- `fact.cpp#Source::kernelInputs` 消失，`Source` 少一个三目；配合 a 项与 c 项，`Source` 才可能只剩一个 `GraphView`（这正是 `C-1123-55` 的目标）；
- `module.cpp#axisPortsOf`、`tiles/src/graph_regions.cpp`、`tiles/src/compiler.cpp` 里那些「为了取 `norm` 而收整张图」的函数可以改收视图；
- `rules.cpp`（3 处）、`writings_diag.cpp`、`writings_match.cpp`、`generic.cpp` 同样；
- 视图读者不必再为了「一个算子的核按什么次序收实参」把表示拖进来。

不加的代价：

- 它是一条**能存的东西的读法**，却只能从表示上取，于是 11 个文件里的 19 处都在签名上钉住了 `GCGraph`/`GraphDraft`；
- 阶段 3a 迁一个文件就要为它留一条后门；
- 同一个拼接（`with` 接到 `norm` 后面、`with` 清空）在两个重载里各写一遍（`operator_instance.cpp:78-100`），第三个读者照抄时很容易漏掉「只对存储的 OPER 做」。

## ④ 我的默认：加，写在头文件里

在 `GraphView` 上加

```cpp
/// The node's arguments as its kernel reads them: the two port groups, with the `with` ones
/// after the ordinary ones when the operator declares `withAfterNorm` (OperatorTraits). Empty
/// `with` then. A node that is not a stored OPER record answers its own two groups unchanged.
KernelArguments kernelArgumentsOf(NodeRef ref) const;
```

默认实现写成头文件里的非虚函数，用 `isOperRecord`（a 项）+ `operatorOf` + `inputs(Normal)`/`inputs(Context)` + `core::joinKernelArguments` 拼；`KernelArguments` 从 `runtime/operator_instance.h` 提到能被视图头文件看见的地方（或就地定义成视图自己的 `KernelArguments`，两处同名同义）。这样两个适配器都不用改，`operator_instance.h` 的两个重载保留给已经在表示上的后端。

若裁为「不加」，请指定 `Source` 这类「草稿/冻结图一个结构」该怎么办——那是这条车道下一个绕不开的卡点。