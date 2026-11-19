# 规则两侧的访问：加 9.11 的定义期检查会把库里哪些规则变红

**问题给作者。** 设计原则 9.11 第 5 条要求「**规则两侧只用什么都不访问的算子**（比 `pure` 更严，读也不行）」，理由是
保住「过声明检查 ⇒ 过提交」这条不变量。今天代码里**没有**这条检查：`src/runtime/rules.cpp#SideReader::term`
只拒 call / `with` 实参 / 控制依赖，不拒带访问的算子；`src/runtime/writings_match.cpp#DraftWritings::build`
构造右侧时也不查。访问只被**匹配期静默排除**（`src/runtime/writings.cpp#DraftWritings::shapeOf`：
`mayBeRecomputed` 为假、或 `readsVariable`（`ResourceClass::Variable`）为真的节点不参与）。

## 问题（作者裁定用，默认写死）

**问题：今天「静默不匹配」的规则，加上 9.11 定义期检查后变成**构建错误** —— 要的，还是太硬？**
**默认：按 9.11 就是要硬错误**（静默不匹配让「过声明检查 ⇒ 过提交」这条失去意义）；
**但先扫清单、先修库，检查在库里 0 条违规时才开启。**

## 扫到的三个数（2026-11-19，`lane-K-rule-access` @ `9b3869d66`）

| 数 | 值 |
|---|---|
| 两侧含访问算子的规则 | **15 条**（都可静态确证；全部在**右侧**） |
| 其中今天就在**静默不匹配**的 | **0 条**（172 条的左侧全是纯算子模式） |
| 库里的访问算子 | 只有两个：`:mark/map_arr`、`:mark/reduce_arr` |

`stdlib/rules.cml` 的 37 条（`std::opt` 的库）**一条都没有**；15 条全在求导/批处理的规则集里。

## 清单

访问算子取自数组的 `map`/`reduce`（`stdlib/prelude.cml:1045,1057` 以 `#op(':mark/map_arr')` /
`#op(':mark/reduce_arr')` 声明，`src/builtin/executors/builtin.cpp` 注册为
`.pure = false, .callsGraphs = true`，即"声明有效果但不逐项列出"⇒ 按「访问表非空」判即为访问）。

| # | 规则 | 文件 | 访问算子 | 经由 |
|---|---|---|---|---|
| 1 | `sum_axis_stated_t` | `stdlib/rulesets/transpose.cml` | `:mark/map_arr` | `lane_spread` → `lane_shape` |
| 2 | `max_axis_stated_t` | 同上 | `:mark/map_arr` | `lane_spread` → `lane_shape` |
| 3 | `min_axis_stated_t` | 同上 | `:mark/map_arr` | `lane_spread` → `lane_shape` |
| 4 | `mean_stated_t` | 同上 | `:mark/reduce_arr` | `extent_product` |
| 5 | `idx_arr_t` | 同上 | `:mark/reduce_arr` | `extent_product` |
| 6 | `mean_axis_from_type_t` | 同上 | `:mark/reduce_arr` | `mean_axis_cotangent_from_type` → `extent_product` |
| 7 | `vmap_rule_add` | `stdlib/rulesets/vmap.cml` | `:mark/map_arr` | `vmap_add` |
| 8 | `vmap_rule_sub` | 同上 | `:mark/map_arr` | `vmap_sub` |
| 9 | `vmap_rule_mul` | 同上 | `:mark/map_arr` | `vmap_mul` |
| 10 | `vmap_rule_div` | 同上 | `:mark/map_arr` | `vmap_div` |
| 11 | `vmap_rule_neg` | 同上 | `:mark/map_arr` | `vmap_neg` |
| 12 | `vmap_rule_reshape` | 同上 | `:mark/map_arr` | `vmap_reshape` → `batched_shape` |
| 13 | `vmap_rule_permute` | 同上 | `:mark/map_arr` | `vmap_permute` → `batched_perm` |
| 14 | `vmap_rule_dim` | 同上 | `:mark/map_arr` | `vmap_dim` |
| 15 | `probe_rule_mul` | `stdlib/rulesets/probe_batch.cml` | `:mark/map_arr` | `probe_scale` |

第 15 条是探针模块（`probe_batch.cml` 自己的注释说它是"the index refuses"的探针），不是发布库的一部分。

## 这三条数怎么读

1. **9.11 的判据不是「规则自己带访问声明」，也不是「施加时检查访问」。** 它是对规则文本的**静态禁止**，
   理由是不变量「过声明检查 ⇒ 过提交」。`#requires(...)` 是 7.2 的**数值许可**，与访问无关
   （`src/parse/ast/builder.cpp#Builder::visitRuleDecl` 把它读成 `PermissionMask`；应用时
   `src/runtime/writings_driver.cpp#DraftWritings::rewrite` 只判 `(rule.needs & ~context) != 0`，
   `context` = `tag::effectivePermissions`，即"**程序**有没有这条许可"）。
2. **15 条今天不红，也不静默失败**：访问算子在右侧，`build` 不查，所以它们照常构造出
   `:mark/map_arr` 节点；只有当同一区域另有对同一（全局）资源的无序访问时，才会被
   `src/runtime/resources.cpp#forEachUnorderedPair`（analysis 4.1 顺序完整）在**提交期**拒绝。
   这正是 9.11 要消灭的失败路径，但今天没有规则踩到提交期。
3. **15 条的"访问"是保守声明，不是真的读写。** `mark/map_arr` 的效果是"被传进去的函数做什么它就做什么"；
   这些规则传进去的都是纯 lambda。所以它们**不是**读写变量/对象/句柄的那种访问。按「访问表非空」的字面
   判据算 15 条；按"逐项列出的资源访问"判是 0 条。**这是这条问题的真正岔口**：检查要拦的是哪一种。

