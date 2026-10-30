# 一问：值在门后等一次写、或等一次调用——这件事该由哪种边承担？

> 状态：等作者裁决。裁决之前，控制边不能停造（理由见末节"边界"）。
> 相关：`docs/design/effects/edges.cn.md` 1（边只有 data/sync/order）、3.5（写与读的顺序）、5.3（GATE = 一个数据口 + 一个同步前驱）；`docs/design-principles.cn.md` 3.1。
> 已落地的两步：`d52823be4`（读侧不再要求门留 ctrl）、`77fd9e53f`（臂出口的门改等链）。车道 `lane-edges`。

## 结论先行（裁决真正要回答的那句话）

**Order 不让一次写成为可观察的效果；那条等待今天是"这次写还活着"的唯一凭据，换成 order 边之后写被当死代码删掉。裁决真正要回答的是"什么边让效果活着"，不是"什么边排序"——排序早就够了。**

证据（`Ctrl → Order` 之后 `std::gir` 的前后对比，`var.cml` / `var_params.cml` / `var_tensor_reference.cml` 三张图同一个形状）：

```
-            N04 [label=":op/assn_l", type="OPER", ... value_type="var int"];
-            N05 [label="GATE", type="GATE", value_type="int"];
-            N04 -> N05 [label="0|0", type="CTRL"];
-            N05 -> O02 [label="0", type="EXIT"];
+            N02 -> O02 [label="0", type="EXIT"];
```

**门没有被绕过——门和写一起没了**（gates 数 2→1、6→4、4→3），图尾直接取表达式的值。今天的写节点**没有数据入边也没有数据出边**（`N04` 的边只有 `CTRL` 与同对的 `SYNC`），所以那条等待一换种类，它就什么都不剩。

**本页只回答“今天靠什么活着”，不回答“该由什么承担”——后者是作者要裁的。**
（本页也覆盖调用、故障、副作用三类，不止“等一次写”；文件名沿用最先发现的那个形状。）

**算账（给作者）**：**硬缺口 2 处、软缺口 6 处；选 (a) 要改 6 处 pass 判据**（其中 3 处在“门只有 ctrl”的形状上做不到），**选 (b) 只改那 2 处的写法**（外加让可达性走一遍 order 边）。

## 一句话

`coerceArmExit` 那个门等的是"**一条链走完**"，5.3 给了它答案（同步前驱），改成 sync 就绿。但**赋值门**（`builder_operators.cpp:4403`）和 **`void` 门**（`:4427`）等的是"**一次写 / 一次调用**"，这两种**手册里没有**：5.3 只说链上的门。今天是控制边在承担它们，而控制边按 1 要消失。

## 今天三种边各能承担什么（实测）

| 边 | 能承担 | 不能承担 |
|---|---|---|
| **Sync** | 链：`writeSyncStatement` 会 `markSyncStatement` 两侧，`checkSyncEdges` 按链校验（同一块、相邻） | **不是链的连接**——写→值、"调用已返回"这类，硬连会把不属于一条链的两个节点标成同块 |
| **Order** | 只排序：谁先谁后，不传值 | **效果的存在**——见"裁决后的第一步"：换成 order 边之后，写节点被当死代码删掉 |
| **Ctrl** | 今天唯一能表达"这个节点必须等那次写/那次调用"的 | 按 `edges.cn.md` 1，**这个种类整体要消失** |

## 三种改法的实测数（都在 `lane-edges` 上跑过，同一个闸门）

| 改法 | 结果 |
|---|---|
| 三处全 `Ctrl → Sync` | `test/plans/feat/linear` 整层 **141 红**（`mutable_reference.*`、`var_*`、`order_edges_regions.*`） |
| 三处全 `Ctrl → Order` | **81 红**，还是 `var_*` 与 `mutable_reference.*` |
| **只 `coerceArmExit` → `Sync`** | **整层绿（872 过、2 XFAIL），整闸绿** |

**三组数为什么不同**：臂出口的门是链的延长（链走到值，值被字面量顶替，门接着等链），所以同步前驱天然合适；赋值门/`void` 门等的是**一次效果**——写已经发生、调用已经返回——它要的不是"值从哪来"，是"**那件事已经做完**"，而"做完"既不是链的一环（Sync 不该连）、也不是从值能推出来的（Order 会被约简删掉）。

