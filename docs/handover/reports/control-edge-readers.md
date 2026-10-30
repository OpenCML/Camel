# 控制边的读侧：谁在读、读它做什么、① 里换成读什么

**重录规则（不再讨论）**：**改了什么就重录什么**——某一步里因为改法而变了的图，就在这一步里重录；不要攒到最后一步统一重录。

**三种边不通用（不再讨论）**：**同步边带链语义**（`writeSyncStatement` 会 `markSyncStatement` 两侧，`checkSyncEdges` 按链校验）；**`var` 的写只排序它的读，按设计原则 3.5 是顺序边**；**控制边只表达"门的等待"**。所以 `Ctrl → ...` 要**一处处判它到底是哪一种，不许整批替换**。反证：把 `coerceArmExit`、赋值门、`void` 门三处 `Ctrl → Sync` 试过，`test/plans/feat/linear` 整层 **141 条红**（`mutable_reference.*`、`var_*`、`order_edges_regions.*`）。

**判据的方向不通用（不再讨论）**：**"有 X 才允许"与"有 X 就拒绝"是两条判据，不是同一条的两种写法**。`gates.cpp:19` 的 `ctrlInputsOf(id).empty()` 是**使能条件**——"**有** ctrl 等待的门才可以被绕过（等待搬到消费者）"——把符号翻过来等于把优化整条掐掉，`control.topology` 立刻报 `ordinary forwarding gate survived`。**这条判据等 ② 停造 ctrl 之后要重新设计，不是改符号。**

**"断言与规则同一笔改"的验证方式**：`passes/test/control.cpp` 的 26 处最后**一行没改**就绿了——**说明"同一笔改"的正确结果是"改完发现断言本来就该不动"，而不是"先把断言改软再看"**。反过来，`gates.cpp` 那次误翻是被断言当场拦下的，不是被我看出来的。

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

**第二条前提也要修正（实测三轮才定下来）**：三处"孤立的 ctrl 等待"里**只有一处能改**，另两处两种改法都红。
- 全改 `Ctrl → Sync`：`test/plans/feat/linear` 整层 **141 条红**（`mutable_reference.*`、`var_*`、`order_edges_regions.*`）。
- 全改 `Ctrl → Order`（按 3.5 "只排序"的读法）：**81 条红**，还是 `var_*` 与 `mutable_reference.*`——**"顺序边只排序"不假，但"读要从写拿到版本"这件事顺序边不负责**，`std::opt::order` 会把这条顺序边当成可推导的冗余删掉。
- **只改 `coerceArmExit`（臂值被字面量顶替的那个门）→ `Sync`：整层绿（872 过、2 XFAIL），整闸也绿。** 它是 5.3 明写的形状（一个数据口 + 一个同步前驱）。
- **留下的两处**（`builder_operators.cpp:4403` 赋值门、`:4427` `void` 门）**不是"还没改"，是"没有答案"**：5.3 只说链上的门，没说"值在门后等一次写/一次调用"该由哪种边承担。**这是设计问题，要作者裁决**——在它落定之前，停造 ctrl 会把这两处的等待整个丢掉。

**① 落地状态（按实测改的，不是按计划）**：
- **已落地**：`graph_runtime_support.cpp:472` 先读同步前驱、ctrl 只作旧图退路（纯增量，没有同步前驱的图一字不变）。
- **已落地**：`control.cpp` 三处 GATE 规则改成"**有同步前驱就不需要 ctrl**"（`appendControlDependency` 的提前返回、`reduceControlDependencies` 的保留、无值种子时的兜底各一处）。`passes/test/control.cpp` 的 26 处**一行没改就跟着绿了**，因为它的用例正是"没有同步前驱的门仍要留一条 ctrl"——新规则在这条上与原规则同形。**唯一被重录的图**：`exemplars/05_effect_order/expected/simplified.agir.dot` 与它的 `run.log`，各删 5 行 `type="CTRL"`（都是"门 → 它的同步后继"那条与 SYNC 重复的边）。
- **没落地、也不该机械翻的**：`gates.cpp:19`。它的 `ctrlInputsOf(id).empty()` 是**使能条件不是守卫**——"有 ctrl 等待的门才可绕过（等待搬到消费者）"，翻成"有 ctrl 就不许绕"会把这个优化整条掐掉，`control.topology` 的 `ordinary forwarding gate survived` 当场报错。等 ② 停造 ctrl 之后，这条判据要**重新设计**，不是改个符号。
- **不用改**：`std::opt::order`（只读 order 与 sync）。