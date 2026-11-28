# ①「建图一次到位」第一笔：要删什么、换成什么、拿什么比

2026-11-28 · `lane/progen` · 由 `D-1128-180` 引出 · 承接 `internal/plans/2026-11-28-refactor-not-patch.cn.md`（`fsh-dev@68fcfc6a57`；`origin/integration` 尚未含它，本页按该提交的原文对齐：①「建图一次到位（架构第 8 节第 4 步）」）

**本页不是修 seed 71。** 决策者原话：`seed 71` 的（甲）不做 —— 因为 seed 71 的坏边正长在要被删掉的机制上。

## 结论先行（一句话）

> **坏边不是建图函数建错的，是建完之后被一次用处替换换进去的 —— 这正是"建完再改接"这套机制要删的理由。**

## 证据（今天三轮读数的产出，全部取自 `lane/progen` 上的临时打印，打印已删净、未提交）

1. **块收尾值门的构造**：`src/compile/src/gir/builder.cpp:2121-2147` —— `:2136 addGateNode(nodeTypeOf(res))`、`:2137 linkNodes(Norm, res, gate)`、`:2144 writeSyncStatement(lastSyncedNode_, gate)`、`:2145 res = gate`。打出来三个块各建一次门：

```
[块门] blockgate res=14 tail=12 armRegion=1
[块门] blockgate res=19 tail=18 armRegion=4
[块门] blockgate res=21 tail=1  armRegion=0
```

2. **关键读数**：else 臂（`armRegion=4`）的门**建好时读的是它自己的 `res=19`**；而最终图里那个门读的是 `4` —— arm 0（region 1）里的 `:op/copy`。⇒ **4 是事后被塞进门里的**，不是建门时接错。

3. **排除**：`builder_control.cpp:4500-4537`（`:4502 gatedValue = addGateNode(...)`、`:4504 outputAnchor = gatedValue`、`:4536 arm->output = outputAnchor`）只在 `activeStaticBody_`/`activeRestBody_`/`ActiveArm` 的 **`return`** 分支里跑；else 臂的体 `sync { println(''); null; }` 没有 `return` ⇒ 不触发。

4. **指向**：改接用处的接口只有一族 —— `GraphDraft::replaceUsesInList`（`src/kernel/runtime/draft.cpp:2359`），四张边表各一条：`:2447`（`NormalPorts`）、`:2455`（`ContextPorts`）、`:2463`（`Ctrl`）、`:2471`（`Order`）。**不再打第三轮打印**：定位到行不产出修法，只让要删的东西更清楚。

## 一、要删的清单
### 1.0 清单的基线（可复核）

读数 tip **`origin/integration@27aea1ba45`**（`lane-parked-rebase` 打的），**只读**，命令形如 `git grep -n "cell|Cell" origin/integration -- src/compile`：

| 目标 | 命中 | 明细 |
|------|------|------|
| `cell|Cell`（`src/compile`） | **749 行**（代码 **234**、注释 **515**） | 代码行分布：`gct/builder_func.cpp` **87**、`gir/builder_variables.cpp` 48、`builder_control.cpp` 21、`builder_call.cpp` 19、`builder_resources.cpp` 10、`builder_expr.cpp` 8、`captures.cpp` 8 … |
| `collapseNonEscapingVariableReads` | **11 处** | 定义 `builder_variables.cpp:347`、声明 `builder.h:1225`、调用 `builder.cpp:620`，其余为注释 |
| `dropCarriedVariableUnits` | **8 处** | 定义 `builder_variables.cpp:714`、声明 `builder.h:1233`、调用 `builder.cpp:624`，其余为注释 |
| `eraseUnreadNodes`（全 `src`） | **17 处** | 声明 `draft.h:897`、定义 `draft.cpp:2086`、compile 侧 `builder_variables.cpp:399/910/951`、`draft_outline.cpp:346`、`test/.../draft_erase_unread.cpp:84/100/115` |

以后复核"清单纯不纯"，照这张表重跑一遍同样的 grep 即可。



