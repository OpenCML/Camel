# `edit`/`view` 块的差距表（2026-11-25，`lane/cc-edit-block`）

对照三份文本，逐条给「有 / 无 / 半」，并指出今天能证明「有」的用例：

- `docs/design/graph-editing/README.cn.md` 第 2–8 节、第 15 节（下文记作 `2.x`、`15.x`；该文件就是派工里写的 `docs/graph-editing.cn.md`，树上的路径是 `docs/design/graph-editing/README.cn.md`）；
- `docs/design-principles.cn.md` 9.12（记作 `9.12-x`）；
- 现状依据：`src/stdlib/graph.cml`、`src/stdlib/graph/module.cpp`、`src/stdlib/graph/txn.cpp`、`src/kernel/runtime/draft*.{h,cpp}`、`src/kernel/runtime/{graph,graph_edges,branch_arms}.cpp`、`src/stdlib/test/{edit_record,commit_checks}.cpp`。

树：`D:\Projects\Camel-wt\edit-block`，分支 `lane/cc-edit-block`，基点 `origin/integration` = `6fcee16d0c`。

## 0. 先读到的四件事（其中两条与派工摘要不符）

1. **块语法与句柄作用域：无。** 全树没有 `edit` / `view` 这两个词的语法：`token_kinds.def` 没有它们，`scripts/context-keywords.mjs` 的上下文关键字表也没有（表里是 `range tag rule fact resource on read_by when uses spelling pure panic join meet widen narrow operator struct tuple array nullable fallible map set leaf error fields indices unordered readonly field nodes param`）。`graph.cml` 仍是 `open`(351) … `commit`(430) 的写法。
2. **草稿记编辑种类：有。** 派工摘要说「④ 草稿不记编辑种类」，与树不符：`EditKind`（`src/kernel/include/camel/runtime/draft.h:361`，九种：`Replace Erase Eval Inline Outline Order ParamPort ExitValue LowLevel`）、`GraphDraft::recordEdit`（`src/kernel/runtime/draft_edit.cpp:71`）、`editedWith` / `editCount` / `editedRegions` / `editTier`（`draft.h:585-597`）、`EditListener::onEditRecorded`（`draft.h:399`）都在，且有一份读数：`test::edit_record`（`src/stdlib/test/edit_record.cpp`，计划 `test/plans/feat/runtime/edit_record.plan.toml`，用例 `test/cases/runtime/edit_record/a_program.cml`），它按反证法写（把 `recordEdit` 里的通知注释掉，`notified` 掉到 0）。
   剩下的缺口只有一个：**九种里只有 `Replace`/`Erase`/`Order` 被算子标上**（`src/stdlib/graph/txn.cpp:261`、`285`、`588`），`inline`/`outline`/`eval` 仍以最后一行 `LowLevel` 到手（`draft_edit.cpp:164-171` 自己写着这件事）。`ParamPort` 与 `ExitValue` 连算子都没有。
3. **`inline` / `outline` / `order` / `eval` / `exit_of` 都已落地**（`module.cpp:231-241`、`255-263`），各有用例（见下表）。派工摘要「`graph.cml` 有 `order`/`inline`/`outline`，但仍是 `open`…`commit` 的写法」只对外壳说对了。
4. **六项提交检查：六项都在。** 出口在顶层区域（`src/kernel/runtime/graph.cpp:710` 的 `validateRunsWithTheBody`）、无环（`graph_edges.cpp:771` `findCycle`）、作用域/跨区域读（`src/kernel/runtime/branch_arms.cpp:99` `validateRuntimeBranchArms`）、端口与出口符合声明的类型（`validateRuntimeGraphPortTypes`）、效应包含、冲突有先后（`src/kernel/include/camel/runtime/resources.h:485` `commitViolation`）。缺的是**按编辑种类分档**的那一半（6.12、8.4 两行）。
   （`graph.cpp:612` 那句「Which nodes may read which is checked once control regions exist.」是**过期注释**：`branch_arms.cpp` 就是那个检查。这是一条文档缺陷，不是功能缺口。）

