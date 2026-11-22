# `var` 只是值的名字：代价清单（测量车道）

- 基线：`b35936243`（`origin/develop`），工作树 `D:\Projects\Camel-wt\varscope`，分支 `lane-varscope`。
- 规范来源：`origin/fsh-dev` 的 `e3447538c`（`docs/design-principles.cn.md` 3.5、2 节表格 `var` 一行）
  与 `internal/reviews/questions-2026-10-29.md` 尾部「裁定与排期调整（2026-11-15）」新排第 1–3 项、第 6 项。
- 本车道只测量：没有改 `src/`、没有改 `test/`、没有改 golden、没有写实现、没有 `git stash`。
  文中所有行号都是基线 `b35936243` 上的行号。

---

## 1. 单元在代码里的落点

### 1.1 一个单元是什么

今天的实现里一个 `var` 不是「名字」，是一个**单槽可变对象**：类型层 `TupleType::cell(T)`（打印成 `var T`），
图上是一个 `:var/new` 节点，读是 `:var/get`（可保留值）或 `:var/peek`（不保留值），
写是 `:op/assn*` 家族（`=`, `+=`, …, `:op/assn_owned`），搬走值给原地形式是 `:var/take`，
被调函数写过以后的版本是 `:var/after`。`var` 形参与闭包捕获口都直接传这个单元。

**这不是 `:var/get` / `:var/set` 两个算子**：是 **7 个 `:var/*` 算子**（`new`、`new/empty`、`get`、`peek`、`take`、`after`，
外加 `:op/assn_owned`）+ **47 个 `:op/assn*` 执行器**（`ops.cpp` 37 + `half_ops.cpp` 10）+ **一个 cell 类型构造器**，
注册表里 **59 条**。

### 1.2 定义点