### 1.1 格子（`var` 的运行时代表）

| 位置 | 是什么 |
|------|--------|
| `src/compile/src/gir/builder_variables.cpp:61` `visitVariNode` | `var` 的入口：建格子、登记 `knownCellUsers_`/`writesOf_` |
| `:216 newVariable` / `:224 newVariableOfType` | 造 `:var/new`（格子本身） |
| `:237 readVariable` / `:241 readVariableThrough` / `:339 peekVariable` | 造 `:var/get`（读格子） |
| `:112 variableOperator` | 造 `:var/*` 算子节点 |
| `:182 variableAfterCall` | 调用之后取回格子 |
| `src/compile/src/gir/builder.h:1101 knownCellUsers_` / `:1106 writesOf_`（及 `cell` 相关注释 `:645`、`nodeModifierMaps_`） | 格子的账：谁是读者、谁写过 |
| **``src/compile/src/gct/builder_func.cpp`` 87 行**（`pcCell`/`readCell`/`writeCell`/`addCellOfType`/`HoistedCell`/`CellDecl`/`cells`，`:590/:604/:605/:660/:670/:678/:714/:719/:724/:744` 一带） | **列入清单，归第二笔**：生成器层的同一套格子。理由见下 |


### 1.2 三处抹除

| 位置 | 是什么 | 调用点 |
|------|--------|--------|
| `builder_variables.cpp:347` `collapseNonEscapingVariableReads` | 折叠：把格子的读换成"写写下的值"（"flip" 那句散文在 `:860`，**它不是一步运行期动作**：`:op/copy` 只由 `copyNodeOf` 造，`builder_control.cpp:268`，调用点 `builder_expr.cpp:114/371/1602`、`builder_literal.cpp:765`） | `builder.cpp:619-620` |
| `builder_variables.cpp:714` `dropCarriedVariableUnits` | 抹掉没人读的格子单元（含 `:848-891` 的 doomed 集合、`:903-912` 的 `eraseUnreadNodes(going)`、`:931-958` 的逐个抹） | `builder.cpp:624` |
| `src/kernel/runtime/draft.cpp:2086` `GraphDraft::eraseUnreadNodes` | 抹除的许可（"集合外还有谁读它就保住整个单元"），**连同它做的"把位置交出去 + 拼接顺序"** | `builder_variables.cpp:399/910/951`、`draft_outline.cpp:346` |

### 1.3 `var` 的三种降低

1. **不逃逸的局部 `var`** → 折叠成 SSA（`collapseNonEscapingVariableReads`），再抹掉单元（`dropCarriedVariableUnits`）。
2. **逃出当前函数的 `var`**（被闭包捕获）→ 保留格子，靠 `state` 的 `Ref`/`get`/`set`（`captures.cpp:195/327` 那两处注释说的就是它）。
3. **循环携带 / 在臂里被写** → 交给 loop 的携带状态与 `arm->output`/`readArmCellValue`（`builder_control.cpp:862-899`、`:2212 addArm`）。

架构第 8 节的原话（`docs/design/architecture/abstractions.cn.md`，"## 8. 前端：阶段各管一件事"，第 172 行起）：

> **管什么**：从源码到图。今天 GIR 建图器在一个阶段里同时做名字解析、类型推断、重载选择、泛型实例化、静态展开、捕获分析、`var` 降低与效应测量，一个类约 160 个数据成员。
>
> **阶段与它唯一的决定**（负责人 2026-11-22）：……
> 4. 建图：类型完整的树 → 图。这一步只做机械的翻译，不做决定；`var` 只降低成 SSA（原则 3.5），`Ref` 是唯一的单元；生成器只降低一次。
>
> **按强连通分量推进**：……逐个分量推进各阶段。

计划页同一句（`internal/plans/2026-11-28-refactor-not-patch.cn.md`，"1. 建图一次到位（架构第 8 节第 4 步）"）：