## 1. 第 2 节 语法

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 2.1 | `edit d = f { 语句 }` / `view g = f { 表达式 }` 两种块，块头与 `loop s = init` 同形，`=` 是绑定 | **无** | 语法表无这两个词；`graph.cml:351` 是 `open` |
| 2.2 | 两种块都是表达式；块里最后一个表达式是块的值；`return` 结束块本身 | **无** | 无块 |
| 2.3 | `edit` 块的值类型是 `G!`，`G` 由块外期望类型给出，没有就是 `f` 的类型 | **无** | `commit` 只交回 `F!`（`graph.cml:430`），没有「期望类型」这一说 |
| 2.4 | `view` 块的值是体的值（类型由体推出） | **无** | 无 `view` |
| 2.5 | `edit` 本身是纯的，体里有外部访问整个表达式才有那个效应；结果没人用要报 | **半** | 编辑算子以 `uses var t` 记在签名上（`graph.cml:358` 起），这是「写草稿」的效应；「结果没人用」的检查未找到 |
| 2.6 | 块可以嵌套，内层对另一个函数值 | **无** | 无块 |

## 2. 第 3 节 哪些算子只能在块里调用

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 3.1 | `graph` 模块导出的类型都是句柄类型；签名提到句柄类型的函数就是图算子，只能在块里调用 | **无** | 今天 `graph_of`(52)、`graphs_of`(57) 在块外就能拿句柄；没有块，规则无处落 |
| 3.2 | 句柄只从块头来（删掉 `graph_of` 这类「从普通值拿句柄」的函数） | **无** | `graph_of`/`graphs_of` 仍在（`graph.cml:52`、`57`） |
| 3.3 | 改图算子只收 `edit` 块的句柄，`view` 的节点交给它们是类型不符 | **无** | 只有 `Txn<F>`，没有块种类标签；`replace`/`order`/… 以 `t: Txn<F>` 为首参（`graph.cml:358`、`385`） |
| 3.4 | 关键字只用在块的边界上（`view`、`edit`，加上已有的 `rule`） | **无** | 两个词都不是关键字，也不是上下文关键字 |

## 3. 第 4 节 句柄出不了块

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 4.1 | 句柄类型带不可命名的块标签 `β`；每个块引入一个新的 | **无** | 类型上没有标签；`Node<T>` 只有元素类型（`graph.cml:37`） |
| 4.2 | 不能作为块的值交出 | **无** | 无块 |
| 4.3 | 不能存到块外（块外建的 `Ref`/结构体/数组） | **无** | 无标签 |
| 4.4 | 不能被离开块的闭包带走 | **无** | 无标签 |
| 4.5 | 帮助函数对标签泛型，在块里实例化成那个块的标签 | **无** | 无标签 |
| 4.6 | 普通值可以交出（`view d = f { len(nodes(d)) }` 交整数） | **无** | 无 `view` |
| 4.7 | 迁移第 11 节要求的四条具名反例诊断（交出、存进块外 `Ref`、闭包带走、`view` 节点交给 `replace`） | **无** | 一条也没有 |

## 4. 第 5 节 编译之后：每个块是算子表里的一个算子

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 5.1 | 编译后没有块节点；每块一个新算子，外层图里就是普通算子节点 | **无** | 块不存在；今天的编辑算子（`graph:replace` 等）是**图里的普通调用节点**，正是设计要消掉的那种 |
| 5.2 | 算子按块体规范形式命名，参数按规范形式中第一次使用的先后 | **无** | 块不存在 |
| 5.3 | 块体（连同它调用的、签名提到句柄的帮助函数）不属于程序的图，任何遍看不到 | **无** | 没有「图外的图」这条机制：`subprogram`/`block body` 一类在 `src/` 下 `git grep` 为空；今天的闭包/函数值都是程序的图，遍看得见 |
| 5.4 | 块体内部照旧受句柄不逃逸与块尾提交约束 | **无** | 无块 |
| 5.5 | 规则应用同样是一个算子（`rewrite`/`run`/`saturate` 内部就是一个 `edit` 块） | **无** | 规则引擎（`src/runtime/writings_*`）与草稿是两套 |
| 5.6 | 执行时机不归块管；要早做就把调用它的函数标 `#macro` | **半** | `#macro` 有；块没有，所以无从谈起 |
| 5.7 | 调试块体靠跟踪选项的区域，不靠 `std::gir` | **无** | 无块 |