| # | 文件:行 | 它为什么碰单元 | 分类 |
|---|---|---|---|
| 1 | `include/camel/core/type/composite/tuple.h:106`（`static TupleType *cell`）、`:107`（`isCell`） | 单元类型本身：一个值类型一个 cell 对象，注释写明「shared, by pointer, with every `var` parameter and capture」 | **删掉** |
| 2 | `src/core/type/composite/tuple.cpp:175-186` | `cell()` 的 interning 实现（`TypeTable::Shape::Cell`） | **删掉** |
| 3 | `src/core/type/text.cpp:161,176-177` | 类型打印出 `var T` | **删掉**（随 cell） |
| 4 | `src/core/type/collection.cpp:84`、`src/core/type/pure.cpp:96`、`src/core/type/typecall.cpp:402` | cell 在数组元素、纯类型判定、类型调用里的特例分支 | **删掉** |
| 5 | `src/core/type/typevar.cpp:700,959,1121,1377` | 类型替换/合一里 cell 保持形状 | **删掉** |
| 6 | `src/core/type/resolver.cpp:550-553` | 重载决议时把 `var` 实参的类型剥成值类型（`valueTypeOf`）——新语义下实参本来就是值 | **改写** |
| 7 | `include/camel/core/operator_table.h:99-103`（`VarNew/VarGet/VarPeek/VarTake/AssnOwned`）、`:121`（`VarNewEmpty`）、`:126`（`VarAfter`）、`:138`（`isVariableRead`） | 7 个按 identity 认的算子 id；id 是编译期固定、`kWellKnownOperatorUris` 按 id 索引，删了要留空洞而不是重排 | **删掉**（留空洞） |
| 8 | `src/builtin/modules/globals.cpp:795-916`（`__var_new__`/`__var_new_empty__`/`__var_get__`/`__var_after__`/`__var_peek__`/`__var_take__`/`__assn_owned__`）以及同文件 49 处 `:op/assn` 的 `__assn_*__` 组 | 全部算子声明与类型解析器（`(value: T) => var T`、`(variable: var T) => T`） | **删掉**（≈420 行，见 §2） |
| 9 | `src/builtin/executors/builtin.cpp:161-254`（59 条 `{"op/assn…"}` 注册）、`:731-735`（5 条 `:var/*` 的 `stateOnly`） | 执行器注册表 | **删掉** |
| 10 | `src/builtin/executors/builtin_semantics.cpp:452-469` | 5 个 `:var/*` 的语义元数据（`stateOnly`） | **删掉** |
| 11 | `src/builtin/operators/ops.h:10-18` + `20-42` | 43 个执行器声明（37 个 `assn` + 6 个 `var`） | **删掉** |
| 12 | `src/builtin/operators/ops.cpp:215-542, 1010-1045` | 43 个执行器定义：`var_new` 建槽、`var_get` 做 `shareForSecondName`、`assn` 写槽、`var_take` 把槽清空、复合赋值读改写 | **删掉**（≈370 行） |
| 13 | `src/builtin/operators/half_ops.cpp:105-173` + `half_ops.h` | bf/f6 的 10 个复合赋值执行器 | **删掉**（≈70 行） |
| 14 | `src/builtin/operators/set_value.cpp:87` | `isCell()` 参与 `set_index` 的类型检查 | **删掉** |
| 15 | `src/compile/gir/builder.h:997`（`variables_`）、`1007-1013`（`VariableRead`/`variableReadSource_`/`knownCellUsers_`/`escapingVariables_`）、`1019-1025`（`callWriteMasks_`/`CallValue`/`callValues_`）、`1026`（`collapseNonEscapingVariableReads`） | 单元的簿记：哪些节点是单元、每个读读的是哪一版、哪些单元逃逸 | **删掉**（SSA 全局化后无一需要） |
| 16 | `src/compile/gir/builder.h:1085-1089`（`pendingUses_`/`VariableUse`）、`1118-1128`（`ArmBuild::Touched`） | 写与读的**顺序**记录，以及分支臂「臂前/臂后」的版本——后者正是新语义要的「分支成为结果」的现成结构 | **不动**（`Touched` 留）/ **改写**（`pendingUses_` 只留同步顺序） |
| 17 | `src/compile/gir/builder_expr.cpp:485-491` | `var x = e` 变成 `newVariable(e)`（建单元） | **改写**（直接绑到值） |
| 18 | `src/compile/gir/builder_expr.cpp:549-671` `recordWrite` | 一次写：给读排顺序、把写的目标标成 cell 的已知用户、经被调写入时记 `writeBeforeCall_`、把捕获写的变量记进 `captures_[].written` | **改写**（顺序边里「单元」的概念去掉；捕获写那半段整段删） |
| 19 | `src/compile/gir/builder_expr.cpp:673-697` `captureWriteMaskOf`、`:762-810` `valueVersionOf`、`:1620-1675` `variableOperator`/`variableAfterCall`、`:1676-1696` `newVariable`/`newVariableOfType`、`:1772-1779` `peekVariable` | 建单元、读单元、取版本、`:var/after` | **删掉** |
| 20 | `src/compile/gir/builder_expr.cpp:1697-1771` `readVariable`/`readVariableThrough` | 读一个变量：当前实现要产出 `:var/get` 节点 | **改写**（读就是当前版本节点，不建节点） |
| 21 | `src/compile/gir/builder_expr.cpp:1780-1929` `collapseNonEscapingVariableReads` | **已经有半套 SSA**：不逃逸的 `var` 把读折成写的那一版；逃逸的（被闭包捕、当 `var` 实参交出、`var` 形参）保留单元 | **删掉**（150 行；新语义下「逃逸」这一档不存在） |
| 22 | `src/compile/gir/builder_control.cpp:1525-1542` `noteVariable`、`:2914-2945` `mergeArmVariables`、`:2837-2870`/`:3674-3700`/`:4081-4105` 三处 `capturedWrites` 出参 | 分支/循环臂改过的变量合并回 JOIN/LOOP | **改写**（臂的写直接成为分支/循环的结果；`capturedWrites` 出参删） |
| 23 | `src/compile/gir/builder_state.cpp:1-290` | 整文件就是 `ClosureCaptureEscapes`（规则二） | **删掉**（见 §4） |
| 24 | `src/compile/gir/builder_diagnostics.cpp:47-160` `capturedWritesOf`/`reportIgnoredCapturedWrites`、`:210`（`:var/after`）、`:1120-1287` `markNonRetainingReads` | 「哪个闭包写了哪个捕获」的汇总；`IgnoredSideEffect` 里 var 实参那一支；`markNonRetainingReads` 把读改成 `:var/peek`（**唯一引用证明**） | **删掉**（前两项）/ **改写**（`markNonRetainingReads`，168 行，证明要搬到值上） |
| 25 | `src/compile/gir/builder_resources.cpp:737-759` `addVarParameters` | `var` 形参被算作一个 `Variable` 类资源（`ReadModifyWrite`） | **改写**（写回仍然是调用者的效应，但不再是「单元」） |
| 26 | `src/compile/gir/captures.cpp:66-124`（`isVar` / cell 捕获 / `captures_[].variables`）、`:144-147`（`site.versions`）、`:175-178` | 捕获口传的就是单元；捕获口带有版本 | **改写**（捕获=值；版本机制删） |
| 27 | `src/compile/gir/program.cpp:437-444`（`moduleVariableResources_`）、`builder_resources.cpp:219` `moduleVariableOf` | 顶层 `var` 的单元被当成资源 | **删掉**（顶层 `var` 新语义下是编译错误） |
| 28 | `src/compile/gir/builder.cpp:128-140`、`builder_helpers.h:425-440` | 入口/形参/捕获的端口类型是 `TupleType::cell(T)` | **改写**（端口就是值类型） |
| 29 | `src/execute/exec_plan.cpp:451-455` `isVariableOperator`、`:468-517` `cellEscapes`、`:519-567` `laterUsersAreOrderedAfter`、`:570-633` `cellOfNode`/`cellDeclaredHere`/`cellValueMayBeKept`、`:651-811` `planStorageReuse` | 执行计划靠「单元 + 它的用户」证明「没别人看见」，从而决定原地更新还是复制 | **改写**（≈360 行；证明改到 SSA 值的用户上。这条是作者新排第 5 项的前提） |
| 30 | `src/passes/trans/cml/cml.cpp:1153-1290, 1405, 2498, 2628-2660, 2732, 2956-3030, 3700-3720` | 把 GIR 打回 CML 时给 `:var/*`/`:op/assn*` 起名、找单元、拼回赋值 | **删掉 + 改写**（≈300 行） |
| 31 | `src/passes/trans/cpp/function_emitter.cpp:1904-2200`（`cellEscapes`@1938、`VarNew/VarTake/VarAfter` 分支） | C++ 后端的单元分配、`var` 形参传引用、`var_take`/`assn_owned` 的原地形式 | **改写**（≈300 行） |
| 32 | `src/passes/trans/cpp/cpp_types.cpp`（2 处 cell） | 单元类型在 C++ 类型里的映射 | **删掉** |
| 33 | `src/passes/opt/generic/generic.cpp:1591-1606` `readsVariable` | CSE 把「读一个单元」当成可能被写改掉的值 | **改写**（16 行） |
| 34 | `include/camel/runtime/graph.h:470`（`GCFuncRef::reserved`）、`src/runtime/graph.cpp:2968`、`draft_construct.cpp:274,301,310`、`draft_inline.cpp:1167`、`draft_clone.cpp:79,145`、`passes/sched/fastvm/compile.cpp:488`、`passes/sched/taskflow/taskflow.cpp:90`、`execute/macro/eval.cpp:978` | 一个 32 位的「这个函数写了它第 k 个捕获」掩码，被塞进 FUNC 的 payload，运行时按掩码取新版本 | **删掉**（9 处；闭包不再写外面） |