**一条待查的线索**（不是结论）：Order 那 81 条红里，有些可能是"门被 `bypassValueGate` 绕过时，挂在它上面的 order 边一起消失"，而不是 order 边本身不承担版本。裁决若倾向 (a)，这条要先验一遍。

## 手册的缺口

`edges.cn.md` 1 说边只有 data / sync / order，5.3 说门 = 一个数据口 + 一个同步前驱。**没有一节说"值在门后等一次写/一次调用"由什么承担。** 所以这是**手册的缺口，不是实现的缺口**——实现今天用 ctrl 顶着，顶得住，但顶的是要删的那一类。

## 两条出路

**(a) 给"等一次写/调用"一种明确的边类**（新种类，或给 sync 加一种"非链"形态）。
代价：边类从 3 变 4；两个导出格式（`docs/technical/gir-interchange.md` 的 version 1 与 version 2）都要加一类；**26 张存档图里那 143 条 `CTRL` 边全部重录**；53 个读边的文件里"ctrl 或 sync 取其一"的地方要各自判一遍（今晚已经证明这类判不能整批替换）。

**(b) 保留 ctrl 这一种类，把它限定为"等一次写/等一次调用"**。
代价：`design-principles.cn.md` 3.1 与 `edges.cn.md` 1 各改一句（"边只有 data/sync/order"→"**三种值/顺序边，外加一种效果等待边**"）；`test/plans/feat/linear/effects_model_edges.plan.toml` 里那条 `no_ctrl_edge_kind` 的用例要**删**（它的前提就是"这个种类不存在"），`gate_has_no_control_input` 保留并变绿。

## 我的建议：**(b)**，理由三条

1. **代价小一个数量级**：一句手册 vs 一种边类 + 两个导出格式 + 143 条存档边 + 53 个读点逐点重判。
2. **这个区分在代码里已经存在，且到处都是**：`src/runtime/control.cpp` 的规则、门的绕过判据（`gates.cpp:19` 的**使能**条件）、`waitUsersOf`、`hasControlDependency`——所有实现都把"效果等待"当成一种与链不同的东西在处理；(a) 只是给它换个名字加个编号，却要求全体读者跟着搬家。
3. **今晚已经付过一次学费**：把 ctrl 换 sync 或换 order 各红一次（141 / 81），说明这个区分**不是命名问题，是语义真的不同**——那就该在模型里给它一个位置，而不是把它塞进已有的两种里。

**若作者倾向 (a)**，我建议的最便宜形态是：**不新增边类，而是让"效果等待"只出现在门的一个侧口上**（门有两个入边位置：值、以及"等谁做完"），导出格式里这个位置本来就存在（门今天就有 ctrl 入边，只是类型名要换）；这样两个 version 各改一行而非各加一类。

## ②a 的可逆演练：停造 ctrl 会红多少（已跑，工作树已复原、无提交）

把 **17 处 `linkNodes(LinkType::Ctrl`**（`builder_control.cpp` 8、`builder_expr.cpp` 5、`builder_operators.cpp` 3、`builder_helpers.h` 1）全注释掉，编一次，跑闸门：

| 项 | 结果 |
|---|---|
| 整闸 | **fail 584 / total 7940**（pass 7349、skip 7） |
| `control.topology` | **PASS**（自检自己造边，不查 ctrl 存不存在） |
| `control.test_pass_runs_when_named` | **PASS** |
| `control.pipeline_contract` | **FAIL**（Python 侧契约用例） |
| 存档图 8 个 | **4 个变**：`01_scalar_boundary` −4、`02_branch_shape_contract` −5、`08_structured_control_flow` −9、`05_effect_order` **+10 / −15** |

**那 10 行新增是什么**：**全是索引重编号，不是新边**——少一条 ctrl 后，同一节点剩下的 ctrl 边 `label="i|j"` 前移（`N0D -> N11` 从 `1|0` 变 `0|0`）。所以它在这里不是语义变化；但 ②a 重录时仍要逐条确认没有第二种来源。

**584 条红的形状**：`gir.*_snapshot`、`boundaries.*`、`consecutive_sync_calls.*`(9)、`simplify_arm_write_lost.*`(8)、`unread_gate.*`、`dropped_effects`、`graph.null_then*`——**全是"效果/写/门"这一族**，与上面收窄后的结论同源：**停造 ctrl 之后，效果没有边来宣告它活着**。这就是 ②a 必须等裁决、不能"红了自己修"的原因。

