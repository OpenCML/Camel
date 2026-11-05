# 协调者那 15 条待办，逐条对树验一遍（`cf9c4495d`）

**这份页的判据**：清单上的措辞今天还像不像个问题不作数，**树里有没有那个东西**作数。每条给一条能复核的读数，判定
**open / closed / 判不了**；closed 写出关掉它的那一笔，open 写出剩下的是哪一步。读数取自 `origin/develop`
`cf9c4495d`（11-05 14:0x，工作树 `D:\Projects\Camel-wt\lane-forkorder` 与主检出同提交）。**一条都没落成「判不了」。**

| # | 判定 | 读数 | 关掉它的那一笔 / 剩下哪一步 |
|---|---|---|---|
| 1 | closed | `node scripts/test.js test/plans/feat/showcase_s2.plan.toml`（主机、`-NoBuild`）`[SUMMARY] total: 10, pass: 10, fail: 0` | 验收那一条是绿的；extents 的线以另一种形态回来了：`e3e1e0631`「`sum_to` takes its extents as an argument」+ `da17411ea`「A `sum_to` whose target is a constant states the extents」。**不是把 `821f30c83` 整条放回**，这正是那份 revert 报告证的 |
| 2 | closed | `git grep -nE "sum_to_at"` → 空（名字在树里不存在）；`git log -1 e3e1e0631` 的标题即裁定「甲」的形态 | `e3e1e0631` + `da17411ea`：`sum_to` 自己拿 extents，ruleset 私有 helper 不用再代劳 |
| 3 | open | `modules/autodiff/pullback.cpp:5055 GradientGraph buildGradientGraph(`、同文件 `:2775` 调用点、`modules/autodiff/operators.cpp:41 class GradResolver`、`:160` 调 `buildGradientGraph` —— 三样都还在 | **剩：删引擎那条路**。库层那半边已落（`48e0327c7`「S2 trains twelve rounds on the library's reverse transform」，`feat/showcase_s2` 的 `train.twelve_rounds_library_curve_is_the_engine_curve` 绿） |
| 4 | closed | `git log -1 1bd7c7176`：「these are the eight that the pending page counted as green assertions pinning a literal number」；改的 4 个 plan：`rules_angle_args`、`transforms`、`showcase_s2`、`split` | `1bd7c7176`（lane `lane-goldnam`，`e98ffbb37`）：位置写 `<>`、整行 `^...$` 锚定、要求恰好一行命中 |
| 5 | open | 前半已落：`git log -1 d08bc5f63`；后半没有用例 —— `cf9c4495d` 自己的标题就写着「no case has ever asserted that a cotangent is an array」 | **剩：`f32[3]` 那条用例**（重载拒绝的三句话里，第二句要靠它才被钉住） |
| 6 | closed | `src/builtin/operators/struct.cpp:145`：「This is the one `extent_of` special case; there is no second spelling of it anywhere in the builder（`Builder::visitLinkNode`、`Builder::functionValueOf`）」 | `4d5e7d9a2`「extent_of answers from the type wherever its name is used, and its refusal names the open position」 |
| 7 | closed | `test/plans/feat/opt/inline_cycle.plan.toml:34` 的 `case` 就是 `../../../cases/opt/inline_cycle/seed23_fold_inline.cml` | `2040d78ed`「Guard the splice place that progen seed 23 was refused on」 |
| 8 | closed | 补前（11-02 老构建 `d2b27967e`，即四类边的树）`test/plans/feat/opt/adb67e081.plan.toml` → `[SUMMARY] total: 4, pass: 2, fail: 2`，rc04 红：`node 24 (11): lies on a cycle of inputs: 24(11) <- 27(13) <- 38(13) <- 24`；补后（`cf9c4495d`）→ `[SUMMARY] total: 4, pass: 4, fail: 0` | `bf12366ba`（合并 `d603372fc`）：`resolvePlaceForks` 的 `waitsOn` 走 norm/with/ctrl/**order**/sync 五组 |
| 9 | closed | `test/plans/feat/linear/jit_framed.plan.toml:223`：「The whole line is pinned, not its prefix」 | `99bd41650`：整行（256 字符）逐字节钉死，两次运行一致 |
| 10 | closed | `test/plan-selection.json` 的 comment：「an unknown selection never becomes a layer」，读不到基线是 exit 2 | `6018535e5`「test(gate): refuse an unreadable change instead of running the whole fast layer」 |
| 11 | closed | `git ls-files "*camel-remote.ps1"` → 只有 `internal/tools/camel-remote.ps1` 一份 | `20a2eaacb`。注：各工作树根下那些副本仍在磁盘上，但**不在版本树里** |
| 12 | closed | 全层（主机 `-NoBuild`，`test/plans/smoke` + `test/plans/feat`）`[SUMMARY] total: 8851, pass: 8806, fail: 0, skip: 11, known failures: 34`；跑器对「已标记却通过」是判 fail 并要人拆标记（`test/tools/run-tests.mjs:3592-3593`），`scripts/check-plan-debt.mjs:81` 再拒一次 | `fail: 0` ⇒ 34 条标记**全部仍然真的红**，没有一条过期 |
| 13 | closed | `src/parse/syntax/parser_type.cpp:319,334` 在类型位置读 `...`/`..`（`(...E)` 是成员展开的元组，不是括号类型）；`include/camel/core/type/typecall.h:87` 记 `case (W, ...Rest)` 得到「最后一个成员是 `...Rest` 的元组类型」 | `97ed2e6ce`「feat(types): type packs, ParamsOf/WithOf/ResultOf, and zip as a declaration」 |
| 14 | open | `scripts/autodiff-migration-ledger.mjs:382` 拿**原始行** `ARRAY_SPELLING.test(line)`（`:364` 拿整个文件源），**没有一步去注释** —— 注释里的 `f32` 计入 | **剩：分类/计数前先剥 `//`**，然后重数。`05e1145a9` 只是把已经被那句 prose 带偏的那条用例挪了类 |
| 15 | open | `include/camel/core/type/tree.h:44 arity()` 原话「answers a non-tree type with whatever its bytes say」，`:101/:124/:133` 三个调用点仍走它；`:56 arityOf` 是 `d14575b55` 新加的无答案形态 | **剩：把最后三个调用点搬到 `arityOf`**，`arity` 才能收掉（`docs/handover/reports/2026-11-05-arity-has-no-answer-for-an-array-and-no-case-reaches-an-array-cotangent.md` 自己就是这么记的欠账） |

## 复核方式

```powershell
git -C D:\Projects\Camel log -1 --format=%s <上面每一笔>
git -C D:\Projects\Camel grep -nE "<上面每条的符号>"
pwsh -NoProfile -File D:\Projects\Camel\internal\tools\camel-remote.ps1 `
     -Path D:\Projects\Camel-wt\lane-forkorder -Plan test/plans/smoke,test/plans/feat -Jobs 8 -NoBuild
```

第 8 条的补前读数用的是本机 `out/latest`（`build-info.json` 的 `revision` 是 `d2b27967e`，11-02 构建，**早于**
`bf12366ba`）；补后是主机在 `cf9c4495d` 上重新构建的同一条 plan 文件。两次都是同一份 `rc04_seed72.cml`，唯一变量是那道守卫。

第 1 条的 `[SUMMARY] total: 10` 与第 8 条的 `total: 4` 是不同 plan 文件各自的条数，不是同一层的两次读数。