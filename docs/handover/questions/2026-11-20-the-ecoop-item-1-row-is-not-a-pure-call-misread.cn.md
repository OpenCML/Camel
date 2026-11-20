# 条目（1）那三颗判据：①成立，②③被同一个事实挡住 —— 库侧的值类型没写子句

**日期**：2026-11-20
**提出者**：车道 `lane-ecoop1`（工作树 `D:\Projects\Camel-wt\ecoop1`，基线 `6cb6fa721`）
**状态**：待作者/决策者过目（甲的代码已落地：一处取子句的函数 + 所有读者调它；本页是「判据②③量出来的样子」）
**触发**：决策者原话「修值调用的足迹。经函数值调用的节点，足迹按被调值的类型读，pure 就是空。」「和 `functionOperand` 共用同一个取效应子句的函数，不在调用点打补丁」「修好后应当是：①条目（1）在两种 `grad` 下都由效应检查拒 ②`print_unordered` 的冲突对变成新打印和 `main` 自己的某个打印 ③`print_prepended` 被接受，新的一行打在最前」

## 1. 三颗判据的结果（Release，主机 `-Jobs 8`，`HOST=6cb6fa721 dirty:7` 的新二进制）

```
① 成立。两种 grad 都仍由效应检查拒，逐字见 §2 与 §3。
② 未成立。冲突对仍是（node 3 `a call of 'run'` 的 write unknown）×（node 19 新打印的 write stdout），
        不是「新打印 × main 自己的某个打印」。
③ 未成立。库侧被同一对拒；引擎侧被接受，但新的那一行根本不打印（四行/三行都与改前相同）。
```

**②③ 是同一条事实的两面，不是两处缺陷**：`main` 里那次 `run` 的调用读出来是 `write unknown`，它因此成为打印的冲突对端。它不是这次调用**经函数值**，而是**静态被调方**——它的足迹来自 `run` 的类型，而 `run` 的类型**没有子句**。

## 2. ① 的两侧逐字（改前改后**逐字相同**）

引擎 `autodiff:grad` 版（`git show 217156abb^:examples/case-study/train.cml`，同一 rewrite `eco::step_print`）：

```
error[RuntimeDiag::PassEffectBeyondType]: 'graph::commit' committed a graph that reaches past the effect its own type declares: node 10 of graph 'step' (:io/println), in a graph whose type declares nothing, makes an access the type does not cover (write stdout): the commit would publish a body that reaches past its own declared effect
```

