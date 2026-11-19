# 经函数值的调用，足迹取那个值的函数类型——而那个类型今天不写子句

2026-11-19 ｜ 来自 lane J（`lane-J-order-draft`，`origin/develop` = `ac2199f0c`）
｜ 对象：`src/runtime/resources.cpp#Resolver::call`、`src/runtime/resources.cpp#declaredClauseAt`、
`modules/autodiff/pullback.cpp#gradientSignature`、`docs/design/effects/plan.cn.md` 2.2
｜ 读数：`-Plan feat/modules/graph_inline`，Release，逐字见下方

## ① 作者裁定（2026-11-19 14:45）

> `step` 里那次经函数值的调用足迹记成 `write unknown`：被调的是 `grad` 的产物，函数值的类型里写着它的效应
> （`autodiff` 的变换已声明为纯、结果类型由被变换函数推出），所以经函数值的调用，足迹取那个值的**函数类型**的
> 效应子句，不记 `unknown`。……`unknown` 只留给真正没有类型的不透明调用。

要求：乙对照必须报 `RuntimeDiag::PassEffectBeyondType`。

## ② 已落地（`src/runtime/resources.cpp`，一笔）

`Resolver::call` 的 `callee == nullptr` 分支不再无条件记 `write unknown`：若 `with[0]` 的值（透 GATE 后）有
函数类型、该类型的 `resources()` 是 `Concrete` 且不点名 capture，就用 `fromSignature(type->resources(),
type->withTypesCount(), graph, ref)`——和 `declaredClauseAt` 读同一处（那里本来就按这个读：`with[0]` 的
`dataType()` → `FunctionType::resources()` / `withTypesCount()`）。`Concrete` 之外的（`Generic`、带 captures、
没有类型）仍走原来的保守读数。读数是 `write unknown` 只在真的没有子句可读时出现。

## ③ 乙对照的读数：那个类型今天没有子句

`step` 是（`commit_effect.cml`，随本页）：

```camel
with <w: float>
func loss(x: float): float {
    let e = x * w - 1.0
    return e * e
}

func step(w: float, x: float): float {
    let dloss = grad(loss)
    let g = dloss<w>(x)
    return w - 0.1 * g
}
```

程序先打印那个值的类型再开事务：

```
[type] the value the call runs: <float> (float) => float
```

**没有 `pure`，也没有任何子句**——`FunctionType` 的 `resources` 停在默认的 `Generic`（"一个都不写"，
`src/core/type/text.cpp#resourceClause` 打印时省略）。于是 `Resolver::call` 的那条新分支取不到子句，回落保守读数，
`step` 的编译期足迹仍带 `anyOf`，`step` 的类型仍是 `Generic`，`effectBeyondDeclaration` 在 `Generic` 上返回
`nullopt`，commit 于是落到序检查：

```
(commit_effect)/…/commit_effect.cml:33:5: [Error]: 'graph::commit' committed a graph whose conflicting effects
are not ordered: node 4 of graph 'step' (a call), in one region of gr…
```

**即乙报的仍是 `PassEffectOrder`，不是 `PassEffectBeyondType`。**

## ④ 缺口在哪

`modules/autodiff/pullback.cpp#gradientSignature` 造梯度函数的类型时是

```cpp
signature.functionType = core::type::FunctionType::create(withParams, normParams, result);
```

`FunctionType::create` 不传播 resources，所以这个类型只有参数与出口、没有效应子句。**同一族变换里别的都传播了**：
`modules/transforms/jvp.cpp#liftSignature` 是
`FunctionType::create(withs, params, lifted)->withResources(function->resources())`。`autodiff` 的算子是
`declareResources` + 空 `ResourceSpec`（"纯"），那只描述**造出这个函数**这件事；**调用**这个函数做多少事，由被变换
函数的子句决定，而那一句没被抄进类型。

顺着设计文档读，这里还多一层：`docs/design/effects/plan.cn.md` 2.2 的打印口径是「**值的类型打印推断出的子句**——
一个没写资源子句的函数，其值的类型带上函数体度量出的资源集，空集就打印 `pure`」，而 2.3 第 1 条是「取被调用者的
`Concrete` 签名；`Generic` 时取实际函数值的具体类型（静态函数常量、已知闭包、实参的声明类型），取不到按 `anyOf`
全局处理」。本次读数 `[type] the value the call runs: <float> (float) => float` 里**没有 `pure`**，说明那个节点上的
类型既不是"推出来的"（否则该打印 `pure`），也没有子句可读 ⇒ 与 2.2 的口径不符。

两个落点（都在 `modules/autodiff`）：

1. `gradientSignature` 把被变换函数的子句抄进结果类型（`withResources(function->resources())`，注意 `result` 应改成
   `Fresh`：梯度是新算出来的，不是某个参数本身）；这一条改动后，`Resolver::call` 已经落地的读数就会取到
   `pure`，`step` 的足迹不再带 `anyOf`，`step` 的类型被 `publishInferredResources` 发布成 `Concrete`，乙就会报
   `PassEffectBeyondType`。
