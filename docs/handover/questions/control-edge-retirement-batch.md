# 删控制边整批的唯一卡点：`var` 读的定位与链位交接冲突

基线 `origin/develop` `20f623835`；`origin/fsh-dev` 此时是 `7c634301f`（`Merge remote-tracking branch 'origin/develop' into fsh-dev`），本页「我们已确认的事实」里引的答复出自其中的 `4e6f2262a`。本页的读数分三类来源，逐条标明：一是 `h-edges` 车道的实测（照录其整行读数，本页未复跑），二是另一条车道对 ctrl-only 读者的盘点（同样照录），三是本页在 ref 上现取的代码位置与计划条目。

## 我量到的

**(1) T1（链断裂）——`h-edges` 实测。** 局部最小命令 `81/76/5 → 81/81/0`，但那要**两笔一起**（内联链位＋`gates.cpp` 门消解）；**单内联是 `76/5`**。全量门 `6990 / 6949 / fail 36`（上一轮 `6949` 之前是 `6939/46`），即 **24 条转绿、15 条新增**。

**(2) T4 与 T1 同源——`h-edges` 实测。** `static_branches.simplify` 的 `replaceNode` 改法**零效果**，读数 `96/95/1` 逐字不变；真因是**体内链从不接调用者的链**：`std::opt::inline` 两次仍只有 1 个 tick 常量，第 3 轮规则循环展开后三条打印只有 control 边、全在出口主链之外。

**(3) T2 是整批的本体，不是可独立的一步——`h-edges` 实测。** `control_data.graph` 的元凶 `control g1n13->g1n15` 同时有 sync 边（双写自命中）；runtime 侧删不到（双写写在**内建端** `writeSyncStatement`）；内建端删一行 ⇒ `836 / 736 / fail 100`（`consecutive_sync_calls.*`、`written_variable.*`、`simplify_nested_function_effect_lost.*`、`broken_branch.*`），因为**只读 control 边的读者（调度器、autodiff pullback）尚未迁移**。

**(4) 撞车：链位交接让 `var_tensor.*` 12 条出语义错——`h-edges` 实测。** 期望 `2.0 | 10.0 10.0 | 20.0 20.0`、实测 `2.0 | 6.0 6.0 | 12.0 12.0`。归因已隔离到**"把调用的链位交给携带自链的体尾"**这一步（`bodyHasChain ? bodyTail : spliceChainAnchor(...)`），它改了 lambda 体内 `:var/get` 读的相对位置；**不在**头侧接链，**也不在**门消解。

**(5) 机制与代码位置——本页现取。**

- 机制原文在车道树 `handover/h242-edges-land`（`bc51e2e46`）`src/compile/gir/builder_helpers.h:578-582`：

```
578  /// A link of the chain between two statements of a `sync` block, written only if it says something
579  /// (`chainEdgeSaysSomething`). What is not here: the edge that orders a `var` write and the read of
580  /// it. The read has no data edge from the write yet, and an edge that only orders would leave it
581  /// with the value from before the write (Builder::orderAfterLastWriter, builder_expr.cpp) -- that
582  /// pair waits until the read is a data edge from the nearest write.
```

  即：只加顺序边的读会拿到写之前的值，所以要等"读＝来自最近一次写的数据边"。

- 内建端的双写是同一份文件 `:571-576`，`writeSyncStatement` 同时落 sync 与 ctrl 两条边，`:575` 就是 `linkNodes(LinkType::Ctrl, from, to);`——"删一行"删的是这一行：

```
571 inline void writeSyncStatement(node_handle_t from, node_handle_t to) {
572     markSyncStatement(from);
573     markSyncStatement(to);
574     linkNodes(LinkType::Sync, from, to);
575     linkNodes(LinkType::Ctrl, from, to);
576 }
```

- `var` 读的控制边在 `20f623835` 上是 `src/compile/gir/builder_expr.cpp:1475` 的 `GCNodeKind::Port` 分支，`:1477` 落 `LinkType::Ctrl`。

- **这批代码还没进 develop**：`git grep -n 'writeSyncStatement' origin/develop -- src include` → **0 行**；`git grep -n 'bodyHasChain' origin/develop -- src include` → **0 行**；`spliceChainAnchor` 只有 `src/runtime/draft_inline.cpp:1349`（定义）与 `:1874`（唯一调用）。

