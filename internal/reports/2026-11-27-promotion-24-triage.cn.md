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