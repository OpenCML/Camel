# E9 的错值在 HEAD 上已经全绿：那条催办的「第一个真正修好的错值」应该怎么算？

> 一页一问。**默认建议写死在下面，本轮按默认做完，不另等。**
> 本页是 `lane-e9fix` 对催办（`f55eba445`，2026-11-16 15:50）的答复。

## 结论（先写这一行）

```
E9's wrong values are already green on HEAD (132/132/0, RUN_EXIT=0).
The value reproduced only on pin 29ea0a011.
A16d: red at 29ea0a011 / 62cfb2218 / 4c6bff674 / 87ce6b302 / 577a68004 / 4131debcc, green at
      eac93478e; the last red and the first green sit two hops apart, and the hop between them is
      the merge 55d8c1d12 (origin/develop 0b34a2de0) -- the fix came to this line through that
      merge, not through a lane commit.
A16a and A16e: red at bbaf8a2b2, green at HEAD; 66491c3c7 is the one place-handover commit in that
      window and its own body records the eight cells it turned green.
No change to src/passes/opt/ was needed, and none was made to manufacture a red.
```

## 这一页要的答复

催办说「E9 的错值仍是零修复」，并要求「下一次合并请交出第一个真正修好的错值（从最先出错的遍开始：A16a `fold` 两次打印对调、A16d `dce` 删掉会打印的调用）」。

**这两处在 `origin/develop` `1ab776c4` 上的读数都是对的。** 不是「标了遍名」，是程序打印出来的东西本来就是程序本来的值：

| 程序 | 遍 | 应该打印 | 现在打印（`1ab776c4`） |
|---|---|---|---|
| `rc07_seed140`（A16d） | `std::opt::inline std::opt::dce` | `"\n\n"` | `"\n\n"` |
| `rc02_seed9`（A16a） | `std::opt::specialize std::opt::inline std::opt::fold` | `"q36 0\nq36 10\n"` | `"q36 0\nq36 10\n"` |

所以「第一个真正修好的错值」在这棵树上**没有红可以转绿**：要交出一处修复，就只能先造一个红，而那正是纪律禁止的事。

**这一页要问的是**：催办的验收口径是不是改成下面这条——**把「修好」定义为「在 pin 上复现、在 HEAD 上不复现，并把修好它的那一笔定位到具体提交」**，而不是「本车道再改一次 `src/passes/opt/`」。

## 读数（每条都带名单，不是行数）

**1. E9 完整跑（`798d19ed7` 的口径：一个计划文件跑满，33 条声明 × 4 后端 nvm/fvm/jit/prl）**

```
pwsh -NoProfile -File D:\Projects\Camel\internal\tools\camel-remote.ps1 `
     -Path D:\Projects\Camel-wt\e9fix -Plan test/plans/feat/opt/e9.plan.toml -Jobs 8
```

`[SUMMARY] total: 132, pass: 132, fail: 0`，`RUN_EXIT=0`，`[TREE] clean`。**132 / 132 / 0。**
按作者那一套分类（`questions-2026-10-29.md`「判定」）记：**错值 0、无位置拒绝 0、崩溃 0。**

**2. 五个 A16 程序逐格（不只计划里那一条前缀）**

在 `std::nvm` 上按「`std::macro` 之后依次加 devirtualize / specialize / inline / fold / cse / algebra / dce / order」的口径扫前缀，另加 `std::opt` 整条：

```
rc02_seed9   (A16a)  none / +fold / inline+fold / specialize+inline+fold / std::opt
                     → 每一格都是 "q36 0\nq36 10\n"                      （5 格全对）
rc07_seed140 (A16d)  none / +dce / inline / inline+dce / inline+fold+dce / std::opt
                     → 每一格都是 "\n\n"                                 （6 格全对）