## 5. 第 6 节 `edit` 的语义

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 6.1 | 进入块：对目标函数开一份草稿，绑定到名字；目标函数对块外持有者不变 | **半** | `open`(351) 就是这份草稿；「原函数不变」有用例 `graph_values.plan.toml::function_txn_leaves_the_original`（`cases/modules/graph_values/function_txn.cml`）；缺的是「进入块就绑定、块外不可见」 |
| 6.2 | 块内按普通语义求值，写草稿按书写顺序、不写 `sync`，每一步做当场检查，一步失败块立即结束 | **半** | 当场检查以运行期故障给出（`txn.cpp:246` 起 `err::throwRuntimeFault(InvalidArgument, …)`）；「块立即结束」没有块可结束，故障是抛给程序的，不是块的值 |
| 6.3 | 正常走到块尾**自动**提交 | **无** | 显式 `commit`（`graph.cml:430`） |
| 6.4 | 任何失败都放弃整份草稿，块的值是一个错误值，说明哪一步、为什么 | **半** | `abort` 有（`graph.cml:434`）；`commit` 只交 `GraphConflict`/`GraphInvalid`/`TxnUsed` 三种错误值（`graph.cml:428`），中途某一步失败不会变成块的值 |
| 6.5 | 没有「程序」这一层：草稿是 `f` 加上从 `f` 可达的全部图，以及这次新造的图，块尾一次提交 | **有** | `open` 的算子注释「a transaction over a function and the graphs it reaches … commits to a program of its own」（`txn.cpp:217`）；`graphs_of` 列出可达图（`graph.cml:57`）；用例 `graph_values.plan.toml::program_txn_leaves_the_program_and_the_running_code` |
| 6.6 | 确定性：`nodes`/`graphs` 按规范顺序；读拼写要声明 `spelling` | **有** | `nodes` 的规范顺序（`graph.cml:66-74`）、`name`/`origin`/`show`/`creation_id` 带 `spelling`（`graph.cml:64`、`257`、`290`、`313`）；用例 `graph.plan.toml::draft_id_is_refused_and_the_published_id_is_a_position`（`cases/modules/graph_draft_id.py`）、`feat/compile/graph_read.plan.toml::graph_read.draft_agrees_with_frozen` |
| 6.7 | `view` 没有草稿、没有提交，块尾什么也不做 | **无** | 无 `view` |

## 6. 第 7 节 编辑算子