**分类计数：删掉 19 组、改写 12 组、不动 1 组（`ArmBuild::Touched` 那半套 SSA 合并）。**

---

## 2. 要删的算子与要改的遍

先给结论：**要删的算子不是 2 个，是 7 个 `:var/*` 算子 + 47 个 `:op/assn*` 执行器 + 6 个 `:var/*` 执行器
（注册表 59 条）+ 一个 cell 类型构造器**；
`src/passes/opt/generic` 几乎不用动（只有 16 行），真正的大头在 builder 与两个后端。

| 区域 | 文件 | 要动的行数（量级） | 动作 |
|---|---|---|---|
| `src/builtin/operators` | `ops.cpp` 370、`ops.h` 52、`half_ops.cpp` 26 + `half_ops.h` 10、`set_value.cpp` 1 | **≈460** | 删 |
| `src/builtin/modules/globals.cpp` | 795-916 + `__assn_*__` 组 | **≈420** | 删 |
| `src/builtin/executors` | `builtin.cpp` 95、`builtin_semantics.cpp` 18 | **≈110** | 删 |
| `src/core`（类型 + 算子表） | `tuple.h/.cpp`、`text.cpp`、`collection.cpp`、`pure.cpp`、`typecall.cpp`、`typevar.cpp`、`resolver.cpp`、`operator_table.h`、`operator_names.cpp` | **≈90** | 删 + 改写 |
| `src/execute/exec_plan.cpp` | 450-811 | **≈360** | 改写（161 行是 `planStorageReuse`） |
| `src/passes/trans/cml/cml.cpp` | 7 个区段 | **≈300** | 删 + 改写 |
| `src/passes/trans/cpp` | `function_emitter.cpp` 1904-2200、`cpp_types.cpp` | **≈305** | 改写 + 删 |
| `src/passes/opt/generic` | `generic.cpp:1591-1606` | **≈16** | 改写（一处谓词） |
| `src/compile/gir/builder_*` | `builder_expr.cpp` 480（删 350 / 改 130）、`builder_control.cpp` 120、`builder_operators.cpp` 80、`builder_diagnostics.cpp` 210、`builder_state.cpp` 290（整文件）、`captures.cpp` 40、`builder_resources.cpp` 30、`builder.h` 90（声明）、`program.cpp` 10、`builder.cpp` 15、`builder_helpers.h` 15 | **≈1 380** | 删 + 改写 |
| `src/runtime` + 调度 | 上表 #34 的 9 处 | **≈40** | 删 |