**为什么 584 条红里没有一条来自那几个最像该红的地方**：`control.topology` 与 `control.test_pass_runs_when_named` 在停造 ctrl 后**仍然 PASS**——**说明这些自检自己造边、不查这一种类是否存在；所以它们不能当作“停造安全”的证据**。

## 一张表：哪一类效果要靠什么活着（给作者一句话能答）

行按实现里已有的分类（`include/camel/core/operator_semantics.h` 的 `Effect {Undeclared, Pure, StateOnly, Effectful}`、`FaultSpec`、`OperatorTraits::mayFault`），不自己发明。**"靠什么活着"= 这个节点在图上凭什么被保住**（有数据使用者，或有某条边把它连到出口）。

| 需要"活着"的效果 | 今天靠什么活着 | 停造 ctrl 之后还剩什么 | 缺口？ |
|---|---|---|---|
| **一次写 `var`**（`StateOnly`，`:op/assn_l`） | **只有 ctrl**（同一对门；`writeSyncStatement` 的那条还陪一条 SYNC） | **SYNC**（成对写那条保留，已实测） | 链外（`builder_operators.cpp:4403`）的写：**只有那一条 ctrl，没有别的边**（代码读数，未用探针复验）← 硬缺口 |
| **一次调用，`void` 或结果没人用** | **只有 ctrl**（`:4427` 的 `void` 门） | 作链上的语句时还有 **SYNC**（已实测）；`:4427` 的门那条**只有那一条 ctrl**（代码读数）← 硬缺口 |
| **一次调用，位置要它的返回值** | 数据边（返回值有人读）+ ctrl（等调用返回） | 数据边在；**"已返回"那条凭据归零**（未单独验） | 悬空 ← 缺口 |
| **可能出故障的算子**（`mayFault`，如 `:op/set_index_in_place`） | ctrl（sync 块里同时有 SYNC） | **ORDER + SYNC**（已实测，节点都还在） | **要 pass 侧确认**：谁把 ORDER 当成"效果活着"的凭据 ← 缺口 |
| **`Effectful` 算子**（`println`、`io/`、profiler） | 返回值一般被读（数据边）；值没人用时走 `:4427` 的门 | 数据边在，链上那条 **SYNC** 也在（已实测）；只有 `:4427` 的门那条**什么都不剩** | ← 缺口 |
| **`Undeclared`**（没人声明） | 按 `operator_semantics.h:431` 当作 `Effectful` 处理，同上 | 同上 | 同上 |
| **`Pure`** | 不需要凭据（没有效果），删不删由使用者决定 | 不变 | 不是缺口 |

**五行现在是实测的，读数如下（`std::gir`，`probe2.cml` / `probe5.cml`）**——**"原来是这两条，现在剩什么"**，不写笼统的"什么都不剩"：

| 效果 | 今天（实测） | 停造 ctrl 之后（实测） |
|---|---|---|
| 一次写 `var`（`x = 1` 之后没人读） | `N04 -> N06 [CTRL]` **+ `[SYNC]`** | **只剩 `[SYNC]`**，写节点 `N04` 还在 |
| 一次 `void` 调用（`voidcall()` 作语句） | `N08 -> N0A [CTRL]` **+ `[SYNC]`** | **只剩 `[SYNC]`**，门 `N0A` 还在 |
| 可能出故障的算子（`xs[i] = 1` 里的 `:op/set_index_in_place`） | `N07 -> N09 [CTRL]` **+ `[ORDER]`**；写 `N09 -> N0B [CTRL]` **+ `[SYNC]`** | **剩 `[ORDER]` 与 `[SYNC]`**，两个节点都在 |
| `Effectful` 且返回值没人读（`println(1)` 作语句） | `N0D -> N0F [CTRL]` **+ `[SYNC]`** | **只剩 `[SYNC]`** |
| `Undeclared`（当 `Effectful` 处理） | 与上一行同形（`operator_semantics.h:431`） | 与上一行同形 |

**所以要把我上一版的说法改掉**：**builder 出的图里，这些节点并没有"什么都不剩"**——它们都还有 `SYNC` 或 `ORDER` 边。**真正丢东西的是经过 pass 之后**：584 条红集中在 `simplify_arm_write_lost.*`、`unread_gate.*`、`dropped_effects`、`consecutive_sync_calls.*` 这一族，也就是**有 pass 把 ctrl 当作"这个效果有凭据"的判据**，ctrl 一停，它们就把写/效果删掉。