| # | 算子 | 现状 | 证据（实现 / 用例） |
|---|---|---|---|
| 7.1 | `replace(old, new)`：`new` 上游对 `old` 的读不改接；拒绝锚点、拒绝自己算自己 | **有** | `txn.cpp:245`（`replaceKernel`，拒绝锚点 243、拒绝自环「computes from it」）/ `graph.plan.toml::txn_replace_and_commit`、`txn_invalid_edit_is_refused_at_commit`（`cases/modules/graph_pass/edits.cml`） |
| 7.2 | `erase(n)`：值还有人读、是效应、是锚点都拒绝 | **有** | `txn.cpp:285` / `graph.plan.toml::txn_erase_of_a_read_value_is_a_fault`（同上一文件） |
| 7.3 | `eval(n)`：把纯算子就地算成常量 | **有** | `module.cpp:236` / `graph.plan.toml::constant_folding_makes_a_value`（`cases/modules/graph_pass/fold_target.cml`） |
| 7.4 | `inline(at, fn, args…)`：实参、接过链上位置、纯节点只选区域、体带写时挂进区域出口 `GATE` | **半** | 有 `inline`，但签名只收**零实参**的函数值（`graph.cml:397`，文档自己写「`fn` takes no arguments」）；「接过链上位置 / 只选区域」有文档与用例 `graph_inline.plan.toml::inline_at_a_statement_takes_over_its_place`（`cases/modules/graph_function/inline.cml`）；`args…` 完全没有 |
| 7.5 | `outline(nodes)`：把一段收成函数值，边界检查 | **有** | `txn.cpp`（`outlineKernel`，`graph.cml:415` 的 `@fails` 逐条列了边界）/ `graph_outline.plan.toml::outline_delivers_the_graph_it_makes`、`outline_delivers_a_graph_the_bytecode_vm_can_compile`（`cases/modules/graph_function/outline.cml`） |
| 7.6 | `order(a, b)`：加顺序边，拒绝成环与不同图 | **有** | `txn.cpp:588` / `graph_edits.plan.toml::order_adds_an_edge_and_the_program_still_computes`、`order_refuses_a_cycle`（`cases/modules/graph_pass/edits_order.cml`） |
| 7.7 | `add_param(d)` / `add_param(d, at: k)`；`erase(p)` 作用在形参端口 | **无** | 算子表里没有（`module.cpp:231-241` 只有 `open replace erase commit abort eval order inline outline add_constant`）；`EditKind::ParamPort`（`draft.h:368`）只有枚举与档位注释，没有算子；`graph.cml` 没有声明 |
| 7.8 | `replace(exit_of(d), v)` 换出口的值（可以是另一种类型） | **无** | `replace` 拒绝锚点（`txn.cpp:243`），而 `exit_of` 交的正是锚点（`graph_edits.plan.toml::exit_of_is_the_node_the_function_hands_out` 的读数里有 `anchor=true`）；`EditKind::ExitValue` 没有算子。**`graph.cml:77-83` 的 `exit_of` 文档与这条自相矛盾（它说「`replace` 拒绝锚点：读这个节点、造它的替代、再 replace 这个节点」），是缺陷候选** |
| 7.9 | 写分析用的两样查询：以节点为键按规范顺序遍历的映射；从调用节点取被调函数 | **有** | `nodes` 规范顺序（`graph.cml:66`）、`callee`（`graph.cml:214`）/ `graph_entry_graph.plan.toml::the_programs_call_names_the_entry_functions_graph`（`cases/modules/graph_pass/entry_target.cml`） |

## 7. 第 8 节 检查与错误报告

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 8.1 | 块内每一步当场检查：`replace` 类型相容/不成环/读者看得见；`erase` 没人读/不是效应/不是锚点；`inline` 实参类型与可见；`outline` 边界；`order` 不成环/同图 | **半** | 五类都在算子入口拒绝（`txn.cpp` 的 `throwRuntimeFault`），但**没有一条是「当场看得见」这个方向专门验过的**：有用例的是 `order` 的成环（`order_refuses_a_cycle`）与 `erase` 的读值（`txn_erase_of_a_read_value_is_a_fault`）；「`new` 在被改接的读者看得见的区域里」今天落到提交的作用域检查，不是当场 |
| 8.2 | 块尾提交六项检查：①出口在顶层区域 ②无环 ③作用域 ④端口与出口符合声明类型 ⑤效应包含 ⑥冲突有先后 | **有** | ①`graph.cpp:710`；②`graph_edges.cpp:771`；③`branch_arms.cpp:99`；④`validateRuntimeGraphPortTypes` + `feat/opt/commit_refusal.plan.toml::commit_refusal.port_type`（`cases/opt/commit_checks/port_type.cml`）；⑤⑥`resources.h:485` + `commit_refusal.order`/`.effect`/`.legal_splice`（`cases/opt/commit_checks/fold_function_type.cml`）；③的用例是 `commit_refusal.sibling_arm`、`commit_refusal.loop_body_read` |
| 8.3 | 错误值写什么：违反哪条规则、涉及哪几个节点、**是块里哪一步造成的**（源码位置） | **半** | 规则与节点有（`CommitViolation`：`check`/`graph`/`first`/`second`/`text`，`resources.h:416`）；源码位置只有 `origin` 一个（`resources.h:421`），而草稿的备注 `EditNote{.kind, .region}`（`draft.h`）不带位置 ⇒「指回第几行的 `inline` 造出了这个没有先后的写」做不到 |
| 8.4 | 开销分档：只用不新增访问的算子（`replace`/`erase`/`eval`/`outline`）的块发行构建不查效应与顺序；用了 `inline`/`order` 的照查；结构/作用域/类型/无环任何时候都查 | **半** | `editTier` 有（`draft_edit.cpp:144`，两半：`made` 与 `lowLevelEdits`），四个结构项任何时候都查；但只有 `Replace`/`Erase`/`Order` 标了种类，`eval`/`outline`/`inline` 以 `LowLevel` 到手（`draft_edit.cpp:164-171`），于是**今天没有任何块能因为「只用了不新增访问的算子」而省掉两问** |