合计 **≈3 200 行、30 个文件**，其中删掉约 **1 900 行**、改写约 **1 300 行**。

按算子说：`= 与复合赋值` 一共 **47 个执行器**（ops.cpp 37 + half_ops.cpp 10）、**6 个 `:var/*` 执行器**、
**59 条注册**（`builtin.cpp:161-254`）与 **49 条声明**（`globals.cpp`）；
`:var/new`、`:var/new/empty`、`:var/get`、`:var/peek`、`:var/take`、`:var/after`、`:op/assn_owned` 共 **7 个 id**。
`std::opt` 的规则库本身没有一条规则认 `:var/*`，所以优化遍的开销不在规则、在语法（CSE 的 16 行）。

---

## 3. 闭包捕获口

**今天怎么表示「按引用捕获」**，四个部件叠在一起：

1. **节点**：捕获口是一个普通端口节点（`captures.cpp:87` `graph->addPortNode(nodeTypeOf(origin), …)`），
   它的类型直接就是源的类型——源是变量时，那就是 `TupleType::cell(T)`。图上没有新的节点种类，
   靠**类型位**（是不是 cell）与端口角色（`appendWithPort` 是直接调用的 with 输入，
   `appendClosureNode` 是函数值的捕获槽，`captures.cpp:89-94`）区分。
