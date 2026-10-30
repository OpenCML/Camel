# 一问：值在门后等一次写、或等一次调用——这件事该由哪种边承担？

> 状态：等作者裁决。裁决之前，控制边不能停造（理由见末节"边界"）。
> 相关：`docs/design/effects/edges.cn.md` 1（边只有 data/sync/order）、3.5（写与读的顺序）、5.3（GATE = 一个数据口 + 一个同步前驱）；`docs/design-principles.cn.md` 3.1。
> 已落地的两步：`d52823be4`（读侧不再要求门留 ctrl）、`77fd9e53f`（臂出口的门改等链）。车道 `lane-edges`。

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

## 裁决后的第一步：把"Order 不担版本"收窄（已验）

我原来把 Order 那 81 条红写成"Order 不担版本"，**验完要收窄**——不是"顺序边传不了版本"，是**顺序边不让一次写成为可观察的效果**。

验法：把三处 `Ctrl` 换成 `Order`，对 `var.cml`、`var_params.cml`、`var_tensor_reference.cml` 各取 `std::gir` 前后对比。三张图里**消失的不是边，是节点**：

```
-            N04 [label=":op/assn_l", type="OPER", ... value_type="var int"];
-            N05 [label="GATE", type="GATE", value_type="int"];
-            N05 -> O02 [label="0", type="EXIT"];
+            N02 -> O02 [label="0", type="EXIT"];
-            N04 -> N05 [label="0|0", type="CTRL"];
```

**写节点和它的门一起没了**，图尾直接取表达式的值。也就是说：**那条等待今天是"这次写是活的"的唯一凭据**；换成 order 边之后写被当死代码删掉，读于是读到旧值。这与"门被 `bypassValueGate` 绕过"**不是**一回事——门根本没被绕过，它和写一起消失了。

**所以裁决要回答的其实是**：什么边能让一次写/一次调用**成为可观察的效果**？（不是"排序"，排序已经够了。）这也让 (b) 的理由更硬：今天没有任何一种边承担"效果活着"，只有 ctrl 承担。

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
| ppendControlDependency（control.cpp:90-121） | inline/engine.cpp、generic/*、untime/draft_inline.cpp | 无 ctrl 可加 | **主体消失**，连同调用点 |
| educeControlDependencies（:124-156） | std::opt::order 的部分路径、generic/* | 无 ctrl 可约简 | **主体消失** |
| hasControlDependency 及 waitUsersOf 的 ctrl 分支 | 多处 | 分支消失 | **换成等同步前驱的版本**（syncPredsOf），不是删 |
| src/passes/test/control.cpp 11 个用例（26 处提及） | control.topology、control.test_pass_runs_when_named | 见下 | 见下 |

**26 处的分类**：
- **主体消失**（测的就是 ctrl 规则本身）——8 个用例、17 处：mandatoryDataPaths(4)、denseChain(4)、ranchBoundaries(3)、chainAndBudget(2)、conditionalDataIsNotMandatory(1)、dataBudget(1)、denseDataChain(1)、documentOrdering(1)。
- **换写成等同步前驱的版本**——1 个用例、6 处：orwardingGateConsumers（门的绕过判据在 ② 之后要重新设计，用例跟着重写）。
- **一行不用改**——2 个用例、0 处：manyReaders、syncChainOrdering（已经是同步边的用例，正是新模型要的那一半）。

**Plan 侧**：	est/plans/feat/opt/control.plan.toml 4 条里 control.topology 与 control.test_pass_runs_when_named 依赖这个自检；control.pipeline_contract、control.test_pass_not_in_default_pipeline 与 ctrl 无关。	est/cases/opt/control_contract.py 是 Python 侧契约用例，一并跟。