## 8. 第 8.1 节 每种编辑可能破坏哪几项

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 8.1.1 | 草稿记下自己用过哪几种编辑，提交据此决定查哪几项 | **有** | `EditKind`/`recordEdit`/`editedWith`/`editTier`（`draft_edit.cpp:71`、`131`、`144`）；读数 `feat/runtime/edit_record.plan.toml::runtime.edit_record.two_entries_one_record_and_the_tier_it_owes` |
| 8.1.2 | 表里每一行都有对应的编辑可被记录 | **半** | `param_port`、`exit_value` 两行没有算子（见 7.7、7.8） |
| 8.1.3 | 「调试构建与测试一律查全部六项」 | **半** | `commitChecksEnabled`/`setCommitChecksEnabled` 有（`resources.h:567`）；「测试一律查全部六项」与 `editTier` 的关系未见一条读数 |

## 9. 第 15 节 语法

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 15.1.1 | 两种块的写法（同 2.1） | **无** | 同 2.1 |
| 15.1.2 | 块头不写类型，结果类型由块外期望类型给出；也可以 `=> 类型`（原则 9.12 2026-11-23 补的 `edit g = f => Step { … }`） | **无** | 无块 |
| 15.1.3 | 遍照常是函数，形状 `(f: F) => F!` | **有** | `graph.cml:9-12` 的模块注释；用例 `graph_pass_function.plan.toml::pass_over_function_keeps_the_program` |
| 15.2.1 | 句柄类型 `Graph`/`Node<T>`/`Region`/`Op`/`TypeRef`/`Tag`，作用域标签由编译器补 | **半** | 六个类型有（`graph.cml:36-41`）；标签没有 |
| 15.2.2 | 根查询以草稿为第一个实参：`nodes(d)`/`graphs(d)`/`params(d)`/`exit(d)` | **半** | `nodes(g)`/`graphs_of(f)`/`exit_of(g)` 有（`graph.cml:74`、`57`、`83`）；`params` 没有 |
| 15.2.3 | 节点上的查询与事实 | **有** | `graph.cml:90-199` 一整段；用例 `graph.plan.toml::facts_of_graphs_and_nodes`（`cases/modules/graph_facts.cml`） |
| 15.2.4 | 读拼写的查询（`name`、`origin`、创建编号）要求函数声明里写出来 | **半** | 有，但写成**能力**（`=> string spelling`，`graph.cml:64`），不是 15.5 的 `resource spelling readonly`；用例 `graph.plan.toml::draft_id_is_refused_and_the_published_id_is_a_position` |
| 15.3.1 | 编辑操作表（`replace`/`replace(exit(d), v)`/`erase`/`inline`/`outline`/`order`/`eval`/`add_param`） | **半** | 见第 6 节：`add_param` 与换出口值两项无，`inline` 少了 `args…` |
| 15.3.2 | 类型实参能从期望类型推出时不写，推不出才写 `::<T>` | **无** | 无 `add_param`/`outline::<F>` 这一层 |

