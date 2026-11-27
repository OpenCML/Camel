# 第二十四次晋级的判词与处置（promo42，钉 P 全快层）

日期：2026-11-27（下午）　签名读数的人：晋级位（seat-merge-2）

## 一、这一轮的量

```
[provenance] tip 35c9f3e077f0ef6d6471439a2676c54966a12880 | binary built 2026-11-27T14:22:17+08:00 | tree tree32 | tier fast | jobs 7
BUILD_EXIT=0   RUN_EXIT=1
[SUMMARY] total 9853 / pass 9616 / fail 176 / skip 28 / known failures 33 / killed by a signal 9
[SPLIT] build 619.5 s, plan 580.3 s, total 1204 s
[load] 1m 67.83，运行中到过 91.61 ⇒ 非静载
```

上一次晋级（`promotion-23`，tip `ef2cd2eb63`）的记录是 `total 9830 / pass 9582 / fail 187 / known 33 / killed 0`。

## 二、判词口径（决策者 `D-1127-97`）

被信号杀掉的不许“单列、不计新增红”：**崩溃就是红**。`killed` 要按信号分类：
运行器超时发的 `SIGKILL`（安静时段串行重跑能过 ⇒ 环境）与 `SIGSEGV`/`SIGABRT`（真崩 ⇒ 新红，二分）。

## 三、逐名比

```
对 promotion-23 的红表（187 名）：still_red 164 / turned_green 23 / new_and_deterministic 12 / skip 28
```

九条 `killed` 的信号分类：**9/9 全是 `SIGABRT (rc=134)`，没有一条 `SIGKILL`** ⇒ 按上述口径全部计入新增红。

## 四、12 条新增红的来源（不是散红，两笔各一类）

```
9 条 SIGABRT ← b073860954  compile: a block's root imports the module whose operator opens it
               （属 e467f20578 Merge lane/editview-gir 这一枝，是该枝第四笔）
               abort 断言行：gir/builder_expr.cpp:958 resolveDRefNode → templates.has_value() 失败
               Reading: The module that declares the operator the compiler emits did not register it: graph:open
3 条 exit 1  ← 同枝连带（editview.check_view_head / syntax_only_semantic / view_semantic）
12 条普通红 ← d6606a2e06  该枝第一笔；其父提交 a4a2b5ebe4 为 17 pass / 1 fail，
               即这 12 条是该特性探针没做完就合入，与 abort 不同源
1 条快照红  ← parse.frontend/ast_record（ast 记录要随 block-root 节点重生成，可按 promo22 先例重生成）
```

`fault_deep.prl_off` 累计 5 次：3 绿 2 红，红灯也是 `SIGABRT` 同族 ⇒ 判 **unstable**，不进 known、不挡快进，
但修复后必须复跑三次。

## 五、处置

1. 修复车道时间盒一轮：让 `visitBlockRoot` 问的 URI 与 `recordOperatorTemplates`/`operatorGroups_` 的 intern
   来自同一处 `intern(...)`，并让 `operatorGroupOf` 在 builtins 与 `allUserModules()` 之外按算子 URI 的模块名
   再问一次组；`importModule("graph", module_.get())` 的 `module_` 为空路径必须处理。
   验收：probe 两组 `killed = 0`、`feat/compile 2724/2705/16`、`smoke/core 10/10`、`feat/runtime` 逐数不变。
2. 修复不成功 ⇒ `revert -m 1 e467f20578` 整枝撤（同时带走 9 条 abort 与 12 条普通红），
   判词写成“新增红 12 条全部由该枝带来、随该枝撤回 ⇒ 本轮新增红 0”。
3. `frontend.ast_record` 单独按 promo22 先例重生成，逐行说明只是节点形态变化。
4. 撤前先列影响面与冲突点；撤后立刻跑一次全快层与 promo42 的 176 名逐名比，确认撤本身没带出新名。

## 六、这一轮不快进

三项判据（176 名逐名比、9 条按信号分类、`fault_deep.prl_off` 三次）里前两项已出，第三项判 unstable；
新增红 12 条未清零前 `develop` 不快进。

## 七、撤掉那一枝之后的四次测量

撤的是 `e467f20578`（`Merge lane/editview-gir: a block head's binding is the block's root, opened by
its kind`），`git revert -m 1` 成一笔 `e8a120c287`，在 `249c85dda1` 上单笔 merge 成 `bb41152893`
（`Lane-Reading: check=targeted-plans total=158 pass=157 fail=1 red=[probe.editview|editview.sec2_log] state=read`，
那条红在 promo40 的 `6a78c46c64` 上就已经红，属 promotion 23 的既有名）。撤之前后在撤出的树上
`BUILD_EXIT=0`（全量 1,180 s，下游 `pass-decl-gate` 与 `footprint-once` 都编过）。

| 读数 | 钉住的 tip | total | pass | fail | skip | known | killed | 对 promotion-23 的 187 名逐名比 |
|---|---|---|---|---|---|---|---|---|
| promo43 | `bb41152893` | 9855 | 9632 | 162 | 28 | 33 | **0** | still 161 / gone 26 / new 1 |
| promo46 | `22afbf1d0c` | 9857 | 9632 | 164 | 28 | 33 | **0** | still 162 / gone 25 / new 2 |
| promo48 | `c0f6a832d2` | 9861 | 9636 | 164 | 28 | 33 | **0** | still 161 / gone 26 / **new 3** |

`promo43` 的 14 条转绿正是撤走的那 12 条（9 条 `SIGABRT` + 3 条 `exit 1`）与另外两条；撤之后
`killed by a signal` 在三次读数里都是 0。

## 八、三条新名的处置（都落在 `promo48` 的 P 之前）

