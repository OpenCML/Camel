# 一个用例带着 `func main` 却没有任何计划条目：删掉，还是给它写一条？

## 问什么

`test/cases/modules/autodiff/probe_library_grad.cml` 有 `func main`，**`test/plans/` 下任何一个
`[[tests]]` 表都没点过它**。它今天在台账里的状态是 `want = none`（2026-11-04 起）：列出来、不算过。
问题是这一条该往哪去——它不是"再记一次账"，是"这条用例还要不要"。

同一目录还有三条没有计划条目的（`vjp_lib.cml`、`vjp_user_a.cml`、`vjp_user_b.cml`），这三条
**没有 `func main`**：`vjp_cross_module.cml` import 它们，它们是被它带着跑的子模块。所以那三条
不是问题，问题只有这一条。

## 读数（可复核）

- 台账的扫描在 2026-11-04 从"只看 `test/plans/feat/modules/*.plan.toml`"扩到"`test/plans` 下所有
  `*.plan.toml`"（`scripts/autodiff-migration-ledger.mjs#PLAN_ROOT`）：132 个用例文件里 **128** 条被
  某条计划点过、**4** 条一条都没被点过——就是上面这四条。扩扫描之后没有一条"找得到计划却定位不到"
  的用例（128 条点名、128 条命中），所以这 4 条不是扫描漏了，是**真没有**。
- `probe_library_grad.cml` 在树的别处只被一个文件提到：`docs/handover/m4-autodiff-migration-table.cn.md`
  的逐条表第 93 行（"不过 / 写法：`grad(f)<x>()` + 被求导的是 `with` 函数"）。没有计划、没有
  `docs/` 引用、没有别的用例 import 它。
- 它的文件头说自己是"the precondition's own program for the implementation comparison of the C2
  example"（`showcase/c2-mlp-mnist/compare_implementations.py` 那条路）。也就是说它当初是给一次
  双实现对照用的程序，不是给计划用的。
- **它今天在这个构建上 exit 1**（`out/latest/bin/camel` 2026-11-03 构建，宿主 `88zjwei`，用台账同一条
  改写规则）。所以 2026-11-04 之前台账按 `expect_exit = 0` 量它，一直记"不过"——它从来没有真被算成
  过，但它**被一条没人写过的期望量过**，而且它在 `library` 这个分母里（`library=123` 含它）。

## 三条路，各带代价

**A. 删掉它**（连同本页一起关掉）
- 代价：一条 exit 1 的程序消失，而它 exit 1 的原因没人读过——如果那是 `vjp` 该拒绝的东西，删掉就是
  把一条（没人用的）守卫丢掉；`m4-autodiff-migration-table.cn.md` 第 93 行要跟着删/标注。
- 好处：分母少一条，`total 132 → 131`、`library 123 → 122`（两个分母都是**决定**，所以我没动它）。

**B. 给它写一条计划条目，钉它现在真的做什么**
- 先要把它的 exit 1 读成一句人话：是拒绝（那就 `expect_exit = 1` 并 `expect_output_contains` 住那句
  诊断），还是它就是坏了（那就先修/先删）。
- 代价：得有人判它"应该"是什么；在没判之前钉一条 `expect_exit = 1` 是把一个**不知道**写成"正确"，
  这正是 2026-11-04 这一笔在修的毛病（拿错的期望量用例）。
- 好处：从"目录里躺着的程序"变成"计划跑的一条用例"，重新有人看着它。

**C. 维持 2026-11-04 的状态（本轮按这个）**
- 台账列它、打 `want = none`、**不算过**，也不当它过。
- 代价：一条 exit 1 的程序继续没人看；它继续占 `library` 分母一格。
- 好处：不改行为、不动分母、不钉一条没人判过的期望——**不猜**。

## 默认（本轮按这个开工，不等回复）

**走 C**，并且 C 已经把"静默当成通过"那条堵住了：`want = none` 的用例在
`scripts/autodiff-migration-ledger.mjs#rows` 里 `matched = Boolean(test) && …`，没有计划条目就永远
不算过；表格的 `want` 列打 `none`，表下逐条点名。于是 C 不是"什么都不做"，是"把猜出来的那一格改成
明写的 none"。

A 与 B 不是这条车道的判断题：A 要动分母（`total`/`library` 是作者的决定），B 要先有一条关于
"这个程序应该做什么"的裁定。作者若裁 A，删文件、把 `m4` 那一行跟着删、`LEDGER_RECORDED` 的两个
分母各减一并在提交里说明；若裁 B，先把它 exit 1 的那句诊断读出来写进计划，再加入口，`pass` 会
跟着 +1。