2. **写集合**：`captures_[graph].written`（`recordWrite` 里 `builder_expr.cpp:665-670`），
   汇总到 `captures_[graph].variables`（`captures.cpp:121`）。
3. **掩码**：`captureWriteMaskOf` 把「第 k 个捕获会被写」压成 32 位，塞进 FUNC payload 的
   `GCFuncRef::reserved`（`builder.h:2250,2508-2510`；`graph.h:470`）。
4. **版本**：一次调用在返回值旁边多带回「每个被写捕获的新版本」（`callValueTypeOf`
   `builder_expr.cpp:699-760`、`CallValue`/`callValues_`、`writeBeforeCall_`、`site.versions`
   `captures.cpp:144-147`），`valueVersionOf` 读回。

**删掉它要动 22 处、约 230 行**：
`captures.cpp` 6 处（66、87、119-124、144-147、164、175）、`builder.h` 3 组（1019-1025、2489-2510、2250）、
`builder_expr.cpp` 4 段（665-670、673-697、699-760、762-810）、`builder_operators.cpp`
（`callWriteMasks_` 解包 4530-4590，≈60 行）、`builder_control.cpp` 3 处 `capturedWrites` 出参、
`builder_diagnostics.cpp` 2 处（47-160）、运行期 9 处（`graph.h:470`、`graph.cpp:2968`、
`draft_construct.cpp:274,301,310`、`draft_inline.cpp:1167`、`draft_clone.cpp:79,145`、
`fastvm/compile.cpp:488`、`taskflow.cpp:90`、`macro/eval.cpp:978`）。
另外 `builder_state.cpp` 里靠 `typeCarriesFunction` 判「这个体能不能把函数交出去」的那套（290 行）整份删。

---

## 4. `ClosureCaptureEscapes` 换成「创建后到最后一次用到不得再赋值」

### 4.1 新检查要读的信息，现有的有没有

| 新检查需要 | 现有的东西 | 有 / 没有 |
|---|---|---|
| 哪个闭包捕获了哪些变量 | `captures_[graph].variables`（`captures.cpp:121`）、`captureOriginOf_`、`captures_[].written` | **有** |
| 闭包的创建点 | `captures_[graph]` 建立时就是创建点；`registerNodeOrigin` 有源码位置 | **有** |
| 创建之后有没有写这些变量 | `recordWrite` 的顺序记录 + `pendingUses_` + draft 节点 id 的单调序（`builder_diagnostics.cpp:1206-1277` 就是「`r` 到它的 last user 之间有没有写这个变量」的现成写法） | **有（形状可复用）** |
| 闭包值的「最后一次用到」 | **没有**。builder 只有 `normUsersOf`/`withUsersOf`（节点级图边），`exec_plan` 的 `users`/`position` 是冻结后的线性序，都没有「按书写顺序的最后一次使用」 | **没有，要新写** |
| 闭包有没有逃出（装进结构体/数组/`Ref`/返回值/交给 `uses` 说会保留的被调函数） | `builder_state.cpp:106-233` `carryOutOf`/`carriedOffAt` 逐条实现过这三种情形，`bodyResultCanHoldAFunction`、`typeCarriesFunction`、`sem::writes(...)+ResourceClass::Variable` | **有（代码可搬）** |
| 循环体里每轮是新绑定 | `ArmBuild` 的 `bodyId`/`round` 区段（`builder.h:1095-1128`）已有「本轮」的概念 | **有（要接上）** |

结论：**四种「有」、一种「没有」**。新的部分是一个按书写顺序的「值的最后一次使用」走查，
形制与 `markNonRetainingReads`（`builder_diagnostics.cpp:1120-1287`，168 行）同构，估计 **120–150 行新代码**，
再搬 `carryOutOf` 的三种情形（约 60 行），删 `builder_state.cpp` 的 `cellsOfBody`/`checkCellEscapes`（约 130 行）。
`markNonRetainingReads` 本身在第一项落地后也要改（读不再是 `:var/get`），两件事应当同批做。