1. **`parse.frontend|frontend.ast_record`** —— 记录过期，不是产品红。撤走后 `check-ast-hashes.mjs`
   在 `22afbf1d0c` 上报 `15 stale (15 mismatch, 0 missing, 0 gone)`，全部是
   `test/cases/probe/editview/*.cml` 加一条 `test/cases/probe/blockscope/imported_type_in_body.cml`
   （块根那枝合进来时记的形态，撤走后又要变回去）。重录用 `--update --only` 分两次做（先
   `test/cases/probe/editview/*`，再那一条），重录后 `0 stale of 4636`。提交 `0476066d2d`。
2. **`runtime.graph_draft_ledger.never_rises`** —— 结构账，不是产品红。原文：
   `[LEDGER] RED entered files: src/stdlib/std/macro/rewrite.cpp (first at rewrite.cpp:83:
   camel::runtime::GraphDraft::NamedEdit edit(draft, camel::runtime::EditKind::Repla…))`。
   工具给的两条路是拿掉或 `--record --force` 记进 baseline；按 `check.mjs:48-57` 的口径成员身份按角色判，
   `NamedEdit` 是 `GraphDraft` 的嵌套类型、只能以被删类名构造，没有工厂与别名，`decltype` 藏名字是
   `check.mjs:52-54` 明确否掉的写法 ⇒ **只能记基线**。`baseline.json` +1 行
   `"src/stdlib/std/macro/rewrite.cpp"`，提交 `649c9395e1`，说明里写了为什么它留下（提交分档与
   “一次编辑一次通知”）、为什么只有这一处（`txn.cpp` 有 8 处同入口，范围留给移除类型那一步）。
3. **`tools.selfchecks|selfchecks.merge_batch`** —— 工具自检（元层），不是产品红。根因是 fixture
   复制真钩子后钩子只看自己旁边、找不到 `scripts/common.js` 就拒推；修在 `0e89c42942`
   （推送钩子读被推送检出自己的 `common.js`，没有就不查）。**这条在单跑与合批里读数不同**：
   在 `c0f6a832d2` 上单跑 `node --test test/tools/merge-batch.test.mjs` 曾报
   `# FAIL with --push the remote integration is the tool's tip` / `20 checks, 1 failed`，
   而在同一棵树的整快层 8 路并发里它**是绿的**（`promo48` 的 gone 名单里有它）。
   ⇒ 这是一条**在负载下不稳的测试**，两种读数都真实存在，写在这里而不是被“排除”。

`promo48` 对上一个 P（`22afbf1d0c`，164 名）是 **still 161 / gone 3 / new 3**，三条 gone 正是上面
这三条 ⇒ 修复在整快层里生效。

## 九、promo48 的三条新红：新增用例，不是回归

```
pools|probe.avg_reverse.nvm    expected exit 0, got exit 1
pools|probe.max_forward.nvm    expected exit 0, got exit 1 | output does not contain
                               'probe max_forward value=5.599999904632568 tangent=-0.9000000357627869
                                expected=-0.9000000357627869 diff=0.0 forward_ok=true'
pools|probe.max_reverse.nvm    expected exit 0, got exit 1
```

三条都来自 `lane/probe-evidence` 那批新加的探针用例（同一批新 `.cml` 里的三条），在 promotion 23
与 `22afbf1d0c` 的红表里都不存在 ⇒ 按名字级比较它们是 new，按性质它们是“新用例还没做完”。

## 十、结论与留给决策者的一条

`promo48`：`total 9861 / pass 9636 / fail 164 / skip 28 / known 33 / killed 0`，
对 promotion-23 **still 161 / gone 26 / new 3**。门禁要求 `comparison.new_reds` 为 0，
`3 > 0` ⇒ **这一轮不发布，`develop` 停在 `ef2cd2eb63`。**

留给决策者的结构性问题：晋级判据是名字级的 `new = 0`，而计划树每天都在进新用例，这一轮
“修掉 3 条、新进 3 条”正好把 `fail` 抵平在 164。只要新用例的红与既有用例的回归混在同一个
名字集里，晋级就会反复卡在“昨天之后新加的用例里恰好有红的”。两条可选口径：

1. 新用例的红单独计一类（“新用例未做完”），门禁只拦既有用例的回归；
2. 新用例合入前必须自己先跑绿。

两条都要由决策者定，不由签名读数的人定；在此之前，这一轮的红表就是 `164 名`，逐名比的三列
是 `161 / 26 / 3`。

## 十一、这三条红的根因（比红本身有价值）

`test/plans/probe/pools.plan.toml` 的头两行写着自己的身份：

```
# Untracked probe plan: the two walls the pooling laws wait at, measured with the transform trace on.
# ... Not part of the tree's plans.
```

而它放在 `test/plans/probe/` 下，整快层按 `-Plan test/plans` 收计划，于是这份**自称不属于计划树**
的探针计划被当成计划收了：三个 `[[tests]]` 被跑，三条都是 `expected exit 0, got exit 1`。把它收进
来的是 `70fbfad2d4`（15:11，把 17 个测量物一起收进仓库的那一笔）——它想留住的是测量物，不是
“这三条探针已经能跑绿”。

处置不由签名读数的人定。按决策者 `D-1127-130` 的口径，这类红只有三种身份：探针写错或断言过期
⇒ 改到单跑全绿；有意钉住未修缺陷 ⇒ 登记进已知红清单（写明 backlog 行与退出条件）；环境依赖
⇒ 用例自带跳过条件。**用 tier 把红遮出快层不在这三种之内**；`selfchecks.merge_batch` 可以那样处置，
是因为它的红依赖墙钟与并发负载，而这里的 `--trace transforms` 的 `exit 1` 不是负载问题。

这条留在页里，是因为“自称不在计划树里的计划放在计划树里”比那三条红更容易复发。
