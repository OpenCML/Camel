# 控制边的读侧：谁在读、读它做什么、① 里换成读什么

**重录规则（不再讨论）**：**改了什么就重录什么**——某一步里因为改法而变了的图，就在这一步里重录；不要攒到最后一步统一重录。

**一句话结论**：写侧 18 处 `linkNodes(LinkType::Ctrl` + 24 处 `setCtrlInputs`；读侧 **40 个文件已经是"`ctrl` 或 `sync` 取其一"**（`writeSyncStatement` 成对写，注释原话 "nothing is judged by it"）；**真正只认 `ctrl` 的判据只剩 6 处**，这就是第 ① 步的全部范围。

| 角色 | 在哪 | 读它做什么 | ① 换成读什么 |
|---|---|---|---|
| 造边 | `builder_control.cpp` 9 处、`builder_expr.cpp` 5 处、`builder_operators.cpp` 3 处、`builder_helpers.h:686`（`writeSyncStatement` 成对写）、`control.cpp:120`（`appendControlDependency`） | 生成 `CTRL` 边 | **不动**（② 才停造） |
| **只认 `ctrl` 的判据** | `runtime/control.cpp:104`、`:148`、`:274-283`（`std::opt::ctrl`：一个 GATE **必须**留一条 ctrl） | 决定约简时能删哪些等待 | 改读**同步前驱**：GATE 的等待在 sync 上，不留 ctrl |
| 同上 | `passes/opt/generic/gates.cpp:19`（有 ctrl 的 GATE 不可绕过） | 值门能否被它的输入替换 | 条件补上 `syncInputsOf(id).empty()`（第 26 行本来已单独判 sync，两处对齐） |
| 同上 | `execute/graph_runtime_support.cpp:472`（GATE 没有数据入边时沿 ctrl 找尾值） | 求图尾值时穿过多余的 GATE | 改读**同步前驱**，ctrl 只作旧图的退路 |
| 二者皆可 | `opt/generic/generic.cpp:288`（`waits = ctrl\|\|sync`）、`modules/autodiff/pullback.cpp:323/609`（两者都遍历）、`control.cpp:301-313`（`std::opt::order` 的 `Pass` 读法只走 order+sync，**data/control 早已被排除在证明之外**） | 判"这个节点等什么" | **不动**；② 之后删掉 `ctrl` 那一半 |
| 转发/搬运 | `runtime/draft.cpp`(45)、`draft_inline.cpp`(27)、`draft_clone.cpp`(10)、`writings.cpp`(10)、`generic/rewrite.cpp`(9)，以及 `autodiff` 其余 | 改写、克隆、内联时把等待原样搬过去 | **不动**；② 随列删除，不需要语义判断 |
| 表示/输出 | `trans/dot`(3)、`trans/json`(4)、`trans/cml`(7)、`modules/onnx`(2)、`modules/tiles`(9)、`trans/gir_metadata` | 打印 `type="CTRL"`、编码 JSON/DOT | **不动**；② 之后图里没有这种边，输出自然消失 |
| 测试专用 | `passes/test/control.cpp`(26) | `std::opt::ctrl` 自己的用例 | 跟 ① 的改动一起改 |

**一条前提要修正（我先按错的做过一次，用真闸门验回来）**：`writeSyncStatement` 是**成对**写 `SYNC`+`CTRL` 的，所以两条 `known_fail` 的图里**必然同时有 sync 和 ctrl**，而它们的断言是"一条 CTRL 都没有"。**这两条只可能在 ② 变绿，① 变不绿**——我把 `coerceArmExit` 的 ctrl 改成 sync 并摘掉标记试过，闸门直接报：

```
[FAIL] edges.gate_has_no_control_input ... N04 has 1 CTRL input(s) from N03
```

那处 CTRL 来自 `writeSyncStatement` 的成对写，不是被改的那一处——**成对写让任何单点修改都不会立刻见效**，这就是"不能先摘标记"的证据。已回退。

**第二条前提也要修正**：三处"孤立的 ctrl 等待"（`coerceArmExit`、赋值门、`void` 门）**不能改成 sync**。试过：`test/plans/feat/linear` 整层 **141 条红**（`mutable_reference.*`、`var_*`、`order_edges_regions.*`）。原因是 sync 边带链的语义（`writeSyncStatement` 会 `markSyncStatement` 两侧，`checkSyncEdges` 按链校验），而"`var` 的写排序它的读"按设计原则 3.5 是**只排序**的边——它该是 **order 边**，不是同步边。所以这三处等待属于 ②（造边那一步里把 `Ctrl` 换成 `Order`），不属于 ①。

**① 这一笔的改动清单（已落地）**：`control.cpp` 三处 GATE 留 ctrl 的规则改成"有同步前驱就不需要 ctrl"、`gates.cpp:19` 改为"有 ctrl 或有 sync 都不可绕过"、`graph_runtime_support.cpp:472` 先读同步前驱、ctrl 只作旧图退路；**`std::opt::order` 不用改**（它已经只读 order 与 sync）；`passes/test/control.cpp` 在第二笔跟着改。