rc04_seed251 (A16b)  none / inline / inline+fold / std::opt              → 每一格 "\n"     （4 格全对）
rc05_seed256 (A16c)  none / inline / fold / std::opt                     → 每一格 "\n"     （4 格全对）
rc09_seed15  (A16e)  none / +fold / inline+fold / std::opt               → 每一格 "L4 0 \n\n"（4 格全对）
```

（两个计划文件合成这 23 格：`zz_diag_e9` 的 18 格与 `zz_diag_shapes`/矩阵里同一形状的重复格；矩阵那 88 格是它的超集，见第 3 条。）

**3. 88 格矩阵**（五个程序 × 上面前缀 × `{generational, mark-sweep}` × `{std::nvm, std::prl}`）：**88/88 打印各自程序本来的值**，一格不差。

**4. 错值只存在于 pin `29ea0a011`**（同一份 `rc07_seed140.cml`，`std::nvm`，`--gc generational`）：

```
29ea0a011  none                  → \n\n     对
29ea0a011  +dce                  → \n\n     对
29ea0a011  +fold                 → \n\n     对
29ea0a011  inline + dce          → \n       错（应为 \n\n）—— A16d 在 pin 上复现
```

**这条读数本身就是催办前提过期的证据**：错值只在 pin 上，而且是 `inline` 与 `dce` 两个遍一起才出现的那一格（单独 `dce`、单独 `fold` 都不错），所以「修好它」的判据也只能是「这一格从 1 个空行变回 2 个空行」。

**5. 定位（bisect，判据就是上面那一格）**

同一份 `rc07_seed140.cml`、`std::nvm`、`--gc generational`，`std::opt::inline std::opt::dce` 一格：

```
29ea0a011                   → "\n"      错   （pin，A16d 在这里）
62cfb2218  2026-11-14       → "\n"      错   （"hand the place of a folded statement to the statement
                                              after it" —— 不是它）
4c6bff674  2026-11-15       → "\n"      错   （keep the ports an unread erase keeps —— 不是它）
87ce6b302  2026-11-15       → "\n"      错   （live-set 那条线的合并 —— 不是它）
577a68004  2026-11-15       → "\n"      错   （撤聚合守卫 —— 不是它）
4131debcc  2026-11-15       → "\n"      错   （"splice only where the chain head's place can be
                                              handed on" —— **也不是它**）
eac93478e  2026-11-15       → "\n\n"    对   （合并；把 55d8c1d12 那一侧引进来）
d33cbe361  2026-11-15       → "\n\n"    对
bbaf8a2b2  2026-11-15       → "\n\n"    对   （= 66491c3c7 的父）
1ab776c4   2026-11-16 HEAD  → "\n\n"    对
```

**结论：红绿之间只剩两跳**——最后红的是 `4131debcc`，最先绿的是 `eac93478e`，而这两笔之间只有
`55d8c1d12`（`Merge origin/develop (0b34a2de0): the refuse line, the regions readings, the
plan-layering checks`）和四个文档提交。**也就是说 A16d 是跟着那次 develop 合并进到这一线上的，
不是哪一笔车道提交单独修的。**

`55d8c1d12` 两侧之间的代码提交（`git log 4131debcc..0b34a2de0` 里触及 `src/passes`、`src/runtime`、
`src/execute` 的那些）名单如下，**下一笔要试的就是它们**，其中最像的是「order edge」那两笔：

```
0b34a2de0  Give every region check the commit path makes a reading that fails when the check is loosened
465ae09a7  Wait for every order edge in a structured plan, and let a rendezvous say the point it meets
           at is unordered                                   ← 顺序边：会打印的语句的位置正是靠它保住的
17345284c  Wait for every order edge in the structured plan, and pin a program one such edge orders
4cc0b36ba  Judge a dead write once, ask what makes a reader alive, and ask the cell a write reaches
           through a GATE
cdd06c951  Ask the cell a write reaches through a GATE, not the GATE
0170a4096  fix: keep the ports a control region is entered with when an unread node would be erased
3eaff4cce  Hold four E9 reduced programs where they aborted the optimizer, ...
fc0312741  Judge a dead write once, and let admission ask that judgement
a879d0c63  Carry a region's exit with the value that replaces it, so a loop's read of its own body
           stays inside