## 10. 第 15.5 节 效应里的两个只读资源

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 15.5.1 | `export resource graph readonly`（读函数的图） | **无** | `src/stdlib` 里的 `export resource` 只有 `process`、`rng`、`rendezvous_point`、`driver`；没有模块声明 `graph`。`readonly` 修饰符本身有（`scripts/context-keywords.mjs` 的 `readonly` 条目，`resource graph readonly` 就是那条目的例子） |
| 15.5.2 | `export resource spelling readonly` | **半** | 同 15.2.4：`spelling` 是能力不是资源；`uses var spelling` 会不会被拒未验 |
| 15.5.3 | 带 `uses graph` 的：`view`/`edit` 块，`grad`/`vjp` 一类 | **无** | 无 `graph` 资源 |
| 15.5.4 | 带 `uses spelling` 的：调用了读写法查询的代码 | **半** | 能力形式在起作用（`graph.cml` 里那些 `spelling` 声明，`gct/builder.cpp:1171` 读它） |

## 11. 原则 9.12 逐条

| # | 要求 | 现状 | 证据 |
|---|---|---|---|
| 9.12-1 | 图操作只写在三处：规则、`view` 块、`edit` 块；块头与 `loop` 同形；两种块都是表达式 | **半** | 规则有；两种块无（第 1 节） |
| 9.12-2 | 编译后没有块节点：每块是算子表里一个按块体规范形式命名的算子，块体不属于程序的图 | **无** | 见 5.1、5.3 |
| 9.12-3 | 句柄出不了块（值、块外 `Ref`、闭包三条） | **无** | 第 3 节 |
| 9.12-4 | `edit` 交出新函数、原函数不变；块尾自动提交，任何一步失败整份放弃、值是错误值 | **半** | 「原函数不变」有（6.1）；「自动提交」「值是错误值」无（6.3、6.4） |
| 9.12-5 | 块头可以用 `=> 类型` 写出；不写由块外期望类型给出 | **无** | 无块 |
| 9.12-6 | 规则与事实的表达式写法（`rule (x: int) { … }`，只能捕获编译期常量） | **未验** | 不在本活判据内，未读 |
| 9.12-7 | 规则的右侧可以是 `edit { … }`（动作规则），一次应用全成或全不成 | **无** | 无块；`ruleBody` 只有 `=>` / `<=>`（`node_kinds.def` 的 `RuleBody`） |
| 9.12-8 | 饱和中每个编辑操作的含义（添写法与并类可以，删除/改接/加顺序当场失败） | **未验** | 在 `saturate` 一侧，不在本活判据内，未读 |
| 9.12-9 | 草稿 = 被编辑的函数、它可达的全部图、这次新造的图；没有「程序」层 | **有** | 6.5 |
| 9.12-10 | 编辑算子：`replace`、`erase`、`inline`、`outline`、`order`、`eval`、增删形参端口、换出口值 | **半** | 7.1–7.8：增删形参端口、换出口值两项无 |
| 9.12-11 | 两层入口：高层编辑算子给 Camel 用，底层是内核图原语给 C++ 遍用；**原语层记下每次编辑的种类与碰过的区域并通知监听者** | **有** | `GraphDraft::NamedEdit` 与 `noteEditedNode` 都落在 `recordEdit`（`draft_edit.cpp:71`、`95`），`EditListener` 每编辑一次通知一次；读数 `test::edit_record` 的 `notified == notes` |
| 9.12-12 | 提交六项检查，**按编辑种类分档**、只在碰过的区域及其祖先里做 | **半** | 六项有（8.2）；分档的种类只标了三种（8.4）；「只在碰过的区域及祖先」有 `editedRegions` 与 `touchedRegions` |
| 9.12-13 | 不设跳过检查的模式 | **有** | `resources.h:564-567`（`commitChecksEnabled` 是遍边界的答案，提交检查不看构建） |
| 9.12-14 | 读图是对只读资源 `graph` 的一次访问 | **无** | 15.5.1 |
| 9.12-15 | 读拼写是对只读资源 `spelling` 的访问 | **半** | 15.2.4 |

## 12. 结论：② 不能作为一条「最小改动」落地

「块语法 + 句柄作用域」要落成的东西不是一条语法，而是下面五件一起，缺一件就只剩假实现：