## 静态扫不出来的那一批（要编译器）

23 条规则的侧文本调用了 `sum` / `max` / `min` / `mean`（`jvp_sum`、`jvp_mean`、`jvp_max`、`jvp_min`、
`max_t`、`min_t`、`array_*_axis` 的 `vmap_rule_*` 等）。这些名字有**两个**可能绑定：`tensor:` 的纯张量算子，
或 `stdlib/array.cml` 的 `#impl(sum_kernel)` 等定义——后者的**函数体里就是 `extent_product`/`some_elements` +
`reduce`**。10.4 说规则是「需要看内部的遍」之一，会按需展开 `#impl`。所以这 23 条里有多少会在展开后带上
`:mark/reduce_arr`，只能由编译器答（本机没有能编译这棵树的 `camel.exe`：`D:\Projects\Camel\out\latest` 是
2026-11-12 的 `v0.2.45`，解析不了今天的 `stdlib/types.cml`）。

⇒ **要定数，必须先有一次「dump 每条规则两侧的算子」的远端读数**（一个临时用例 + 一个计划项，或一个
只读的 dump 工具）。在拿到它之前，"15 条"是下界，"23 + 15"是上界。

## 复现

```powershell
git grep -n "not a value a rewrite may read"        # 只有 src/runtime/writings_diag.cpp:56，test/ 下 0 命中
# 规则计数与 $op / 静态展开（本次扫法；同名函数只在规则自己的文件里解析，避免重载串味）：
#   D:\Temp\k-scan4.ps1  —— 每侧 -> 同文件函数体闭包 -> 叶子名 -> 标记算子
```

## 建议（默认）

**先不动检查。** 顺序：
1. 用一次远端读数把上界那 23 条定下来（临时用例 + `std::jgir`，或一个 dump 计划项）；
2. 按读数二选一：把 `mark/map_arr` 的访问表按"传入函数"说清（改声明），或把 15 条（以及第二批）的
   `map`/`reduce` 换成算子；
3. 库 clean 之后再加定义期检查；检查先做成**报告式**（`-W` 级、不红）跑一轮六计划，再转硬错误。

**不许为了让它绿而放宽判据**：如果 9.11 要的是"访问表非空"，那么 `mark/map_arr` 就是访问，
修的是库或声明，不是 9.11。

## 作者的裁定（2026-11-19）：判据写死，先报三个数

裁定原文要点：「**(b)**：报告式检查 + **一次报三个数** —— 开工」；「9.11 拦的是**真的插进一个读**，
不是『声明表非空』」；因为 `mark/map_arr` 传的是纯 lambda，所以「字面判 15，按 9.11 要拦的判 0」。
判据因此写死为：

> **右侧构造出的算子的实际效应不是纯**（`.pure = false` **且** 传进去的函数无法证明纯）。

于是三个数不是三个候选答案，而是**同一条判据下的三个量**：

| 数 | 值 | 它问的是什么 | 今天怎么读 |
|---|---|---|---|
| ① | **15** | 两侧文本**够得到**非纯算子的规则（按"访问表非空"的字面口径） | 上界口径的下界：量得出来，全部在右侧 |
| ② | **0** | 按裁定判据**真会插进一个读**的规则（`callsGraphs` 的算子传的全是纯 lambda） | 9.11 真正要拦的东西，今天**一条都没有** |
| ③ | **15～38** | 把 `sum`/`max`/`min`/`mean` 的 `#impl` 展开后仍算①的规则数 | **上界未量**：要在 `2d7c5c3a1` 上跑「dump 每条规则两侧算子 + `#impl` 是否展开」的探针才定得下来，这条读数本轮没取到（见下） |

`15～38` 的区间是 `15`（已确证）+ `23`（那批调 `sum`/`max`/`min`/`mean` 的规则，见上节）。
**区间中点不是数**：没有探针读数就不许写 26 或 27，只能写区间并注明「上界未量」。

## 检查落点与命令证明

定义期检查已落地，落在 `src/compile/gir/builder_diagnostics.cpp#Builder::reportRuleSideAccesses`
（走的是和 `reportEffectfulConditions` 同一条遍历：根图 + 各模块体，认 `rewrite:rule` /
`rewrite:rule_when` / `rewrite:rule_generic` 三种 maker），两侧的算子集合由
`#Builder::accessesOfGraph` 给（跟随 `Func` callee，`callsGraphs` 的算子再问一次其函数实参的图是否纯）。
警告号 `SemanticDiag::RuleSideHasAccess = 0x1001C4`，在 `diagnostics/registry.tsv` 里声明、由
`scripts/diagnostics-catalog.mjs --write` 生成表；`-W` 级、不红。环境变量 `CAMEL_RULE_ACCESS_REPORT`
把口径放宽到「访问表非空」（① 的口径），survey 就是这么取的。

用例：`test/cases/modules/rules/side_access.cml` + `test/plans/feat/modules/rules_side_access.plan.toml`
（一条会插进读的规则必须报，一条纯 lambda 的规则必须不报）。

**③ 为什么本轮没量到**：`CAMEL_RULE_ACCESS_REPORT` 是进程环境变量，而远端闸门
`internal/tools/camel-remote.ps1` 只能传文件、计划和 `-Check`，没有设置环境变量的入口；把调查口径做成
仓库检查器要改 `test/tools`（墙），不在本 lane 的范围里。⇒ 本页按裁定的退路报 `15～38` 并写明**上界未量**。