> **目标**：`var` 只降低成 SSA。每次赋值是一个新值，分支与循环在汇合处交出各自的版本。逃逸的情形照原则：可变形参是值上的写法，调用交回新值；闭包要共享可变状态，用 `Ref`。不建格子，就没有抹除；建图也不产出什么都不等的关卡。
>
> **删**：格子与三处抹除；那一位记账位与懒整理路径；`var` 的其余降低（架构里说有三种）。
>
> **方法**：照架构第 8 节的"迁移靠比对"，新旧建图编译同一批程序，比较规范形的图。

### 1.4 `kGraphFlagGatesDissolved`（`lane-editview-commit` 正在删，**本车道只列不动**）

本枝实际命中 **4 处**（不是 5 处，第 5 处是 `test/plans/feat/.../cxx_isolation.plan.toml:34` 的注释（归 `lane-editview-commit`））：

| 位置 | 是什么 |
|------|--------|
| `src/kernel/include/camel/runtime/graph.h:247` | 定义 `kGraphFlagGatesDissolved = 1 << 1` |
| `src/kernel/runtime/draft_session.cpp:1727` | 读：`if (!drafted && (graph->flags() & kGraphFlagGatesDissolved))` |
| `:1757` | 写：`GCGraphBuildAccess::addFlags(graph, kGraphFlagGatesDissolved)` |
| `:1808` | 写：`draft.setFlags(draft.flags() | kGraphFlagGatesDissolved)` |


### 1.5 生成器层的格子：与 `gir` 侧的格子是**同一套机制的两半**，一起删

`src/compile/src/gct/builder_func.cpp` 的 **87 行代码**：`pcCell`、`readCell`、`writeCell`、`addCellOfType`、`HoistedCell`、`CellDecl`、`cells`（`:590/:604/:605/:660/:670/:678/:714/:719/:724/:744` 一带）。

**`D-1128-182` ① 已定：并入①，而且不分两段。** 决策者原话：

> "var 的降低在目标里只有一处（架构第 8 节第 4 步，类型完整的树→图时直接降成 SSA），生成器层的 `CellDecl`/`HoistedCell`/`readCell`/`writeCell` 和 gir 层的格子与三处抹除是同一套机制的两半，先删一半，另一半还得把格子喂进来。"

⇒ **撤回**本页上一版"先删 `gir` 侧、`gct` 侧排在其后"的排法：那个排法正是"先删一半"，会把另一半的喂料问题留到以后。**两层格子一起删**，与三处抹除同一笔收尾（§四 比齐之后）。
### 1.6 终点：没有一类留格子

`D-1128-186` ①：顺序照旧，但**终点写死** —— **没有一类留格子**。决策者原话（逐条照抄）：

- **原则 3.5**：`var` 是**会被重新绑定的名字**，值本身不可变；这条**取代了"闭包对变量的捕获也是引用"**。
- 因此**闭包捕获的是捕获时的值**、**`var` 形参是调用交回新值**，**"逃逸"这个类别本身会消失**。
- 最后**一起删**：`escapingVariables_`、`collapseNonEscapingVariableReads`、`dropCarriedVariableUnits`、`knownCellUsers_`/`writesOf_`，以及 `gct` 的 `CellDecl`/`HoistedCell`。
- **语料里若有程序靠闭包共享修改一个 `var`，它违背的是原则** ⇒ **用例改写成 `Ref`，不为它留格子**。

**要点名的用例（`test/` 里靠"闭包/逃逸共享修改 `var`"这一套写的）**：

| 用例 | 现状 | 改成什么形状 |
|------|------|--------------|
| `test/cases/compile/var_closure/err_captured_var_put_into_an_array.cml` | 反例（期待诊断） | **不改成 `Ref`**：它验的就是"把捕获的 `var` 塞进容器"被拒；诊断在，用例照旧 |
| `test/cases/compile/state_escape/err_escape_into_closure.cml` | 反例（期待诊断） | 同上：逃逸反例，保留为诊断用例 |
| `test/cases/compile/state_escape/err_escape_array.cml`、`err_escape_field.cml`、`err_escape_return.cml`、`err_escape_stored_by_callee.cml` | 反例（期待诊断） | 同上，保留 |
| `test/cases/consistency/new_values/peek_loop.cml`、`rebinding_loop.cml` | **正例**：靠"重新绑定 = 新值"写 | **不改写成 `Ref`**：它们正是原则 3.5 的形状，应在新路径下**保持绿**；它们是"新值"语义的验收例 |

