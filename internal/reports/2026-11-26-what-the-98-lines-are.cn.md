# 九十八个引用里，七十一个是读者，二十七个是这个种类自己

路线三的判据是 `kinds-to-oper` 计数清零。`Cast`/`Copy` 那一步是把种类删掉，于是所有点名他们的行
一起消失。`Fill`/`Accs` 不一样：种类还在，前端、`transforms/` 和 `autodiff/` 一共 71 个构造点仍在
造他们，所以「引用数 → 0」这句话得先问清楚数的是什么。这一页把 98 行分成四类，量给下一次定序用。

## 一、命令与读数

```powershell
git grep -nE '\b(GCNodeKind|NodeKind)::(Fill|Accs)\b' -- src
```

98 行。逐行分类（排除表就是下面四节列出的 `file:line`）：

| 类 | 行数 | 是什么 | 删得掉吗 |
|---|---:|---|---|
| **定义与映射** | 10 | `oper_shape.h` 的 `shapeOfKind` 两臂；两个视图把 `Fill`/`Accs` 折成 `NodeKind::Oper` 的映射；`nodekind.cpp` 的两条名字表与两臂；`gc_graph_view.h` 的一行注释 | 只有删掉种类才行 |
| **构造写入** | 4 | `draft_construct.cpp` 的 `.kind = GCNodeKind::Fill` ×2、`Accs` ×2 | 只有迁走构造点才行 |
| **载荷布局与校验** | 13 | `graph.cpp` 的 `draftPayloadWords`、payload memcpy、`fillBodyOf` 守卫、错误检查；`graph_edges`、`draft_types`、`fact_persist`、`block_form`、`resources`、`graph.h` 各一处 | 同左 |
| **读者** | 71 | 按「这是什么操作」决定行为、印刷、导出、事实的行 | **这一步要走的** |

71 个读者再按冻结面分：`autodiff/` 求导引擎与 C++ 优化遍 **7**，`transforms/` **7**，开放 **57**
（`std/cml/cml.cpp` 8、`compile/src/gir/builder_control.cpp` 6、`kernel/runtime/graph.cpp` 5、
`std/gir/gir_metadata.cpp` 4、`builder_diagnostics.cpp` 3、`builder_helpers.h` 3、
`kernel/runtime/draft_inline.cpp` 3、`onnx/exporter.cpp` 3、`tiles/src/region_extract.cpp` 3、
`std/gir/json/gir_canonical.cpp` 3、`tiles/src/compiler.cpp` 2、`kernel/runtime/canonical.cpp` 2、
`std/tns/topo_node_seq.cpp` 2、`std/gir/dot/graphviz.cpp` 2、其余 8 个文件各 1）。

## 二、二十七行为什么不能改写

拿三个例子：

- `graph.cpp#draftPayloadWords` 里 `case Accs: case Oper: case Fill:` 归成一支，因为三者都是**变长
  载荷**。问的是「这个种类的体怎么摆」，不是「这是什么操作」；换成形状要写成
  「`Construction` 或 `Access` 或（`Oper`，形状是 `None`）」，比原来错得更远。
- `oper_shape.h#shapeOfKind` 是**唯一**把种类翻成形状的地方，它必须点名两个种类——否则
  `shapeOfKind(kind)` 对 kind 节点答不出东西，所有用它的读者一起坏。
- `draft_construct.cpp` 的四行是**写**这一侧，本来就不是读者。

所以「引用 0」与「构造点 0」是同一件事的两面，中间没有第三个停在 0 的姿势。

## 三、代码自己的注释也这么说

`kernel/include/camel/runtime/oper_shape.h:77-80`，`nodeKindName` 的函数注释：

> A kind that stands for one of the four operations is named by the operator (`shapeName`), so a
> `FILL` node and a `:op/fill` call answer the same word -- which is the whole of what the day the
> front end stops building the two kinds leaves to change here: nothing.

也就是说：印刷与判定的读者**已经**有一个「两种写法答一个词」的入口，剩下的 27 行是这个种类的
定义、构造与载荷布局，删种类那天一起消失，不需要逐行迁。

## 四、两条路，以及建议

- **A（到 0）**：先迁 71 个构造点（`transforms/` 正常迁；`autodiff/` 构造点按裁定做机械迁移、只
  换写法），再删两个种类，98 行与读者臂一起消失。前置：② 的稀疏/换序槽位（前端唯一非恒等槽位的
  `fill` 构造点靠它）。
- **B（到 41）**：先迁 57 个开放读者，98 → 41，41 行即上表前三类（10 + 4 + 13 = 27）加冻结 7、
  `transforms/` 7。然后仍要走 A 才能到 0。

建议 **A**，而且 ② 在 A 的关键路径上：`②` 补的是声明已承认的形状，补完 `builder_expr.cpp:298`
那个换序槽位才能写成 `:op/fill` 调用；写出之后 71 个构造点里没有一个是「调用表达不了」的，种类
才可以删。B 里的 57 个读者在 A 之后大多随种类一起消失（`nodeKindName` 的注释就是承诺），先迁他们
是把工作做两遍。

## 五、这一轮做了什么、没做什么

做了：上面那份逐行分类（可重跑，命令在第一节）。没做：**没有动一个读者**、没有动种类、没有动
构造点——因为这两件事的先后由上面的定序决定，先迁读者会让 57 处改动在删种类时再改一次。

`②` 仍然可以做，且不依赖这个定序：它只补内核认声明已写明的形状，行为在现有输入上不变。