### 4.2 旧诊断有几条用例依赖

- 消息表：`src/core/error/diagnostics/messages/semantic.cpp:1824-1827`（名字、一行摘要、长帮助），
  常量 `semantic.h:489` `ClosureCaptureEscapes = 0x000210`。
- **plan 条目 12 条**：`test/plans/feat/compile/state_escape.plan.toml` 5 条、
  `test/plans/feat/opt/adb67e081.plan.toml` 2 条、`test/plans/feat/opt/e9.plan.toml` 2 条、
  `test/plans/feat/opt/inline_cycle.plan.toml` 3 条。
- 用例文件 5 个：`test/cases/compile/state_escape/err_escape_{return,field,array,stored_by_callee,into_closure}.cml`。
- 另有 1 个正例 `ok_capture_within.cml` 依赖它的「允许」那一半（闭包不离开那次求值）。
- 注意 `test/cases/effects/err_capture_escapes.cml` 走的是**另一条**诊断（`pure` 被破），不在这 12 条里。

---

## 5. 会变输出的用例

### 5.1 口径与三个文件数

| 口径（正则，`test/cases`，3775 个 `.cml`） | 文件数 |
|---|---|
| 形参位置出现 `var`：`[(,]\s*var\s+\w+` | **83**（另用括号配对扫 `func` 头，得 82，差 1 是函数类型里的 `var` 形参） |
| 声明或形参出现 `var`：`\bvar\s+\w+` | **301** |
| 只出现 `var` 这个词（含注释）：`\bvar\b` | 312 |
| `<var …>` 上下文参数 | 31 |
| `uses … var …` 资源子句 | 26 |
| 上述三类参数的并集 | 122 |
| 有类型书写的 `var x:`（声明或形参） | 146 |

**关于 138 与作者写的 75 / 281**：用任何一条单一、可写下来的口径都复现不出 138。
最接近的三个候选是 146（`var x:` 有类型）、122（形参 ∪ `with` ∪ `uses`）、83（形参位置）。
作者的 75 / 281 与我在**同一个提交**（`e3447538c` 本身）上量的 83 / 306 也不同，
所以那是对另一个口径（例如只数「能编译的程序」、或排除 `err_` 用例）量的；
按 `err_` 前缀排除后是 77，仍差 2。**建议采用 83 / 301（形参 / 声明），并把口径写进迁移条目。**

按 plan 条目算（6421 条带 `case` 的条目）：**394 条的用例文件有 `var` 形参、848 条出现 `var`**。集中处：
`linear/var_reference` 252 条（232 有 `var` 形参）、`linear/captured_writes` 52 条（35）、
`linear/toplevel_var` 34 条（6）、`linear/closures_and_state` 24 条（15）、`compile/effects_syntax` 55 条（8）、
`linear/var_in_sync` 13 条（6）、`consistency/new_values` 35 条（18）。

### 5.2 闭包写外层 `var`：34 个文件

按「lambda / 嵌套 `func` 的块体内给外层 `var` 赋值」扫（排除 `case … =>` 的 match 臂与 `static for … =>` 的
编译期展开），**34 个文件**；含 `static for` 是 39，含 match 臂是 41。

改写方式与文件数（合计 34）：

- **改成 `Ref`：13 个**（闭包离开这次调用：返回、存进结构体字段/数组元素、装进另一个闭包、交给会保留它的被调函数）
  `compile/state_escape/err_escape_{array,field,into_closure,return,stored_by_callee}.cml`、
  `effects/err_capture_escapes.cml`、`linear/byref/var_capture_reference.cml`、
  `linear/toplevel_var/argument_order.cml`、`modules/tensor/var_tensor_reference.cml`、
  `opt/adb67e081/rc04_seed72.cml`、`opt/e9/rc04_seed72.cml`、`opt/inline_cycle/seed23_fold_inline.cml`、
  `tools/grammar/functions.cml`