2. 或者让"值的类型"在节点上取度量后的那一份（2.2 的口径），梯度函数体的足迹是纯的，同样会给出 `pure`。

## ⑤ 问题（已裁定，见 ⑦）

1. 要让乙报 `PassEffectBeyondType`，最小改动是 `gradientSignature` 把被变换函数的子句抄进结果类型（`result` 改成
   `Fresh`）。**但 `modules/autodiff` 本轮被明确禁止改动**（迁移台账 144 不许涨）。请判：这一轮是否放行这一行？
   还是把乙留在 `PassEffectOrder` 等下一轮？
2. 若不放行：是否接受"`Generic` 保持保守读数"的现状，把乙的期望从 `PassEffectBeyondType` 改成"本轮不可能"？
   两条路我都能落，但结果不同，不自己挑。
3. 已落地的读数本身是对的读法吗？2.3 第 1 条列出的"实际函数值的具体类型"三类来源（静态函数常量、已知闭包、实参的
   声明类型）里，这次加的是**第三类**（`with[0]` 那个值的类型，与 `declaredClauseAt` 同一处）；前两类仍走
   `staticFunctionOf` 与闭包那一支。若这条读法不要，`resources.cpp` 那一笔就整个回退。

## ⑥ 六条计划（本轮收口读数，都在本页落地后的树上，逐条单独跑）

`feat/compile` 2635/2619/0（16 known）· `feat/linear` 869/864/0（2 skip、3 known）· `feat/opt` 1617/1617/0 ·
`feat/parse` 126/126/0 · `feat/para` 104/104/0 · `feat/tools` 70/70/0。与 `ac2199f0c` 相比一条都没动 ⇒
`resources.cpp` 那一笔在语料上没有读数移动；"带子句的形参类型"这一形状在语料里没有被测到（否则足迹会从 `unknown`
变成一个具体资源，读数应当会动）。台账 ratchet（`feat/tools#tools.autodiff_migration_ledger.ratchet`）绿，记录值仍
是 `total: 144 / library: 135 / pass: 22 / with: 56 / array: 69 / other: 10`。

## ⑦ 裁定与落地（2026-11-19 傍晚，第三笔）

作者放行 `modules/autodiff` 那一行，附三个条件（迁移台账 144 不许涨 · 六计划不差于 `ac2199f0c` · 单独一笔）。

**改动**（`modules/autodiff/pullback.cpp#gradientSignature`）：被变换函数的子句是 `Concrete` 且不点名 capture 时，
把它带进结果类型（`withResources`）；`result` 换成 `Fresh`（梯度是 `differentiationRefusal` 允许的第一个 `with`
形参能有的数或张量，因此不别名任何调用者持有的资源），`allocates` 清掉。参数逐位复制（上面两个循环），所以形参编号
原样transfer。点名 capture 的签名不搬：那些捕获位是被变换函数的，而这个类型一位也不命名，于是它保持 `Generic`
（"一个都不说"）。

**读数**：`[type] the value the call runs: <float> (float) => float pure` —— 那个值的类型现在写着子句。commit 被
**效应**检查拒：

```
'graph::commit' committed a graph that reaches past the effect its own type declares:
node 10 of graph 'step' (:io/println), … write stdout
RuntimeDiag::PassEffectBeyondType
```

即作者要求的逐字诊断，不再是 `PassEffectOrder`。用例 `test/cases/modules/graph_function/commit_effect.cml` 接回
`graph_inline` 计划（`a_call_through_a_value_reaches_what_its_type_declares`，负例，`expect_diagnostic_name` 钉
`RuntimeDiag::PassEffectBeyondType`），`ast_hashes.txt` 用 `-UpdateAstHashes -AstOnly` 记入（新增 1 行）。

**模块层读数**（`-Plan feat/modules`，一次跑，2074 条）：2074 / pass 2052 / fail 8 / skip 7 / known 7。那 8 条是
**基地就红的**，不是这一笔带来的——把 `pullback.cpp` 那一行临时回退再跑同一计划，红的变成 **10** 条：同样的 8 条，
加上本页的用例，加上 `grad_captured_var_written.macro.opt`（一个写模块级 `var` 的被变换函数：`grad` 的类型以前不带
子句、调用点记 `unknown`，`std::macro` + `std::opt` 下被拒；带上子句后通过 ⇒ **这一笔把它从红变绿**）。那 8 条逐字
原因与本笔无关（`siamese_shared_encoder.devirtualize_inline.nvm` 的 `std::cxx::inline` 排序拒在两版里**逐字相同**）。

**仍未解的一条**：`Inline` 区域没有链入口、纯节点处 splice 进去的写不执行（页
`2026-11-19-order-cannot-reach-a-node-that-inline-just-made.cn.md` ④）—— 独立缺陷，单列，不在这三笔里修。

## 附：复现

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <tree> -Plan feat/modules/graph_inline -Jobs 8
```

（落地过程里有两轮读数是在用例**还没接进计划**时取的，为的是不把一条注定红的期望钉成"今天如此"：乙的用例先放在本
目录下，作者裁定后才接回 `graph_inline` 计划。）