**结论**：这一族里**没有"正例靠共享修改"的用例**——共享可变状态在语言里只有 `Ref` 一条路（`import { Ref, ref, get, set } from state`），`var` 只能是"重新绑定"。所以 §1.6 不产生"把正例改写成 `Ref`"的清单，只产生两个动作：**(a) 反例保持为诊断用例；(b) 正例（`peek_loop`/`rebinding_loop`）作为新值语义的验收例必须保持绿**。

## 二、替换后的形状（`var` 只降低成 SSA）

**一句话**：每次赋值是一个**新值**；分支与循环在**汇合处**交出各自的版本；建图**不产出"什么都不等的关卡"**，因为不建格子就没人需要事后抹。

落到建图代码：

- **`visitVariNode`（`builder_variables.cpp:61`）变成两条绑定**：`var x = e` 把名字 `x` 绑到"版本 0 = `e` 的节点"，不造 `:var/new`；`x = e` 把 `x` 绑到"版本 n+1 = `e` 的节点"，不造 `:op/assn`。格子的账（`knownCellUsers_`/`writesOf_`）整块消失。
- **`readVariable`（`:237`）变成查版本**：返回 `x` 当前版本的节点，不造 `:var/get`。`peekVariable`/`readVariableThrough`/`variableAfterCall` 一并消失（它们的理由全是"格子"）。
- **分支汇合**：`buildBranch`（`builder_control.cpp:2163`）的 `addArm` 在臂建完时，问每个臂"你这一路上每个名字的**最后版本**"；两臂版本不同的名字，在 JOIN（`:2169-2170 addJoinNode/addBrchNode`）上落**一个按分支选的节点**（join 携带的那个值），而不是"读格子 + 事后改接"。⇒ **`arm->output` 只由 `setArmResult`（`:1103-1119`）从臂体自己的值定**，`readArmCellValue`（`:862`）随格子一起删。
- **循环携带**：循环体里被写、循环后再读的名字，成为 **LOOP 的显式携带版本**（进入时的版本 + 每轮回传的版本），**不需要 `dropCarriedVariableUnits`**：没有格子就没有单元可抹。
- **逃逸（被闭包捕获）**：仍走 `state` 的 `Ref`/`get`/`set`，但那是**语言层面的句柄**，不是建图内部的格子 —— 建图不再为它记账。


## 二·一 ① 的范围比"平直 `var`"大：简单情形**今天已经是 SSA**，要做的是把三类边界从格子改成版本

**这一条必须先读，否则会把①做成"改一遍已经对的东西"。**

**读数（`lane-ssa-residue` 交，`D:\Projects\Camel-wt\onnx-run@42889bcde9`，只读）**：

- **调用面很小**：`visitVariNode` **1 个调用者**（`builder.cpp:828` 的 visit 分派臂）；`newVariable` 3（`builder_expr.cpp:515` 绑定、`builder_call.cpp:3011` `var` 实参、`builder_generator.cpp:248` pc 格）、`newVariableOfType` 1（`builder_expr.cpp:434`）、`readVariable` 6（`builder.h:3219`、`builder_call.cpp:503`、`builder_calls.cpp:313`、**`builder_control.cpp:3170` 循环入口**、`builder_operator_writes.cpp:441`、**`captures.cpp:328` 捕获源**）、`peekVariable` 1、`variableOperator` 4、`variableAfterCall` 3。
- **逃逸的判据不在那两个函数里**：`builder.h:1087-1095`（原文："captured by a closure, handed out as a `var` argument, or a `var` parameter of this graph -- keeps its cell"），`escapingVariables_` 由 `builder_calls.cpp:1004` 与 `captures.cpp:188` 在**别的文件、别的时机**填；**捕获**在 `captures.cpp:117-129/183-213`；**循环携带**在 `builder_control.cpp:2449`（"A write inside a loop is carried state"），登记在 `:2587 carriedVersions_[variable] = joinNode` 与 `:2849`。
- **账很小、没有第三方读者**：`knownCellUsers_`（`builder.h:1101`）写 2（`builder_expr.cpp:684`、`builder_variables.cpp:312`）/删 1（`builder_variables.cpp:919`）/读 1（`builder_variables.cpp:491` 折叠资格判据）；`writesOf_`（`builder.h:1106`）写 1（`builder_expr.cpp:586`）/删 1（`builder_variables.cpp:918`）/读 2（`builder_variables.cpp:544`、`:808`）。

