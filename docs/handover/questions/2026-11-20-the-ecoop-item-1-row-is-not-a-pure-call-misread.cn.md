# 条目（1）与三条打印判据：四条读数一字未动，挡住它们的是「没写子句」，不是读法

**日期**：2026-11-20
**提出者**：车道 `lane-ecoop1`（工作树 `D:\Projects\Camel-wt\ecoop1`，基线 `6cb6fa721`）
**状态**：待作者/决策者过目（两笔都已落地并且全绿：`2246e39ef` 共享取子句；本页量的是它之后）
**触发**：决策者原话「规则：经函数值调用的节点，足迹按被调值的类型读，`pure` 的足迹为空。」「实现：立一个函数『取某个节点某个操作数的效应子句』（含穿过 GATE、Norm/With 下标、with 实参基址），运行时 `Resolver`、构建器 `reachOfOper`、`effects.cpp`、`builder_diagnostics.cpp` 四处都改为调用它。构建器在 `CannotPlace` 时也和运行时一样记『任意值』。」「修好后应当是：①`print_unordered` 冲突对变成新打印和 `main` 自己的某个打印 ②`print_prepended` 被接受，新的一行打在最前 ③`print_appended_reversed` 被拒，冲突对是新打印和更早的某个打印 ④条目（1）在库 `grad` 下由效应检查拒」

## 1. 结果：四条读数，改前改后**逐字相同**

```
① 未成立  冲突对仍是（node 3 `a call of 'run'` 的 write unknown）×（node 19 新打印的 write stdout）
② 未成立  仍被同一对拒（exit 1），新的一行没有打印
③ 未成立  `print_appended_reversed` 被**接受**（exit 0），不是「被拒，冲突对是新打印和更早的某个打印」
④ 未成立  库 grad 下仍是 `PassEffectOrder`，不是效应检查 `PassEffectBeyondType`
```

引擎侧 `step_print` 仍是 `PassEffectBeyondType`、库侧仍是 `PassEffectOrder`，`loss_print` 对照也逐字相同。**规格的三件事都做到了（下标、GATE、基址、`CannotPlace` 记任意值、四处调它），四条例题一条没动。**

## 2. 为什么：子句根本不存在的那个值，读法改不动它

`main` / `run` / `step` 的类型**都没有子句**（逐字，`std::jgir`，库 `grad` 版）：

```
GRAPH main | () => int
GRAPH run  | (X: f32[?, ?], y: f32[?, ?], rate: float) => (float, Linear)
GRAPH step | (layer: Linear, X: f32[?, ?], y: f32[?, ?], rate: float) => Linear
GRAPH loss | <layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure
GRAPH vjp::grad<<layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure>
           | (f: <…> (X: f32[?, ?]) => float pure) => (<__pack_0_0: Linear, __pack_0_1: f32[?, ?]> (__pack_0_0: f32[?, ?]) => { weight: f32[?, ?] }) uses var f
```

链条是 `main → run → step → 调用 vjp::grad<…> 交出的那个值`。最里面那个值的类型**没有子句**，于是：

```
src/compile/gir/builder_resources.cpp:1079   if (settled.anyOf.empty()) { … graph->setResources(published); }
```

**`anyOf` 非空就不发布子句**（这是既有的规矩：「一个不能写成子句的集合，宁可让签名保持原样，也不许承诺过少」）。而 `anyOf` 的来源正是那个没写子句的值读出来的「任意值」。所以 `step`、`run`、`main` 一层层都没有子句，`run` 的调用在 `main` 里读作 `write unknown` —— 它就成了打印的冲突对端。**「按值的类型读、pure 就是空」对这条链一个字也没说：那不是 pure，是没写。**

## 3. 四条判据各自还缺什么

```
① 缺：`run` 的调用不再是 `write unknown`。
② = ① + 另外一件：**插进去的打印今天根本不执行**。引擎版 `print_prepended` 是被接受的（exit 0），
   stdout 仍只有 `main` 自己的三行，`announced` 一次也没出现；`print_ordered` 打得出（它锚在
   `main` 自己的打印那个语句位上，那里有需求）。`print_unordered` / `print_prepended` /
   `print_appended` 锚的是 `is_inputs(n)`，**那个值没人要** ⇒ 打印不被求值。
④ 缺：`step` 的子句要发布出来、并且不盖住那次 `write stdout`。
```