库 `grad` 版（`docs/handover/reports/eval-round10/case-study-variants/train.cml`）：

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 5 of graph 'step' (a call of 'vjp::grad<<layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure>'), in one region of graph 'step', has no order against node 10 of graph 'step' (:io/println), and at least one of the two writes (write unknown / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
```

`eco::loss_print` 对照（两版都 `PassEffectBeyondType`，`node 16 of graph 'loss' (:io/println)`）也逐字相同。

## 3. ②③ 的逐字读数

库 `train.cml`，`eco::print_unordered`（exit 1）：

```
error[RuntimeDiag::PassEffectOrder]: 'graph::commit' committed a graph whose conflicting effects are not ordered: node 3 of graph 'main' (a call of 'run'), in one region of graph 'main', has no order against node 19 of graph 'main' (:io/println), and at least one of the two writes (write unknown / write stdout): no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other, so the commit would publish two effects on one resource that the program leaves free to happen in either order
```

库 `train.cml`，`eco::print_prepended`（exit 1）：**同一对**，逐字相同。

引擎版两条都被接受（`print_unordered` exit 0、`print_prepended` exit 0），但**四个 rewrite 的 stdout 都只有三行**：

```
loss before: 3.7500
loss after:  0.0000
[[1.9999925], [-0.99998784]]
```

即 `announce` 的那一行**一次也没出现**（对照：`eco::print_ordered` 在引擎版上打出 `announced`——它把打印插在 `main` 自己的重量打印那个语句位，那里有需求）。`print_unordered` / `print_prepended` / `print_appended` / `print_appended_reversed` 锚的是 `is_inputs(n)`，**那个值没人要** ⇒ 打印不被求值。**所以 ③ 的「新的那一行打在最前」在今天这棵树上无法观测**，与效应检查无关。

## 4. 为什么 `run` 的调用读出 `write unknown`（这棵树上逐字）

```
GRAPH main | () => int                                    ← 没有子句
GRAPH run  | (X: f32[?, ?], y: f32[?, ?], rate: float) => (float, Linear)   ← 没有子句
GRAPH step | (layer: Linear, X: f32[?, ?], y: f32[?, ?], rate: float) => Linear  ← 没有子句
GRAPH loss | <layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure    ← pure
GRAPH vjp::grad<<layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure>
           | (f: <layer: Linear, y: f32[?, ?]> (X: f32[?, ?]) => float pure) => (<__pack_0_0: Linear, __pack_0_1: f32[?, ?]> (__pack_0_0: f32[?, ?]) => { weight: f32[?, ?] }) uses var f
```

**链是 `main → run → step → 调用 vjp::grad<…> 交出的那个值`。** 链条最里面那个值的类型**没有子句**（`{ weight: … }` 后面什么都不写）。「pure 就是空」对它不适用 —— 它不是 pure，是**没写**。前端的规矩是：读不到的调用就不给这一层写子句（所以 `step`、`run`、`main` 都没有），读得到才写（所以 `loss` 是 `pure`）。于是 `step` 的体里那次调用读作保守的 `write unknown`，一路传上去，`run` 的调用在 `main` 里也是 `write unknown` —— 成了打印的冲突对端。

⇒ **②③ 要成立，需要的是链条最里面那个值类型有子句**，也就是库的 `grad` 在它自己的**声明**上写一个。写读法不会让它出现：`Generic` 按定义就是「什么都不承诺 ⇒ 调用方必须保守」，把它当空就是不健全。

## 5. 三条路与代价

| 路 | ②③ | 代价 | 结论 |
|---|---|---|---|
| 甲（本线已做）：一处取子句 + 所有读者调它 + `EffectWalk` 的 Call 分支读值的类型 | 不变 | 已付 | **做**（这是「pure 就是空」本身；引擎侧那次调用今天就已经读出空） |
| 乙：值没写子句时读**值背后的图** | 不变 | 大 | **不做**：那图是 `uses var f` 且三个节点带 `effect` 角色，读出来不是空（这是「按图读」与「按类型读」在这程序上真分岔的第一份证据） |
| 丙：`grad` 的**声明**上写子句 | 有希望 → ②③ | 改的是 autodiff 的声明，不是读法 | **建议交给 `vjp`/autodiff 那条线裁** |

**默认建议：甲已落地，②③ 挂到丙。** 若决策者认为「没写子句的值类型」在**这一处**该读成空，请指出依据——那会把「没写」与「写 pure」变成同一件事，与设计原则 2.2 冲突。

## 6. 本线落实了什么（甲）

```
include/camel/core/operator_table.h        functionValueClause(const type::FunctionType*)：Concrete ⇒ 子句，否则 nullopt
src/core/operator_table.cpp                它；functionOperandClause 改为调它
src/compile/gir/builder_resources.cpp      reachOfCall、readFunctionOperandReach 改调它
src/compile/gir/builder_diagnostics.cpp    isEffectNode 的 Call 分支改调它
src/runtime/resources.cpp                  Resolver::call 改调它（删掉它私有的 captures.empty() 条件）、fromFunctionOperand 改调它
src/runtime/effects.cpp                    EffectWalk::ofNodes 的 Call 分支：不再一律 external = true，改为读值的类型
                                           ——**「pure 就是空」唯一真正不成立的落点**
src/runtime/node_roles.cpp                 effect 角色改调它
```

`declaredClauseAt`（`src/runtime/resources.cpp`）**故意不调**它：那里一个「没写子句」的调用点必须**被判过**而不是**被拒读**
（`commit_checks.a_type_that_promises_nothing` 钉住 `checked >= 3`、`declined == 0`），而 `functionValueClause` 的 `nullopt`
在 `Resolver::call` 那里答的正是「拒读」。**同一个 `nullopt`，两个检查要两个答案** —— 这是本轮的第四个读数。

## 7. 本页没有做的

- 没有改任何用例的期望值、没有改 `train.cml` / `edits-ecoop.cml` / 基线文件 / `.plan.toml`。
- 没有动 `modules/transforms/jvp.cpp` / `split.cpp` / `lift.h`（那是 `lane-jvp-closure` 的地盘）。
- 没有把「基址差异」（运行时 `withArgBase` vs 构建器 `isWith && isCall ? 1 : 0`）并进这一笔。