**结论（照读数写）**：**平直的 `var` 今天已经被折叠成 SSA**（`variableReadSource_`（`builder_variables.cpp:311`）+ `writesOf_` 在折叠那一趟做掉了）。所以①**不是**"把平直的改掉"，而是：**把逃逸／循环携带／被捕获这三类的边界，从"格子"改成"版本"** —— 而这三类的边界正是在：

- **臂的结果**：`addArm`（`builder_control.cpp:2212`）/`setArmResult`（`:1103-1119`）——臂交出的是"值"还是"读格子"；
- **循环携带**：`carriedVersions_[variable] = joinNode`（`builder_control.cpp:2587`，另见 `:2849`）；
- **捕获**：`captures.cpp:117-129/183-213` 与 `escapingVariables_` 的填充点（`builder_calls.cpp:1004`、`captures.cpp:188`）。

⇒ 因此**"只动 `visitVariNode` + `readVariable` 两处"做不到**：第二刀（`addArm`/`setArmResult`）是必需的。这一条把 §二 的形状与"开工第一笔"的范围一起改了：**第一笔只能覆盖"既不平直也不需要第二刀"的那一小块，其余必须等第二刀**。

### 2·0 落点更正（读代码后纠）：`visitVariNode` 不建格子

**页里 §二 原先写"`visitVariNode`（`builder_variables.cpp:61`）变成两条绑定"，位置不准。** 读原文（`builder_variables.cpp:61-72`）：它只做四件事 —— 置 `varied_`/`declaresVariable_` 两个标志、访问初值（`visit(gct->at(0))`）、还原标志、返回。**它不调 `newVariable`、不造 `:var/new`。**

真正的落点：

| 动作 | 落点 |
|------|------|
| `var` 绑定时建格子 | **`src/compile/src/gir/builder_expr.cpp:515`**（调 `newVariable`） |
| 写的记账 | `builder_expr.cpp:586`（`writesOf_`） |
| 读格子 | `builder_variables.cpp:237 readVariable` |
| 折叠（今天做的活） | `builder_variables.cpp:347 collapseNonEscapingVariableReads` |

**第一刀的真实形状 = 惰性绑定 + 物化**（因为"平直"在建图那一刻不可知）：

- `var x = e`：**绑一个版本**（名字 → `e` 的节点），不建格子；
- 写 `x = f`：命中版本绑定时**改绑**到 `f` 的节点，不造 `:op/assn`；
- 读 `x`：命中版本绑定时**返回版本节点**，不造 `:var/get`；
- **物化点**：读/写发生在"已不平直"的上下文（臂内、循环体内、闭包体内 —— 用一个纪元计数器判定）时，先用当前版本把 `:var/new` 建出来、灌进去，**之后走老路**（`escapingVariables_`、`carriedVersions_` 一行不碰，那是第 2–4 刀）。
- **判据的两种可能**（先量再定，见 §3.8）：**甲** = 今天平直 `var` 的 canonical 里本来就没有 `/var/get`（`variableReadSource_` 那套已折掉）⇒ 逐字节相同成立；**乙** = canonical 里有这些节点 ⇒ 第一刀只能"节点照旧造、机制换掉"，删格子留第二刀。

## 二·二 迁移期并存：编译期开关，运行期只有一条路