1. **语法**：`edit`/`view` 两个上下文关键字（`scripts/context-keywords.mjs` 的表 + 两条 `atWord` 证据行）、一个产生式、一个 `NK::` 节点、一个 AST `Load` 类、AST 构建器分支、语言参考的两个语言版本（`check-grammar --write` 生成）、`ast_hashes` 金标。这一段本身是几百行、十个文件，且不改变任何今天已绿的语法（好在这一段不是障碍）。
2. **类型里的块标签 `β`**：句柄类型带标签、块引入新标签、标签出块即不良构。今天 `Node<T>` 只有一个类型参数（`graph.cml:37`），类型系统里没有「不可命名的类型变量」这一层，`Ref`/结构体/闭包三处的逃逸规则要各自落。
3. **块体不属于程序的图**：这是最关键的一件。设计要「块体由同一个编译器编译、同一个虚拟机执行，但任何遍看不到它」（5.3）。树上没有这条机制：`src/` 下没有子程序/隐藏图的构造点，闭包与函数值都是程序自己的图（遍看得见）。要做的是编译器新增「贴在算子上的图」，加上算子表登记、VM 执行与按规范形式命名。
4. **块尾自动提交与「值是错误值」**：今天 `commit` 是普通算子，失败以故障或三种错误值出现（6.3、6.4），块的边界不存在。
5. **`add_param`**：它必须在块头能声明新签名的前提下才有意义（提交的 ④ 端口/出口类型检查会拒绝只改端口不挡类型的那一份），所以它排在 ② 之后，不能先做。

因此「先落块语法与句柄作用域」不是一条最小改动，而是车道 L（`internal/plans/2026-11-18-lane-map.cn.md` 的 L 行）本体：它自己列的文件就是「前端解析与类型检查、`stdlib/graph.cml`、效应推断」。

**本活停在这里**：块语法与 `add_param` 不落，理由是它们不可能在一条派工里成真；把假的外壳（把 `open`/`commit` 包成块、或把块体编进外层图）落进去，正好是原则 9.12 与设计第 5 节点名要避免的那种实现（「草稿句柄是图里的一个值」「一个遍去改另一个遍的图」），而 AGENTS.md 说设计原则与代码冲突时以原则为准。

**可以立刻接着做、且各自独立可验的三块**（按依赖排序）：

1. **把剩下六种编辑标上种类**（`inline`/`outline`/`eval` 三个在 `txn.cpp` 里各开一个 `NamedEdit`），补齐 8.4 与 9.12-12 的「半」。读数：`test::edit_record` 的 `kinds=` 行出现 `inline,outline,eval`，`editTier` 对只含这三者的草稿给出 `effects=0 order=?`。**注意这会让分档真的生效**，所以要同时报「哪些用例的 `[SUMMARY]` 变了」。
2. **`add_param` 的算子与声明**（在 `txn.cpp` + `graph.cml` 里，端口是图上的普通节点），它的提交会因 ④ 被拒——这本身是一条可写的读数（「只加端口不改声明 ⇒ 提交拒绝，报文点名端口」），但要让它在块出现前有用，还差块头。
3. **8.3 的「是块里哪一步造成的」**：给 `EditNote` 加一个 origin，让提交的诊断能指回造出问题节点的那次编辑。它不依赖块，且是 `CommitViolation.origin` 已经在等的东西。

## 13. 本活没有做的事（明确写出来，避免被读成「已验」）

- 没有构建、没有跑任何计划、没有跑统计脚本：本页全部读数来自 `git grep`、读源码与读既有计划的 `name`/`desc`/`case` 字段，不是一次运行的输出。所以「有」的意思是「实现与用例都在树上、用例的判据确实断言了这件事」，不是「我今天看见它绿」。
- 9.12-6、9.12-8 两行标「未验」：规则表达式写法与饱和里的编辑含义不在本活判据内，没有读。
- 第 8 节的「半」是按「有用例证到」的标准判的；实现可能在，但没有用例把它钉住——那正是需要补的用例，不是已经有的读数。