**这让问题更准，而不是更松**：要裁的不是"builder 该写哪种边"（builder 那一步自洽），而是**"效果是否活着"这件事今天是由 ctrl 这个种类**代理**表达的；换成什么，才让那些 pass 不必靠一个要消失的种类来判断**。唯一"只有 ctrl、没有别的边"的两个造边点仍是 `builder_operators.cpp:4403` 与 `:4427`——它们是停造后**真的会丢**的两处。

**表怎么读**：**“真的什么都不剩”的格子是硬缺口**（只有 `:4403` 与 `:4427` 两处，实测）；**“要 pass 侧确认”的格子是软缺口**（builder 有边，但今天的 pass 靠 ctrl 判断效果是否活着）。两类都要作者一句话，但**修法不同**：硬的要一种边；软的要么让 pass 改读 SYNC/ORDER，要么保留这个种类作为“效果活着”的凭据——前者是 (a)，后者是 (b)。

## 软缺口清单：哪些判据把 ctrl 当成"这个效果有凭据"（裁决当日就能开工）

| 文件:行 | 它用 ctrl 判断什么 | 改读 SYNC / ORDER 还缺什么信息 | 做得到吗 |
|---|---|---|---|
| `passes/opt/generic/gates.cpp:19` | 一道值门**有没有等待**（有等待才允许被绕过；见 `control-edge-readers.md` 规矩二） | SYNC 能回答"链上的门"，ORDER 不能回答"门被谁等" | **能**（对链上的门）；**不能**（对下面两处硬缺口造的门） |
| `execute/fusion_group.cpp:86` | 这个节点**在组外还有没有消费者**（`with` 与 `ctrl` 出边都空才算没有） | 把 `syncUsersOf` 一起查：链上语句的 sync 使用者就是它的"下一个语句" | **能**；两处硬缺口的节点仍为"没有" |
| `runtime/graph.cpp:568` | 封口检查：一个节点**要么有同步前驱、要么有 ctrl 入边** | SYNC 已能回答；对硬缺口那两处会判成"既无 sync 也无 ctrl" | **能**（链上）；**不能**（硬缺口两处，封口会报） |
| `runtime/draft_inline.cpp:1864`（`hasExternalCtrl` / `unreadArgumentEffects`） | 拼接后**未读的实参效果**还要不要等外层 | 要知道"拼进来的语句应挂在调用点哪一条链后面"——sync 能表达，但要**先**写同步边再判断 | **能，但代价在 inline 引擎**（不是换个读法） |
| `runtime/writings.cpp:1131` | 改写时把同类代表的 ctrl 等待**搬过去** | 改读 `syncUsersOf` 后还要**重建同步链**（谁接谁） | **能**，但改写后要重排链 |
| `runtime/control.cpp:139/153/192` | 约简时能不能删等待 | 这一处是**这个种类自身**的约简 | **主体消失**（随种类一起删） |
| `runtime/rules.cpp:107`、`passes/opt/defun/defunctionalize.cpp:1493` | 都是"**没有 ctrl**"的合取项（absence-shaped） | 已经在同时查 sync / 五种 user，归零后仍然成立 | **不用改**（见附录一） |
| `compile/gir/builder_diagnostics.cpp:380-410` | 报"效果被丢掉"时的**可达性**（走 data / with / ctrl / sync，**不走 order**） | 已经走 sync；**但 order 边不算凭据**——这正是 order 方案会静默丢效果的地方 | **能**，但要把 order 也走一遍（见下） |

**数出来是：软判据 6 处，其中 3 处（`gates.cpp`、`fusion_group.cpp`、`graph.cpp` 封口）在"门只有 ctrl"这个形状上做不到，而这 3 处指向的是同一对硬缺口。** 另外 2 处（inline 拼接、writings 改写）能做到，代价在各自引擎里；1 处（`control.cpp`）随种类一起消失。

## 两处硬缺口，以及 (b) 的后半截

`builder_operators.cpp:4403`（赋值表达式的值在门后等这次写）与 `:4427`（`void` 位置的值在门后等这次调用）今天只写一条 ctrl，**没有第二条边**。若保留这个种类作凭据（(b)），它们**不需要"占位使用者"**：占位使用者要么改值的口数、要么要求有一个真消费者，都不成立。**能用的是一条 order 边**——但今天 `builder_diagnostics.cpp:380-410` 的可达性**不走 order 边**，所以 order 边保不住效果。