新路径（从类型完整的树直接降成 SSA，不建格子、不抹除）**与旧路径并存**，但并存是**编译期选择**：一个临时常量（例如 `src/compile/src/gir/builder_variables.cpp` 顶部的 `constexpr bool kVarLoweringSsa = false;`），**运行期只有一条路**，没有任何运行时分支。

**页与提交体都要写同一句**：*迁移期开关，用"迁移靠比对"（架构第 8 节）比齐之后，连同旧路径一起删* —— 开关不是长期配置项，它存在的时间 = 迁移的时间。

比齐之后，§一 里那三层一起删：`gir` 的格子（1.1）、三处抹除（1.2）、`gct/builder_func.cpp` 那 87 行（1.5）。

## 二·三 比对话料

| 语料 | 规模 | 为什么它在语料里 |
|------|------|------------------|
| `progen` | **100 个程序**（`test/tools/progen`，`test/plans/feat/opt/progen.plan.toml`） | 生成器专造"边界形状"：臂里的 `var`、循环携带、闭包捕获、分支值汇合。今天的两条红（seed 11、25）与昨天的两条（seed 71、39）都出自它 |
| `examples/` | `test/plans/feat/examples.plan.toml` 里的全部用例 | 人手写的小程序，覆盖语言表面（`var`、`if`、循环、闭包、`match`），是"新路径不能把常见写法做坏"的第一道 |
| `feat/compile` | `test/plans/feat/compile/**` 全部用例 | 编译器的自家用例：诊断、类型、期望行为都在这儿；`var` 降低换路径最容易碰坏的就是这一批 |
| **臂里写 / 循环体里写 / 被闭包捕获**的 `var` 最小程序（各一条，人手写） | 3–6 个程序 | **这才是新旧路径真的会出差异的地方**：平直 `var` 今天已折叠成 SSA（§二·一），差异只会出现在这三类边界上；它们各自正对 `addArm`/`setArmResult`、`carriedVersions_`、`captures.cpp`。**同一条规矩：归不了因算新路径的错** |
| **`foreach` 作用在 `Range` 上**（`(0 .. 3)->foreach<show>`，9 行） | 1 个程序 | 每日审查第 2 条点名的形状：`builder_call.cpp` 的 `withParameterOfSelectedEntry` 在 `signatureForArgumentContext` 之外再扫一遍候选条目，**第二条"选哪个条目"的路径在这里被走到**。今天它是**绿的**（读数见下）⇒ 它是**护栏**：新旧路径都要绿，且两条路径若给出不同条目，按 §3.4 归因 |

## 三、比对方法（可执行步骤）

### 3.0 地基验证（`std::jgir canonical=true` 能不能当基准）

**读数（`lane-kinds-oper` 测得，原文照抄）**：5 个程序 × 2 遍、**同一个二进制**：

| 程序 | 遍 1 / 遍 2 | 结论 | 指纹 |
|------|-------------|------|------|
| `broadcast_like_spreads_rank_zero` | `0/0` | SAME | `21D09512588B4D36` |
| `closed_rank_rejects_rank_zero` | `1/1` | SAME | `7D09A21BF1F6A3AD` |
| `err_lambda_tuple_arity` | `1/1` | SAME | `CE9DD7603B837C1E` |
| `hello` | `0/0` | SAME | `9806026B430A1872` |
| `binary_search` | `0/0` | SAME | `F6EA1AEEEAEF4702` |

- 换 cwd 跑 `hello` / `binary_search` **亦 SAME**。
- 产物里**时间戳 0、临时目录名 0、随机 id 0**（`[0-9a-f]{16,}` 命中的全是 `"bits": "0x…"` 的浮点位模式，属内容）。
- **唯一要归一的一样：源文件绝对路径** —— 失败程序首行 `--> D:\Projects\Camel-wt\coord\test\cases\compile\…cml:11:21`（另一例 `:8:23`）⇒ 归一到"**文件名 + 行:列**"（去掉树前缀）。