- **改成循环状态或返回值：11 个**（闭包在本函数内被调用，写的是本函数的 `var`）
  `compile/state_escape/ok_capture_within.cml`、`effects/ok_capture_local_var.cml`、
  `effects/ok_capture_local_var_as_value.cml`、`consistency/attempt/effects.cml`、
  `consistency/entering/assignment.cml`、`consistency/values/closure_var.cml`、
  `consistency/new_values/sort_rebinding.cml`、`linear/byref/lambda_capture_ignored.cml`、
  `opt/adb67e081/rc04_seed72_within_call.cml`、`opt/inline_cycle/seed23_fold_inline_within_call.cml`、
  `trans/cpp/exec/variables.cml`
- **改成 `fold`：10 个**（高阶算子 `foreach`/`map` 的闭包里写外层累加量）
  `consistency/faults/fault_in_foreach_closure.cml`、`consistency/new_values/peek_loop.cml`、
  `consistency/new_values/rebinding_loop.cml`、`feat/mm/capacity/perm_query_types.cml`、
  `feat/mm/gc/gc_heap_workload.cml`、`feat/mm/gc/gc_nested_foreach.cml`、
  `linear/jit/lambda_points.cml`、`linear/nvm/lambda.cml`、
  `opt/progen/findings/inline_jit_arm_value_float.cml`、`trans/cpp/named_structs.cml`

其中 `rebinding_loop.cml` 与 `perm_query_types.cml` 写的是 `var` **形参**，也可以算「形参改返回值」那一档；
`sort_rebinding.cml` 的闭包只调用一次，也可以直接改成返回值。三种改法之间有 2–3 个文件可互换。

### 5.3 `stdlib` 的两个形状

- **`stdlib/stream.cml:181` `tee`**：4 个 `var` 局部（`src`、`buffer`、`taken`、`peak`），
  两个**嵌套 `func`**（`pull`、`read`）在里面写 `buffer`（198）、`src`（199）、`peak`（219）。
  形状是「同一函数里的两个闭包共享可变缓冲」，闭包只在本调用内被调用 → **改 `Ref`**（作者已定）。
  另有 `read` 返回流、`unfold` 递归等，`tee` 是 `stdlib` 里唯一一个「嵌套函数写外层 `var`」的文件。
- **`stdlib/perf.cml:9-10`**：`with <var result: float[], repeat: int> export func timeit(...)`，
  体内 `result = result->append<duration>`，并**部分应用**成 `timeit<result, repeat - 1>(fn)`。
  形状是「`var` 上下文参数 + 部分应用」，按新裁定两条都不合法（不能部分应用 `var` 上下文参数）→ **改返回值**。
- `stdlib` 52 个 `.cml` 里出现 `var` 的 9 个（`ckpt`、`graph`、`perf`、`process`、`random`、`state`、`stream`、
  `test_support`、`tiles`），只有上面 2 个需要改。

---

## 6. golden 与 `ast_hashes`

- `test/plans/feat/parse/golden/ast_hashes.txt`：**4280 行，会变 ≈36 行（0.8%）**。
  理由是这张表按文件记「规范化 AST 或拒绝时的诊断（按 code + 位置，不按措辞）」，所以
  **只要不动语法，`var` 的 AST 一个字都不变**；会变的只是：34 个被迁移的用例（源码改了）+ 2 个 stdlib 文件。
  如果第 1–3 项顺手改了 `var` 形参的 AST 表示，则要按 301 个含 `var` 的文件重录——**技术上不需要，别这么做**。
- `test/plans/**/golden/*.dot`：共 **30 个**，会变 **3 个**：
  `test/plans/feat/opt/golden/camel_passes.cse.dot`（源 `cases/opt/camel_passes/cse_subject.cml`）、
  `test/plans/feat/opt/golden/camel_passes.cse_written_variable.dot`（源 `cases/opt/generic/written_variable.cml`）、
  `test/plans/feat/docs/fixture/images/10_var_order.dot`。
  这正是 30 个 `.dot` 里含 `:var/`/`:op/assn` 的全部 3 个；其余 27 个的源程序不含 `var`，节点编号不会动。