**因此 (b) 的后半截只有一句**：**让"效果活着"的可达性把 order 边也走一遍**（一行；`fusion_group.cpp:86` 的消费者判定跟着加一条）。这比新增边类小，也比"永远留一个 ctrl"干净——它把"效果等待"降级成"效果有一条只排序的边"，而这条边的存在与否正好就是"这次写/这次调用还是活的"。

## 边界（现状，给作者看）

**在裁决落下之前，停造 ctrl 会把这两处的等待整个丢掉，所以 ②a 不能开工；这不是"还没做"，是"做了会丢等待"。** 今天 `builder_operators.cpp:4403` 与 `:4427` 是仅存的两个"等待只有 ctrl 一条边"的造边点；读侧（`d52823be4`）已经不再要求它们留 ctrl，但**它们仍在写**，所以图还是对的。
## 附录一：两条 absence-shaped 守卫（ctrl 归零后要不要改）

| 位置 | 判据 | 归零后恒真吗 | 守着什么 | 恒真后的后果 | 对不对 |
|---|---|---|---|---|---|
| src/runtime/rules.cpp:107-108 | with.empty() && ctrlInputsOf(ref).empty() && syncPredsOf(ref).empty() | **不恒真**——第三项还在 | "只吃常量、且不是某条链或某次等待的一环"才许被规则改写 | 不多命中：sync 那一半仍拦着链上的语句 | **对**。它已经是"或"形状，ctrl 那一项可直接删 |
| src/passes/opt/defun/defunctionalize.cpp:1492-1494 | live(base) 且 norm/with/ctrl/order/sync 五种 user 全空 | **不恒真**——是"没有用户"的合取 | 闭包把静态函数打包后，确认没人用才删节点 | 删节点判定略早成立，但只在真没人用时 | **对**。死代码判定，少查一类用户是正解 |

结论：两条都**不需要**在裁决前改。真正要重新设计的仍只有 gates.cpp:19（使能条件，见 control-edge-readers.md 规矩二）。

## 附录二：std::opt::ctrl 与那 26 处用例的去向（只列，不删）

**先纠一个名字**：**std::opt::ctrl 这个 pass 已经不存在了**——它改名为 std::opt::order（dges.cn.md 5.4），	est/cases/opt/pass_surface.py:30 把它列进"已退役"并断言它不在。ffects_model_edges.plan.toml 的 known_fail_note 里那句 "std::opt::ctrl preserves them on purpose" 是**旧名残留**，裁决后顺手改。

所以"删除清单"的对象是 src/runtime/control.cpp 的三个辅助函数与它们的自检，不是 pass：

| 对象 | 今天的用户 | ctrl 归零后 | 处置 |
|---|---|---|---|
| ppendControlDependency（control.cpp:90-121） | inline/engine.cpp、generic/*、
untime/draft_inline.cpp | 无 ctrl 可加 | **主体消失**，连同调用点 |
| 
educeControlDependencies（:124-156） | std::opt::order 的部分路径、generic/* | 无 ctrl 可约简 | **主体消失** |
| hasControlDependency 及 waitUsersOf 的 ctrl 分支 | 多处 | 分支消失 | **换成等同步前驱的版本**（syncPredsOf），不是删 |
| src/passes/test/control.cpp 11 个用例（26 处提及） | control.topology、control.test_pass_runs_when_named | 见下 | 见下 |

**26 处的分类**：
- **主体消失**（测的就是 ctrl 规则本身）——8 个用例、17 处：mandatoryDataPaths(4)、denseChain(4)、ranchBoundaries(3)、chainAndBudget(2)、conditionalDataIsNotMandatory(1)、dataBudget(1)、denseDataChain(1)、documentOrdering(1)。
- **换写成等同步前驱的版本**——1 个用例、6 处：orwardingGateConsumers（门的绕过判据在 ② 之后要重新设计，用例跟着重写）。
- **一行不用改**——2 个用例、0 处：manyReaders、syncChainOrdering（已经是同步边的用例，正是新模型要的那一半）。

**Plan 侧**：	est/plans/feat/opt/control.plan.toml 4 条里 control.topology 与 control.test_pass_runs_when_named 依赖这个自检；control.pipeline_contract、control.test_pass_not_in_default_pipeline 与 ctrl 无关。	est/cases/opt/control_contract.py 是 Python 侧契约用例，一并跟。