2ebfa45b3  std::opt: say when the round bound stops it, and which function did not settle
```
（`4c6bff674` 也在这条线上，但已经单独测过是红的，所以不在嫌疑里。）

**为什么没把它压到一笔**：这一段的每一跳要重建一次并跑一次读数，今天主机 1 分钟负载 20~28、
每次排队等 200~800 秒，单跳 10~14 分钟；本页是在这条线上第 9 次读数后停下的，**不是放弃了定位**——
判据、命令、计划文件与读数格式都在这页里，下一次从 `465ae09a7` 起试即可。

**6. A16a 与 A16e 的两次读数**

```
bbaf8a2b2（66491c3c7 的父）  rc02_seed9  specialize+inline+fold → "q36 10\nq36 0\n"   错（对调）
bbaf8a2b2                   rc09_seed15 inline+fold              → "\nL4 0 \n"        错（对调）
1ab776c4 (HEAD)             rc02_seed9  同上                      → "q36 0\nq36 10\n"  对
1ab776c4 (HEAD)             rc09_seed15 同上                      → "L4 0 \n\n"        对
```

父上对调、HEAD 上不再对调；`bbaf8a2b2..HEAD` 这一段的 34 笔里只有 `66491c3c7` 动了 `fold` 的交接位置
（`git log bbaf8a2b2..1ab776c4 -- src/passes/opt`），它自己的正文也记着 A16a/A16e 八个格由它转绿。

## 默认建议（已按此做完，不另等）

1. **不为零读数改 `src/passes/opt/`。** 今天改它只能是把「先造一个红再修」写进代码，那是纪律明令禁止的。
2. **交出定位**：用与上面第 4 条同一份读数，在 `29ea0a011` 与 HEAD 之间 bisect，把「哪一笔让这一格从 1 变回 2」定到具体提交；A16a 同样用两次读数钉住 `66491c3c7`。**定位比再改一遍更值**：催办说「没有一处修复」，而事实是「修好它的那一笔已经在了，只是没人指出来」。
3. **三个数照口径报**，入口命令写全（见上）。

## 7. E9 完整跑（500 种子）：跑法与口径先说清

**跑法（工具自带，不是计划）**：`test/tools/e9diff/e9diff.py`，三个档 fast / slow / full：

```
python test/tools/e9diff/e9diff.py run --matrix full --out /tmp/e9-full --camel <install>
python test/tools/e9diff/e9diff.py analyze --out /tmp/e9-full
```

full = 种子 `1..500`、99 格（11 条流水线 × 4 后端 × 2 回收器 + `std::cpp`）= 31,680 格；`--jobs 4` 在
**安静**的 32 核主机上 25 分钟；**今天主机 1 分钟负载 20~28**（其它车道在跑），实际会显著更久。

**这条跑法进不了本车道的闸门入口**，这是要先说的第一件事：

- `internal/tools/camel-remote.ps1` 的 `-Check` 表里**没有 e9 这一项**（表是 `diagnostics-catalog`、
  `camel-for-ai`、`check-doc-links`、`headers`、`plan-dir-markers`、`check-grammar`、`stdlib-reference`、
  `conformance`、`export-matrix`、`model-generators`、`rule-proofs`、`plan-debt`、`gate-static`、
  `plan-fields`、`ast-hashes`，加 report-only 的那几个），`test/plans/` 下也没有任何计划包着它；
- 所以一个车道要跑它，只能自己手写 ssh 命令行，而本车道的纪律与 `camel-gate.local.json` 的设计都要求
  「闸门入口只认 `internal/tools/camel-remote.ps1`」，而且它还要占主机那道测试互斥量。

**第二件事是口径**：`test/tools/e9diff/baseline.json` 里那只棘轮的读数是

```
programs_wrong 10   programs_refused 17   programs_crashed 3
cells_wrong   321   cells_refused   747   cells_crashed    3
pin a8df99c13 (2026-11-14) + 提交时带进 test/ 的五个未提交文件   matrix full   seeds 1..500
500 个程序里 180 个是生成器的废品，矩阵里 320 个
```

而作者要对照的 **19 / 21 / 12+2 是 CC 那套 harness 在 `29ea0a011` 上的数**（`questions-2026-10-29.md`
「基线」）。**两套 harness、两个 pin，两个数不能并排放。** 所以「与 11-14 基线对比」这件事，退一步的
做法是：**先跑一次 full，与 `baseline.json` 的 10 / 17 / 3 比**（这是仓库里那只棘轮的定义），并在提交
说明里同时写「这次是哪套 harness、哪些种子、哪个 pin」，不把它和 19 / 21 / 12+2 相减。

## 若作者另有裁定

- **若说「口径不变，仍要本车道在 HEAD 上再改一处 `src/passes/opt/`」**：请指一处**今天读数为错**的程序（仓库内或 E9 慢层里的种子都行）。没有具体的红，改遍只能靠猜，而且会把「没红也能交修复」变成可接受的做法。
- **若说「三个数应当按完整跑（500 种子 × 99 格）报错值/拒绝/崩溃，而不是计划文件的 pass/fail」**：那需要 `test/tools/` 下的 E9 工具与生成器，本车道的禁止项里有 `test/tools/*`，请另派或解禁；今天的 132/132/0 是 `798d19ed7` 自己用的口径，我照它报，是为了能对上账。
- **若说「pin 上其实不复现，你的读数有问题」**：上面第 4 条的四行可以原样重跑，`-Plan` 指一个只有这四格的计划文件，几秒钟出读数。

## 这一页不改变的事

- **A16 程序的期望输出一个字节没动**（判据本身）。
- `captures.cpp` / `decideCaptureByValue`（`lane-captedge`）、`test/cases/linear/byref/*` 与 `consistency/new_values/*`（`lane-rewrite`）、`test/tools/*` 一律没碰。
- 工作树 `D:\Projects\Camel-wt\e9fix`，分支 `lane-e9fix`，基于 `origin/develop` `1ab776c4`。