**前提必须如实读**：这次用的是 `D:\Projects\Camel-wt\perf\out\latest\bin\camel.exe`（**23.4 小时前**构建，`coord` 树没有构建）——**它不是当前尖的产物**。迁移比对时两个构建都会各留 `build-info.json`（§3.1），**基准是否稳定要以那次的读数为准**；本次只证明**同一二进制**下产物可重复、与 cwd 无关。

### 3.1 两个构建

1. **旧路径构建**：`kVarLoweringSsa = false`，`npm run build`（或 `camel-remote.ps1 -Path <wt>`），把 `out/latest/bin/camel` 与 `out/latest/libs` 拷到 `out/migrate/old/`。
2. **新路径构建**：`kVarLoweringSsa = true`，同上，拷到 `out/migrate/new/`。
3. 两次构建的 `out/latest/build-info.json` 各存一份（`out/migrate/{old,new}/build-info.json`），归因表里要写这两份的 fingerprint。

### 3.2 同一批程序 × 规范形产物

对语料里**每一个程序**，各用两个二进制跑同一个遍，把产物落到 `out/migrate/graphs/{old,new}/<case>.jgir`：

```powershell
$camel = "out/migrate/old/bin/camel"   # 第二轮换成 new
& $camel --diag-format json <case>.cml std::jgir canonical=true | Set-Content "out/migrate/graphs/old/<case>.jgir"
```

`std::jgir canonical=true` 是现有遍（`src/stdlib/std.cml:335` 的注释原文就是"with `std::jgir canonical=true` and compared byte for byte"），**不新造工具**。

### 3.3a 比对前归一（**只有这一条**）

**只把输出里出现的"源文件绝对路径前缀"换成一个固定串**（`文件名 + 行:列` 保留），例如把 `D:\Projects\Camel-wt\<树>\test\cases\compile\x.cml:11:21` 归成 `<src>/x.cml:11:21`：

```powershell
$raw = Get-Content $_.FullName -Raw
($raw -replace '(?<p>[A-Za-z]:\\[^\r\n:]*\\)(?<f>[^\\:\r\n]+:\d+:\d+)', '<src>/$2') | Set-Content $out
```

**判据：归一规则只有这一条。** 其余**一个字节都不许归一** —— 多归一一样（空白、顺序、数字）就会把真正的差异洗掉，比不归一更坏。归一之后的 hash 才算比对用的 hash。

### 3.3 逐字节 diff

```powershell
Get-ChildItem out/migrate/graphs/old/*.jgir | ForEach-Object {
    $n = $_.Name
    if (Compare-Object (Get-Content $_.FullName) (Get-Content "out/migrate/graphs/new/$n")) { $n }
} | Set-Content out/migrate/differing.txt
```

### 3.4 归因表模板（每个差异程序一行）

| 程序 | 差异处（节点/边/区域，贴原文两行） | 归因 | 依据 |
|------|-----------------------------------|------|------|
| `<case>` | `old: …` / `new: …` | **新路径的错** / **旧路径的缺陷**（如 seed 71、39） / **归不了因** | 一句 |

**规矩：归不了因的一律算"新路径的错"**，不许把"不清楚"记成"旧路径的毛病"。旧路径的缺陷要归因成立，必须能指着本页 §一 里某条要删的机制说"它是那个机制产出的"。

### 3.5 命令清单（照着能复现）

```powershell
# 0) 语料：progen 100
python test/tools/progen/progen.py show --seed <n> > out/migrate/corpus/progen_<n>.cml   # n = 1..100；参数名以 `show --help` 为准
#    examples / feat/compile：取各自计划里点名的 case 源文件，逐个拷进 out/migrate/corpus/
node scripts/test.js test/plans/feat/examples.plan.toml --jobs 1        # 仅确认这批用例本身此刻是绿的
node scripts/test.js test/plans/feat/compile --jobs 1 --tier fast

# 1) 两个构建（开关见 §二·二）
npm run build ; Copy-Item out/latest/bin,out/latest/libs -Destination out/migrate/old -Recurse -Force
#   改 kVarLoweringSsa = true
npm run build ; Copy-Item out/latest/bin,out/latest/libs -Destination out/migrate/new -Recurse -Force

# 2) 规范形产物（两轮，第二轮把 old 换成 new）
Get-ChildItem out/migrate/corpus/*.cml | ForEach-Object {
    & out/migrate/old/bin/camel --diag-format json $_.FullName std::jgir canonical=true |
        Set-Content "out/migrate/graphs/old/$($_.BaseName).jgir"
}

# 3) diff + 归因表
#   见 3.3 / 3.4；结果写 out/migrate/differing.txt 与本节表格
```
### 3.6 分刀表（顺序照旧，每一刀都要新的最小程序与逐字节比）