⇒ **①②④ 是同一件：那条链上必须有子句。** ② 还多一件**与效应检查无关**的事：插在 `is_inputs` 上的打印不被求值。

## 4. 两条能让链上有子句的路（都要一个裁定，不是我能在读法里改出来的）

| 路 | 做什么 | 代价 | 判断 |
|---|---|---|---|
| 丙 | 库的 `grad` 在**它自己的声明**上写子句 | 改 autodiff 的声明，不改读法 | 干净；但 `{}` 里到底写什么要 autodiff 那边定 |
| 乙 | 值没写子句时，读**值背后的图**（`vjp::grad<…>` 声明 `uses var f`） | 改读法：构建器 `reachOfCall`/`readFunctionOperandReach` 有 `functionValueGraphOf(...)`，运行时 `Resolver::call` 要顺着值找到图 | 可行，但与本轮「按值的类型读」的字面相反 |

**我上一轮说「乙不换读数」是错的，这里更正**：我当时拿**纯度**否认它（那张图不是 pure），而让配对消失的不是纯度，是**命名**——`write unknown` 与**每一个**写都冲突，`uses var f` 只与同一个变量单元冲突。所以乙**会**让 ①②③ 的配对变成判据要的样子（`step` 的子句也才发布得出来，④ 才有戏）。**这是「按图读」与「按类型读」在这程序上真分岔的第二份证据，而且方向与上次相反。**

## 5. 这两笔各改了什么

`2246e39ef`（共享取子句，全绿：smoke 10/10、feat/opt 1621/1621、feat/effects 7/7、feat/compile 0 fail + 16 known）：

```
include/camel/core/operator_table.h   functionValueClause(type)；functionValueWithBase(kind, index)；
                                      functionValueClauseAt(type, kind, index) -> {clause, withArgBase, withCount}
src/core/operator_table.cpp           三者的实现；functionOperandClause 改为调它
src/runtime/resources.cpp             clauseOfOperand / clauseOfCall（穿过 GATE、Norm/With 下标）；
                                      Resolver::call、Resolver::fromFunctionOperand 改调它
src/runtime/effects.cpp               EffectWalk::ofNodes 的 Call 分支改为读值的类型（此前一律 external = true）
src/compile/gir/builder_resources.cpp reachOfCall、readFunctionOperandReach 改调它；CannotPlace 记任意值
src/compile/gir/builder_diagnostics.cpp  isEffectNode 的 Call 分支改调它
src/runtime/node_roles.cpp            effect 角色改调它
```

`declaredClauseAt` 仍**故意不调**：那里一个「没写子句」的调用点必须**被判过**而不是被拒读（`commit_checks.a_type_that_promises_nothing` 钉住 `checked >= 3`、`declined == 0`）。同一个 `nullopt`，两个检查要两个答案 —— 本轮的第四个读数。

## 6. 本页没有做的

- 没有改任何用例的期望值、没有改 `train.cml` / `edits-ecoop.cml` / 基线文件 / `.plan.toml`。
- 没有动 `modules/transforms/jvp.cpp` / `split.cpp` / `lift.h`（那是 `lane-jvp-closure` 的地盘）。
- 没有走乙，也没有动 autodiff 的声明：两条都在等 §4 的裁定。

## 默认建议（写死）

**默认建议**：按 §4 的 **丙** 开工 —— **在库里 `grad` 自己的声明上写子句**，一个字都不动读法
（`2246e39ef` 那笔共享取子句保持原样）。
**判据**：库 `grad` 版下条目（1）由 **`PassEffectBeyondType`** 拒（不是 `PassEffectOrder`）；
`print_unordered` 的冲突对变成「新打印 × `main` 自己的某个打印」；四条读数**各报改前改后两次**、逐字对比
（§1 就是靠这个逐字对比证明「读法改不动没写子句的值」的）。
另外两件与这条分开记：② 的「插在 `is_inputs(n)` 上的打印根本不被求值」与效应检查无关，**单列另修**；
**乙**（值没写子句时读值背后的图）先不做 —— 它与「按值的类型读」的字面相反，要走得先让作者改那条规则。