- 期望值原文：`test/plans/feat/linear/var_reference.plan.toml:129` → `assert(out === '2.0 | 10.0 10.0 | 20.0 20.0', \`unexpected output: ${out}\`)`；该计划里 `var_tensor.*` 共 45 条，撞到的是其中 12 条。T1 的另一半在 `src/passes/opt/generic/gates.cpp`；`static_branches.simplify` 在 `test/plans/feat/opt/generic.plan.toml:154`。

**(6) ctrl-only 读者的完整盘点。** 见下面「ctrl-only 读者的完整盘点」一节：另一条车道的实测，命中 191 条 / 54 文件，四类分法与本页核对见该节。

## 要裁的一句

**"`var` 读按最近一次写定位"与"链位交接"冲突时，以哪条为准？**

- **① 保留写入定位。** 代价：链位交接必须对含 `:var/*` 的体例外，T1/T4 只能部分转绿；同一份图里并存两套规则，`var_tensor.*` 的 12 条不动。
- **② 以链位交接为准。** 代价：`var_tensor.*` 那 12 条的期望按新语义改（`var_reference.plan.toml:129` 的 `2.0 | 10.0 10.0 | 20.0 20.0` → `2.0 | 6.0 6.0 | 12.0 12.0`），需要明确指出这是**语义变更**：tensor 变量经 `var` 形参传入、被 lambda 捕获之后不再是同一个对象。
- **③ 有第三种写法。** 代价：请给出，我们按它把两条路径的读数重跑。

三条都不由本页选；本页只把每条要动的东西和读数写清。

## ctrl-only 读者的完整盘点（另一条车道实测，本页照录读数）

命中 **191 条 / 54 文件**，分四类：**W 写点 70**（该退役）、**R-order 只读顺序、可改读 sync/order 54**、**R-ctrl 必须看控制语义 15**、**D 诊断/打印 51**。

**W 写点（70）按文件**：`builder_control.cpp` 14、`draft.cpp` 12、`draft_inline.cpp` 11、`rewrite.cpp` 6、`generic.cpp` 5、`gates.cpp` 4、`engine.cpp` 4、`builder_operators.cpp` 3、`builder_support.h` 3、`builder_expr.cpp` 2、`draft_clone.cpp` 2、`graph_encode.cpp` 2、`macro/rewrite.cpp` 2、**`builder_helpers.h` 1（就是那行 `linkNodes(Ctrl)` 双写）**、`algebra.cpp` 1。

**R-ctrl 必须看控制的 15 条**：`graph_runtime_support.cpp` 11（BRCH 选择器/环检测）、`taskflow.cpp` 3（`wantCtrl` 与 BRCH 前驱）、`branch_arms.cpp` 1、`graph_edges.cpp` 1、`exec_plan.cpp` 1 ⇒ **这 15 条是"迁移"里唯一绝对不能改成顺序边的**。

**D 诊断/打印 51**（含 `modules/graph/read.cpp` 6 条宾客 API `graph.ctrl_inputs`，作者侧、不归我们改）。

**100 条红的归因**：**行为红 38** ／ **图上断言红 36**（`control_data` 20 ＋ `unread_gate` 16，断言的是旧形状 ⇒ 退役后要重读，不是回归）／ **快照 10**（全在 `camel_passes`）／ **退出码 3**（`algebra_rules`、`graph_equal`、`tables`，与前两类有重叠）。

**最小迁移清单（6 项，只列不改）**：

1. `src/runtime/rules.cpp:101,133` 判纯/判有序补读 `syncInputsOf`；
2. `src/passes/opt/generic/generic.cpp:867` 门消解的等待枚举补 `syncInputsOf`；
3. `screen.cpp:228,272` 同；
4. `rewrite.cpp:226-232` 的 `inheritedWait` 补 sync；
5. `rewrite.cpp:250-252` 与 `gates.cpp:76-80` 把交给消费者的等待**按 kind 复制**、不写 ctrl（`gates.cpp` 的 `bypassValueGate` 属"该退役的写点"；`rewrite.cpp:250-252` 是写点但**不是病灶**，T4 实测单独改它零效果）；
6. `modules/autodiff/pullback.cpp:262,565,1502,2422,2494` 克隆/搬移 call 时除 `ctrlInputsOf` 再复制 `syncInputsOf`。