---

## 7. 能不能一条车道做完；切点在哪

**能，但不能一次入库。**第 1–3 项彼此咬住：今天的 `var` 形参写回**就是**闭包捕获写那套机制
（`callValues_` + `versions` + `GCFuncRef::reserved`），所以「删捕获口」与「形参降成多返回值」不能分两次顺序反过来做；
而「删本地单元」又必须与「闭包写外层是编译错误」同时落地，否则 34 个用例当场变红。

建议 **3 个必做切点 + 1 个可选切点，每个单独入库后全量不新增红**：

| 切点 | 内容 | 为什么这样切还是绿的 |
|---|---|---|
| **C1（只动 test/，不动 src/）** | 把 §5.2 的 34 个用例按 13 `Ref` / 11 循环状态或返回值 / 10 `fold` 改写，把 2 个 stdlib 文件改写；把 12 条 `ClosureCaptureEscapes` 的 plan 条目改成新期望（或迁走） | 改写后的程序在**今天的编译器**下语义等价（`Ref`、循环状态、`fold` 都是今天已有的写法），输出不变；`err_escape_*` 从「拒绝」变成「通过」只改条目不断言 |
| **C2（第 1 项 + 第 3 项的规则）** | 删本地单元（`collapseNonEscapingVariableReads` 反向：全部 SSA）、捕获按值、加入 3.5 的「创建后到最后一次用到不得再赋值」检查、删 `builder_state.cpp` 与 `ClosureCaptureEscapes`；分支的写成为分支结果、循环体的写成为循环携带状态（`ArmBuild::Touched` 已有） | C1 之后语料里已经没有「闭包写外层」，剩下的写入都在同一函数内，SSA 化不改变任何输出；`var` 形参**仍保留单元**，所以调用处写回的行为不变 |
| **C3（第 2 项）** | `var` 形参/上下文参数降成多返回值：调用处写回、实参必须是以 `var` 名为根的路径、同一次调用两个同根实参报错、`Ref` 式掩码/版本机制整段删（`capturedWrites`、`reserved`、`callValues_`、`:var/after`）；两条验收（别名下先读后写、`sync` 块外的形参写）按书写顺序给出结果 | 这一刀才改 `linear/var_reference`（252 条）与 `linear/captured_writes`（52 条）的期望；两条验收是**从错到对**，属于预期内的期望更新 |
| **C4（可选，第 5 项的前提）** | `exec_plan` 的 `planStorageReuse` 从「单元 + 用户」改成「SSA 值 + 用户」，`markNonRetainingReads`/`:var/peek` 随之删掉 | 只影响原地更新与复制计数，不改语义；`camel inspect --copies` 的读数在这里变准 |

**一句话**：第 1–3 项一条车道能做，但必须按 **C1 → C2 → C3** 的顺序（C2 = 第 1 项 + 第 3 项的规则，
C3 = 第 2 项），三个必做切点分别约 **36 个测试文件 / 2 000 行 src / 1 100 行 src**，C4 另约 530 行；
把第 5 项（计数做准）留给另一条车道，但它与 C4 必须排在 C3 之后，否则 `var` 形参的原地写回没有证明可依。

---

### 附：本次测量用到的命令与文件

- `git show origin/fsh-dev:docs/design-principles.cn.md`（3.5 在 186-207 行）
- `git show origin/fsh-dev:internal/reviews/questions-2026-10-29.md`（3751-3766 行）
- 计数脚本口径：`test/cases` 3775 个 `.cml`；`test/plans` 里 6421 条带 `case` 的 `[[tests]]`；
  `ast_hashes.txt` 4280 行；`*.dot` 30 个。