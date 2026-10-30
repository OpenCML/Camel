# 控制边的读侧：谁在读、读它做什么、① 里换成读什么

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

**一条前提要修正（我先按错的做过一次，用真闸门验回来）**：`writeSyncStatement` 是**成对**写 `SYNC`+`CTRL` 的，所以两条 `known_fail` 的图里**必然同时有 sync 和 ctrl**，而它们的断言是"一条 CTRL 都没有"。**这两条只可能在 ② 变绿，① 变不绿**——我把 `coerceArmExit` 的 ctrl 改成 sync 并摘掉标记试过，闸门直接 `[FAIL] edges.gate_has_no_control_input ... N04 has 1 CTRL input(s) from N03`，已回退。**所以摘标记只能跟 ② 同一笔，① 一笔也不许摘。**

**① 这一笔的改动清单**：`control.cpp` 三处 GATE 留 ctrl 的规则、`gates.cpp:19` 补 sync 条件、`graph_runtime_support.cpp:472` 读同步前驱、`passes/test/control.cpp` 跟着改；**`std::opt::order` 不用改**（它已经只读 order 与 sync）。