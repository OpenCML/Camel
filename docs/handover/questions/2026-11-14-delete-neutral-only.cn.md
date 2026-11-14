# 作者要删 `algebra.cpp` 的 `neutral_only`，可它是活代码：删还是留？

**状态**：待裁。第十一节第 7 路（卫生）的催办原文是「删 `algebra.cpp` 的 `neutral_only`」
（`docs/handover/batches-2026-10-31-72h.cn.md` 的「催办（11-14 07:17）」）。本轮开工按第一件去查它
有没有调用者，查到的是反的：**它不是残留的死代码，是一个文档化的遍参数，两个计划还把它当差分对照
列用**。所以这一页不是「删不删死代码」，而是「照原话删掉一个活参数，还是留下」。

## 1. 它现在做什么

`std::opt::algebra` 有两半。定律那一半早已是规则库（`stdlib/rules.cml` 的 37 条 `rule`，
注册为 `rules::simplify`）；`neutral_only` 那一半是这个遍剩下的 C++ 实现，三处都在：

- **声明**：`stdlib/std.cml` 给 `std::opt::algebra` 声明了 `neutral_only: bool = false`；
- **分派**：`src/execute/pass.cpp` 的 `algebraPass()`——`args.getBool("neutral_only")` 为真时返回
  `AlgebraPass(ctx, true)`，否则返回跑规则库的 `RulesAlgebraPass(ctx, safe_fp)`；
- **实现**：`src/passes/opt/generic/algebra.cpp` 的 `algebraDraft()`——`neutralOnly` 为假时第一句
  就 `return 0`；为真时只做五个整数单位元（`x + 0`、`0 + x`、`x - 0`、`x * 1`、`1 * x`），不做同类项
  与常数因子合并。`src/passes/opt/generic/rewrite.h` 与 `include/camel/passes/opt/generic.h` 的类
  声明都写明它是「the integer neutral-element reference mode」。

所以它是一个**默认关闭、打开就改变输出**的遍参数，不是没人跑的函数。

## 2. 谁在用它

`git grep -n neutral_only`：**42 行、21 个文件**（本分支删掉 `docs/handover/stdlib/std.cml` 那份镜像
后；删之前是 43 行、22 个文件）。

| 位置 | 量 | 用法 |
|---|---|---|
| `src` + `include` | 5 行 / 4 文件 | 分派、实现、两处声明注释 |
| `stdlib/std.cml` | 1 行 | 遍参数的声明本身（标准库表面） |
| `test/plans/feat/modules/graph.plan.toml` | 9 处 `neutral_only=true` | 与用 Camel 写的遍 `neutral::integers` 做**逐图差分**的对照列，连同金标 |
| `test/plans/feat/opt/algebra_rules.plan.toml` | 10 处 `neutral_only=true` | 同上：`rules::simplify` 那一列旁边的参考列，金标就是它的读数 |
| `test/cases/modules/graph_pass/neutral_identities.cml`、`neutral_kept.cml` | 2 个用例 | 五个单位元与「不是同类项」的反例，比的正是这一列 |
| `test/cases/opt/algebra_rules/`（4 个 .cml + `pairs.mjs`、`tables.mjs`） | 6 个文件 | 差分程序与表格脚本的对照说明 |
| `test/perf-baseline.json` | 1 行 | 一条既有读数的主体 |
| `docs/cli.md`、`docs/cli.cn.md` | 各 2 行 | 遍参数参考 |
| `docs/technical/45_graph_module.md`、`docs/design/rewrite/rule-inventory.cn.md` | 各 1 行 | 架构说明；后者明写「C++ 侧剩下的 `neutral_only` 是文档化的参考模式，不是库的一部分」 |
| `docs/handover/reports/H367-effect-check-audit.md`、`code-review-rewrite.md` | 各 1 行 | 历史读数与审阅记录 |

## 3. 作者自己此前是有意保留的

- `a510676bb`「opt: delete the C++ algebra law table, keep neutral_only」——定律表删了，它留着；
- `c7b6133bb`「docs(handover): H160 keeps algebra.cpp as the neutral_only reference」；
- `4df0be103`「Merge H160: the five survivors are deliberate, because neutral_only is a different mode」。

## 4. 两条路与代价

**A. 留。** 代价：`algebra.cpp` 这个文件、`AlgebraPass` 这个类、`algebraDraft()` 这条路径继续存在，
「C++ 侧不再有第二份代数实现」这句话仍要带上「除了单位元模式」这个括号——现在也正带着。

**B. 删。** 要一起改的四处，没有一处是卫生那一件能顺手的：

1. `stdlib/std.cml` 的 `neutral_only` 声明——标准库表面，`scripts/stdlib-reference.mjs --verify-ops`
   与两份 `docs/cli*` 的遍参数参考都要跟着改；
2. 两个计划里的 19 处 `neutral_only=true` 与它们的**金标**；金标必须重生成，而重生成要跑那两个
   计划（`test/plans/feat/modules/graph.plan.toml`、`test/plans/feat/opt/algebra_rules.plan.toml`），
   测试通道这一周归合并车道；
3. 六个用例与 `pairs.mjs`、`tables.mjs` 里的对照说明；
4. 六份文档与 `test/perf-baseline.json`。

删完的同一轮，`feat/modules/graph` 与 `feat/opt/algebra_rules` 会红（金标还是旧读数），本轮判据
「红集与基线相同（四条）」就不成立；而且差的是一整组对照——那两个计划问的正是「Camel 写的遍与
C++ 的参考模式是不是逐图相同」，没有参考列就没有这组读数。

## 默认建议（按 A 继续，不等回复）

**保留 `neutral_only`，本轮不动**，把它记成「查过、不是死代码」。若确实要删，请它作为独立一件排进
一条**有测试通道**的车道，顺序是：先改 `stdlib/std.cml` 的声明与两个计划的 `args`，用窄 plan 重生成
那两份金标，最后改用例与文档；不要混进卫生里做，那样会把一个「删死代码」的提交变成一次参数下架。

## 关联

- `docs/handover/batches-2026-10-31-72h.cn.md`：催办原文（11-14 07:17，第 7 路卫生）；
- `docs/design/rewrite/rule-inventory.cn.md`：37 条规则与「C++ 侧不再注册规则」的现状；
- `docs/technical/45_graph_module.md`：用 Camel 写的遍与 C++ 遍的差分对（`neutral::integers`）；
- `test/plans/feat/opt/algebra_rules.plan.toml`：两列对照的说明与金标。