| 刀 | 覆盖 | 每类的**新最小程序**（进 §二·三 语料） | 比对 |
|----|------|------------------------------------------|------|
| 1 | **平直**（既不平直也不需要第二刀的那一小块） | 一条：`var x = e; x = f; println(x)`（无分支/循环/闭包） | 新旧 canonical **逐字节比** |
| 2 | **臂** | 一条：`if c then sync { var x = 1; x = 2; null; } else sync { null; }; let n = 0`（即 seed 71 的最小形状） | 同上 |
| 3 | **循环携带** | 一条：`var x = 0; let i = while i < 3 { x = x + 1 };` 一类（循环体里写、循环后读） | 同上 |
| 4 | **捕获与 `var` 形参** | 两条：闭包捕获 `var`（捕获时的值）＋ `with <p: var int>` 形参（调用交回新值） | 同上 |

**规矩不变**：每类的差异**逐个归因**（§3.4），**归不了因的一律算新路径的错**。

## 不做什么
### 3.7 `foreach` / `Range` 这条护栏（每日审查第 2 条的一半）

每日审查（`internal/plans/2026-11-28-daily-review.cn.md`，`fsh-dev@1188fb1b0d`）第 2 条的修法原句：**"`foreach` 对 `Range` 这个例子进回归用例，答案归那一个解析器。"** 另一半（`builder_call.cpp` 的 `withParameterOfSelectedEntry` 在 `signatureForArgumentContext` 之外再扫一遍候选条目）**归"前端类型检查"那件重构**，不在①的范围里，本车道不碰 `builder_call.cpp`。

**今天的读数（`lane/progen` 上现跑的，探针已删）**：

```
foreach_range [gir]: exit=0 lines=119      （产物首行 digraph GraphIR {）
foreach_range [run]: exit=0 → 0 / 1 / …
```

程序（9 行）：

```cml
func show(x: int) => void sync {
    println(x)
}

func main() sync {
    (0 .. 3)->foreach<show>
}
```

**结论**：今天**不是红的** ⇒ 它不进 §3.4 的"旧路径缺陷"行，而是**护栏行**：新旧路径下都必须 `exit=0` 且 canonical 一致。**自纠一条**：我第一版写成 `for x in 0..4 { … }`，解析器报 `Unexpected 'for'; expected '}'.` —— 语言里没有 `for` 关键字，`foreach` 是库函数（`(0 .. 6)->foreach<show>`，见 `test/cases/consistency/faults/fault_in_foreach.cml`），页里按后者的形状写。

---


- **不打第三轮打印**（定位到行不产出修法）。
- **不动 `src/`**（这一笔只交材料）。
- **不碰 `kGraphFlagGatesDissolved`**（`lane-editview-commit` 在删）。
- **不在格子/抹除上做任何修补**（它们都是要删的）。

**为什么生成器层列入清单（而不是"不属于这一笔"）**：架构第 8 节第 4 步的原话是"`var` 只降低成 SSA……**生成器只降低一次**"。`gct/builder_func.cpp` 里的 `pcCell`/`readCell`/`writeCell`/`addCellOfType`/`HoistedCell`/`CellDecl` **就是 `var` 的读写降低在生成器层的第二份表示**，不是"与 `var` 降低无关的另一层"。所以它属于这一笔要删的机制，只排在**第二笔**（先删 `gir/` 侧，再删 `gct/` 侧），不单独立项、也不假装看不见。
