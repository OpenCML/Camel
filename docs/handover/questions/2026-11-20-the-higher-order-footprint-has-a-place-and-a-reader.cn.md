# The higher-order footprint has a place to be written and a reader; what is left is the two shapes it cannot place

日期：2026-11-20 ｜ 来自 `lane-merge-10`（工作树 `mrg2`）｜ 对象：`callsGraphs` 类算子的资源足迹 ｜ 状态：**默认按甲开工，待作者确认**

本页原本要问的是「`callsGraphs` 类算子的足迹**没有地方可以写**」。在基线 `52fe1b6a2` 上读了一遍之后，
那一问不成立；于是本页问它剩下的那一半。撤回哪一半，由作者点。

## ① 先更正一条读数：声明不在 `builtin.cpp:747`，读者也已经在树上了

`src/builtin/executors/builtin.cpp:747` 那个循环确实只写 `.pure = false, .callsGraphs = true`，没有
`ResourceSpec`。但同一个文件 `:712` 的注释说的就是这件事该到哪儿找：「it declares that much and
`declareBuiltinOperatorResources` says what it really touches」。声明在别处：

- `src/builtin/executors/builtin_semantics.cpp:600`–`:622` 正是这一族 —— `map_arr` / `filter_arr` /
  `foreach_arr` / `unordered_foreach_arr` 一组、`reduce_arr` / `unordered_reduce_arr` 一组、
  `:mark/isolate` 单独一条，写的都是 `ResourceSpec{.functionOperand = …}`：前六个取第一个 `with`
  操作数，`:mark/isolate` 取第一个普通操作数（`Norm`）。
- 读它的地方也在：`src/runtime/resources.cpp:542`（`Resolver::fromFunctionOperand`），`:577` 那一支按
  `where.index + 1` 把子句的形参绑到节点的操作数上 —— 「照抄一次经函数值的调用的读法」这件事本身。
  加它的提交是 `f0051edd2`（*Read a function operand's clause in the runtime resource footprint*），
  **已经在 `origin/develop` 里**。
- 所以「字段写了是函数操作数的，运行时 resolver 不读」这句原因，在 `e575b49f0` 写下它那一刻成立，
  在 `f0051edd2` 之后不成立。`e575b49f0` 与 `925c38791` 都还不在 `origin/develop` 里。

这是树里一条绿的正对照，不是推断：`test/cases/effects/hof_callback_effect_footprint.cml` 让同一个
`map` 分别跑 `show`（`uses var stdout sync`）与 `inc`（不写子句），`compile.higher_order_clause` 的
`map.runtime_footprint` 钉住的读数就是 `RESOURCES node main … :mark/map_arr: rmw ambient stdout;
external`，以及「另一个 `map` 什么都不带」。

⇒ 于是「只有两条坏路：声明成假的 `pure`，或发明一个访问」也不成立：第三条路早就铺好，
`map` / `filter` / `foreach` 已经在走它。

## ② 剩下的那一件：两种形状落不到节点的操作数上

`src/runtime/resources.cpp:569`–`:573` 的注释把剩余部分写明了：`with` 那条且形状不是 `Fold` 时，子句的
形参绑到操作数（`:577`–`:578`）；`Fold`（`reduce` 的累加器）与「函数当普通操作数」（`:mark/isolate`，
`Norm`）走 `:579` 的 else —— `signature.ambient` 仍逐条保留名字与模式（`:591`–`:598`），而
`signature.params` 每一条都读成 `anyValue = true`（`:583`–`:590`）。

**问作者的一件事：回调足迹的地点对不上时，读到「该类的某一份」就到此为止吗？**

- **甲（默认）**：到此为止。这正是 `fromSignature` 给一个捕获的同一种读法（`:571`–`:573` 原话：
  「a use that cannot be placed is "some resource of its class" … never a dropped one」），而 `Fold` 的
  累加器本来就不是回调的形参，位置映射在那一边不成立。**不发明新概念。**
- **乙**：给这两种形状各补一条绑定 —— `Fold` 绑累加器、`Norm` 绑第一个普通操作数，像 `:577` 对 `with`
  做的那样。代价：一套新读法加每种形状各一套映射，而且要先把「累加器不是形参」在类型层写出来，
  否则那条绑定说的是类型层没承认的事。

**⇒ 我选定甲（默认建议）：它复用的是已有的读法，而乙要发明新映射；且 `e575b49f0` 的教训正是「字段写了
不读」，甲是让 resolver 去读它本来就该读的那个位置，不是给每种形状另立一套。**

## ③ 一个站点的两类问题，别和 `tiles:regions` 混成一件事

`tiles:regions` 是「**该声明却没声明**」—— 注册处会拒收一个非纯算子的空列表。这一类在基线上是
「**声明有了、读法有了**，只剩两种形状的位置」。两条的堵点不在同一层；合成一件事，会把「缺一份声明」
和「缺一套形状映射」记成同一个待办。

## ④ 预警：`925c38791` 的那句原话，只在它自己那棵树上

`925c38791`（**未合**）的 `src/runtime/resources.cpp:1413` / `:1417` 原文是「its operator declares
nothing, so what it reaches is read conservatively」与「… the operator's declaration is where one would be
named」。那段代码（`unnamedReasonOf`）在基线 `52fe1b6a2` 上 `git grep` 零命中 —— 它不在 develop 里。

所以**下一条问 commit checks 的计划，读数取决于它读哪棵树**：读那条支线，`map_arr` / `filter_arr` /
`reduce_arr` / `unordered_*`、`:mark/isolate` 会再得到同一句话；读 develop 不会。派工时要把工作树的 sha
写进判据，否则两个人会各报一次「撞上同一面墙」。

## ⑤ 读数坐标（本页没有一行计划汇总）

`ref.foreach_gives_back` 的失败是在合了 develop 之后的 `925c38791` 附近读到的，**而那次三条计划一条没
跑完（主机 `1m load` 20）**，所以本页不引用任何计划的汇总行，也没有一处跨树相加。

另外：那一问引用的原文 `touches any unknown; no clause can name what node … reaches` 在基线
`52fe1b6a2` 上 `git grep` 同样零命中 —— 今天没人能在 develop 上复现它。这恰恰是 ① 的另一面：它描述的
是那条支线的诊断，不是 develop 的。用例本身在：`test/plans/feat/compile/ref.plan.toml:692`
（`backends = ["nvm", "fvm", "jit", "prl"]` 在 `:696`，「4 个后端」的来处）。