另注：`src/runtime/graph.cpp:520`、`writings.cpp:486` **已是双读**，不在清单内。

**行为红里已定位的 3 条**：`rules.cpp:101/:133`（只读 ctrl ⇒ sync-only 后被判无序、可被 CSE/搬动）、`generic.cpp:867`（枚举 `{norm,with,ctrl}` 漏 sync ⇒ 靠 sync 才有人等的门被判"没人等"而消掉、效应丢）、`screen.cpp:228/:272`（同）；其余**未定位**。

**本页在 `20f623835` 上核对过的几处**（其余行号未逐一核对，用之前按结构找）：

- `src/runtime/rules.cpp:101`、`:133` 逐字对上：`:101` 是 `return graph_.withInputsOf(ref).empty() && graph_.ctrlInputsOf(ref).empty();`；`:133` 是 `if (!graph_.withInputsOf(ref).empty() || !graph_.ctrlInputsOf(ref).empty()) {`，`:134` 紧接着 `fail("has an operator with a context argument or an order dependency");`。
- `src/passes/opt/generic/screen.cpp:228` 对上：`:227-229` 是 `normInputsOf(ref).size() == 1 && withInputsOf(ref).empty() && !ctrlInputsOf(ref).empty()`。
- `src/passes/opt/generic/generic.cpp:867` 在这个 tip 上是注释；所说的枚举在 `:870-871`：`{draft.normInputsOf(gate), draft.withInputsOf(gate), draft.ctrlInputsOf(gate)}`。
- 另三处在 `20f623835` 上不是所述代码（行号应是取自车道树）：`gates.cpp:76-80` 是 `edgeBudget -= cost; return true; }`（函数结尾）；`src/runtime/graph.cpp:520` 是一句 `std::format("Runtime graph '{}' has FUNC node {} without callee graph.", …)`；`src/runtime/writings.cpp:486` 是 `payload.size() >= sizeof(GCOperBody) && …`。
- `modules/graph/read.cpp` 里 `ctrl_inputs` 在 `20f623835` 上 0 命中；该文件的 ctrl 用法在 `:346`、`:354`、`:552-553`（`ctrlUsersOf`、`ctrlOutputsOf`、`ctrlInputsOf`）。

## 没量到的

- 未量到上一节未逐一核对的行号（本页只核了列出的那几处）；行号在车道树与 `20f623835` 之间有漂移，用之前按结构定位。
- 未量到 100 条红里其余行为红的逐条定位（上一节写明"未定位"）。
- 未量到 `20f623835` 上 T1/T2/T4 的闸门读数：本页没有构建、没有跑计划，上述数字全部照录车道读数。
- 未量到 `bodyHasChain ? bodyTail : spliceChainAnchor(...)` 那行补丁的确切行号：它不在任何本仓 ref 里（develop 与四个 edge 分支都 0 命中 `bodyHasChain`），只有车道工作树有。
- 未量到 `var_tensor.*` 那 12 条的具体名字：计划里共 45 条，本页没有逐条跑出哪 12 条红。

## 我们已确认的事实

- **更正一个数**：先前说"46 = A 类 36 + B 类 10"是**错的**。实测 `46 = 已知 21 + 链断裂 25`，那 25 里只有 8 条属 B 类 ⇒ **A 类实际是 21**。此前一直按 36 传，以此页为准。
- `var` 形参的既有答复（`4e6f2262a` 提交信息原文）：

> **`var` 形参的读取留在链上（`builder_expr.cpp:1475` 的 `GCNodeKind::Port` 分支）**：语义是有意的，写法是过渡的。
> - **语义**：一个 `var` 形参可能是调用方把同一个变量传了两次，被调函数分不清两个名字是不是同一个单元，所以这次读只能靠写下它的位置，和另一个名字的写排序。
> - **写法**：它现在接的是控制边。删控制边那一批里，它与其他语句的链位一样改成同步边，不留控制边，也不另设特例。

- 这条答复说这批里"`var` 读改成同步边、不另设特例"；而第 (4) 条量到的是：把链位交接真按同步边做下去，`var_tensor.*` 的语义就变了。两件事放在一起，就是上面那一句要问的。

本页只报测量与一句询问：不改代码、计划、断言或生成物。