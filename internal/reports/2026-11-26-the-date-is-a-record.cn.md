# 位置标记：日期是"何时"的记录 —— 30 行抽样、一条逐行判据、71 行按它删掉

**状态：已落。** 读数 732 → 662。检查器的判据**一条未改**；改的是注释本身。本页记四件事：
派工给的前提为什么没能复现、30 行抽样逐行怎么判、判出来的四个形状、以及按这四个形状能删
什么、为什么其余不能删。

## 一、读数

| 项 | 值 |
|---|---|
| 起点 | `node scripts/check-process-traces.mjs --root .` → **732**（6336 文件 / 167404 注释行），exit 1 |
| 终点 | 同上 → **662**（6336 文件 / 167421 注释行），exit 1 |
| 少掉 | **70 行**；`date` 一族 638 → 567 |
| 判据 | `scripts/check-process-traces.mjs` 一个字节没动（`git status` 里没有它） |
| 正对照 | `node --test scripts/check-process-traces.test.mjs` **8/8**；五条新 `pin` 在"删掉 date 判据"的副本上全部变绿 —— 它们是被那条判据拒绝的，不是被别的谁 |

注释行数 +17 是 `scripts/check-process-traces.test.mjs` 里新写的说明与 `pin`，不是被读的散文变多。

## 二、派工给的前提没能复现（先写这一节）

派工说这 638 行是"正文里合法的日期（例如「2026-11-25 决定…」），不是进程痕迹"。本树上逐条查过，
**这个形状不存在**：

- 638 行里，`20\d\d-\d\d-\d\d` 后面紧跟"决定/裁定/确定/定"的：**0 行**；
- 按文件分布均匀取 30 行逐行判（下一节）：**0 行**是"日期本身就是内容"；
- `src/` 全树只有 1 行日期，且是页路径（`facts.cpp:1001`）。

原因不是这棵树特别脏，是检查器**只读注释**：一个日期要是数据，它就写在字符串或程序文本里，
而检查器本来就不读那些（`scripts/check-process-traces.test.mjs` 的 clean 例里
`program = "let d = 2026-11-15"` 钉的就是这条）。进了注释，日期只剩一种身份 —— **"何时"**。
所以"正文里合法的日期"这句话在注释里没有对应物，638 行是 638 条"何时"的记录。

这不是"改不动就说不改"。下面每个形状都给了可判的条件和反例；判据能落的地方（71 行）已经落了，
落不下去的地方写明了是哪一截读不通。

## 三、30 行抽样与逐行判断

取法：638 行按文件分组（306 个文件），按各文件命中数轮转取，同一文件内按行号推进，取满 30 行。
即"命中多的文件多占几格、每个文件至多一格一轮"，不是按行号从头抓。

形状代号见第四节；"已删"表示这一行按第四节的判据改了。

| # | 位置 | 摘句（原样） | 行在说什么 | 形状 | 判定 |
|---:|---|---|---|---|---|
| 1 | `test/plans/feat/compile/transforms.plan.toml:120` | `This case was marked known_fail until 2026-11-04` | 这个用例的状态哪天结束 | 状态边界 | 痕迹 |
| 2 | `internal/tools/camel-remote.ps1:405` | `…deleted their seven rows on 2026-11-17` | 工具有一天删了七行 | 流程事故 | 痕迹 |
| 3 | `test/plans/feat/modules/onnx.plan.toml:258` | `…beside a Transpose (rule of 2026-10-26)` | 日期占着规则内容的位置 | 有日期的决定 | 痕迹 |
| 4 | `test/plans/feat/mm/gc_collections.plan.toml:26` | `were measured at three run lengths on 2026-10-24` | 读数哪天量的 | 读数 | 痕迹（句中，未删） |
| 5 | `test/plans/feat/modules/autodiff.plan.toml:2597` | `(2026-10-23, load probe beside it: 67-98)` | 读数现场 | 读数 | 痕迹（已删） |
| 6 | `test/plans/feat/showcase_s2.plan.toml:7` | `` (`internal/archive/batches-2026-10-31-72h.cn.md`) `` | 日期是页名的一段 | 页名 | 痕迹 |
| 7 | `test/plans/feat/opt/effects_kept.plan.toml:279` | `(2026-11-01,)` | 括号里只有日期 | 括号里只有日期 | 痕迹（已删） |
| 8 | `test/tools/run-tests.mjs:316` | `read from … until 2026-11-25, which made every condition…` | 这条判据哪天改成现在这样 | 状态边界 | 痕迹 |
| 9 | `test/plans/feat/compile/rulesets.plan.toml:249` | `(measured 2026-11-14 on this branch with …)` | 读数哪天量的 | 读数 | 痕迹（已删） |
| 10 | `test/plans/feat/opt/commit_refusal.plan.toml:6` | `as the author's rework of 2026-11-18 has them` | 谁哪天改的 | 有日期的决定 | 痕迹 |
| 11 | `test/tools/startup-cost.mjs:50` | `author chose on 2026-10-23 (…)` | 谁哪天选的 | 有日期的决定 | 痕迹 |
| 12 | `scripts/law-cotangent-ports.mjs:27` | `Author, 2026-11-04 04:20 (…)` | 谁哪天几点说的 | 有日期的决定 | 痕迹 |
| 13 | `scripts/plan-inventory.mjs:56` | `` `para_speedup` from the 2026-10-23 rule `` | 哪天的规则 | 有日期的决定 | 痕迹 |
| 14 | `test/plans/feat/docs_links.plan.toml:186` | ``on the release gate of 2026-11-05 (`42078846656`, linux-x64) the case used 34410 ms`` | 读数哪天量的、哪次跑 | 读数 | 痕迹 |
| 15 | `test/plans/feat/linear/var_read_before_write.plan.toml:53` | `the 2026-11-16 ruling retires both remaining var branches` | 哪天的裁定 | 有日期的决定 | 痕迹 |
| 16 | `test/plans/feat/opt/e9.plan.toml:332` | `` `internal/reviews/questions-2026-10-29.md` `` | 日期是页名的一段 | 页名 | 痕迹 |
| 17 | `test/plans/feat/opt/progen.plan.toml:24` | `(2026-10-24, the first window in which every cell ran)` | 读数现场 | 读数 | 痕迹（已删） |
| 18 | `test/plans/feat/transforms/split.plan.toml:22` | `the author's example of 2026-10-31 15:25` | 谁哪天给的例子 | 有日期的决定 | 痕迹 |
| 19 | `internal/tools/host-tokens.sh:8` | `five waits of thirty minutes on 2026-11-24` | 哪天排队排了五次 | 流程事故 | 痕迹 |
| 20 | `test/plans/feat/opt/progen8_arm.plan.toml:31` | `were the plan's open debt until 2026-11-05` | 计划欠账哪天结清 | 状态边界 | 痕迹 |
| 21 | `test/plans/perf/showcase_c2.plan.toml:50` | `7.17 s of CPU / 11.73 s wall (2026-10-24, the eleven slow plans…)` | 读数现场 | 读数 | 痕迹（已删） |
| 22 | `test/plans/perf/startup.plan.toml:20` | `Measured on the Linux host (AMD EPYC 7343, 32 threads) on 2026-10-23` | 读数哪天量的 | 读数 | 痕迹（句中，未删） |
| 23 | `test/tools/selftest/run.mjs:65` | `on 2026-11-03 the sweep fixture's 13 cases came back all FAIL` | 哪天出了这件事 | 流程事故 | 痕迹 |
| 24 | `scripts/check-doc-links.mjs:15` | `author's rule of 2026-11-03: a question is asked with…` | 哪天的规则 | 有日期的决定 | 痕迹 |
| 25 | `test/plans/feat/compile/var.plan.toml:3` | `The author's acceptance for the var-to-SSA work (queue item 1, 2026-11-15)` | 谁哪天验收的 | 有日期的决定 | 痕迹 |
| 26 | `test/plans/feat/consistency/new_values.plan.toml:329` | `The author's answer of 2026-11-18` | 谁哪天答的 | 有日期的决定 | 痕迹 |
| 27 | `test/plans/feat/effects/masking_transition.plan.toml:10` | `the author's review of 2026-11-14 (origin/fsh-dev f3be47bf3)` | 谁哪天审的 | 有日期的决定 | 痕迹 |
| 28 | `test/plans/feat/modules/nn.plan.toml:1246` | `The page is internal/reviews/2026-11-06-who-roots-a-program…` | 日期是页名的一段 | 页名 | 痕迹 |
| 29 | `test/plans/feat/modules/selected_protection.plan.toml:397` | `Refused since 2026-11-16 (design principles 3.5, 198: …)` | 哪条路哪天起拒 | 状态边界 | 痕迹 |
| 30 | `test/plans/feat/modules/stdlib_registry.plan.toml:321` | `at 180000 ms, 2026-11-04 04:39 +08:00, 1m load 16.5` | 读数现场 | 读数 | 痕迹 |

合计：**30 / 30 是"何时"的记录**，其中读数 7、有日期的决定 10、状态边界 4、页名 3、流程事故 3、
括号里只有日期 1、句中读数 2。没有一行是"日期本身是内容"。

## 四、归纳出的四个形状

判据只用一条：**把日期抽走，这个从句还成句吗？**

- **成句 ⇒ 日期只是"何时"的挂件，可以删**（形状 A、B、C）；
- **不成句 ⇒ 日期当主语/宾语/页名，抽走要重写整句，不是删**（形状 D）。

| 形 | 代码条件 | 正例（删） | 反例（不删） |
|---|---|---|---|
| **A** | `[ \t]*\(\s*<日期>\s*,?\s*\)` | `…GATE inlined in front of it (2026-11-01,).` → `…in front of it.` | `…the operand the parentheses carry (2026-11-05).` 前面那句"the parentheses"指的就是它，删掉可以（本行删了），但 `(the day two …)` 这种同位语不行 —— 见 G |
| **B** | `\(\s*<日期>\s*,\s*` | `(2026-10-24, the eleven slow plans of this sweep, 186 tests)` → `(the eleven slow plans of this sweep, 186 tests)` | `(2026-11-02, the day two …)` —— "the day" 指的就是被删的那个日期，删了句子悬空 |
| **C** | `\b(measured\|Measured\|read\|Read)\s+<日期>\s+(?=on\|before\|in\|at\|during\|under\|beside\b)` | `Measured 2026-11-04 on a loaded host (1 m load 20.8-21.0)` → `Measured on a loaded host (…)` | `measured 2026-11-25, two runs` —— 删掉日期只剩 `measured, two runs`，同位语没有落点 |
| **D** | 其余：`until\|since\|as of\|before\|after` + 日期；`<rule\|review\|ruling\|answer\|decision\|rework\|verdict> of <日期>`；`author('s)? <日期>`；`on <日期>` + 一件事；`<日期>` 是一段页名 | —— | `was marked known_fail until 2026-11-04`（抽走 `until …`，剩下 `was marked known_fail` 仍讲历史，claim 没留下）；`(rule of 2026-10-26)`（抽走只剩 `(rule)`，规则内容本来就是日期替它站的位置）；`internal/reviews/2026-11-02-…md`（日期是名字的一段） |

A/B/C 三个条件都是"日期在括号里、或紧跟测量动词"的位置条件，不是"看起来像日期就放过"：
它们要求日期**在括号里当第一项**或**紧跟一个测量动词且后面还有介词短语**。D 不是一条规则，
是一个"剩下什么"的清单 —— 这也是本车道只删了 71 行的原因。

## 五、按判据删掉的 71 行

| 形 | 行 | 例 |
|---|---:|---|
| A 括号里只有日期 | 4 | `An effect whose place in the chain was the GATE inlined in front of it (2026-11-01,).` → `…in front of it.` |
| B 括号里第一项 | 43 | `943.31 s of CPU / 127.5 s wall (2026-10-23, load probe beside it: 67-98)` → `…wall (load probe beside it: 67-98)` |
| C `measured <日期> on …` | 25 | `Measured 2026-11-04 on a loaded host (1 m load 20.8-21.0)` → `Measured on a loaded host (…)` |

删的都是注释行与 `desc` 值里的日期，没有一行动代码。**数字、单位、机器、负载、窗口全部留着** ——
去掉的只有"哪天"。

三处伤，按下述方式避开或修掉：

- `(2026-11-02, the day two …)`（`test/tools/runner-output.mjs:183`）：B 的机械改写会删掉
  `the day` 指的那一天，句子悬空。**回退了这一行**，留给重写；
- `Measured 2026-11-04 on` 在行尾断行的那处（`test/cases/compile/transforms/extent_of_answers_a_parameter_type.cml:5`）：
  改写后行尾多一个空格，已去掉；
- `read 2026-11-21 on …`（`test/cases/transforms/compositions/nested_grad_min.cml:21`）：那行注释自己也说
  "it is recorded with its date instead of being relied on"，删日期会让注释自我矛盾。**跳过**，留给重写。

接缝扫描扫的是 `(:`、`,,`、`()`、` of .`、`on:`、`  `（新造的双空格）、`[.,;:)]` 前的空格，
对比改写前后每行的数量，**新增 0 处**；逐行比词也确认没有丢词（71 行逐对比较，0 处丢词）。

## 六、判据落地：自测

`scripts/check-process-traces.test.mjs` 加了七条，全部是上面抽样里出现过的形状：

| 条 | 内容 | 期望 |
|---|---|---|
| `ended.plan.toml` | `# This case was marked known_fail until 2026-11-04, against the rule.` | 红（状态边界） |
| `loaded_host.cml` | `// Measured 2026-11-04 on a loaded host (1 m load 20.8-21.0): value= 3.0.` | 红（读数） |
| `dated_rule.cml` | `…(rule of 2026-10-26).` | 红（有日期的规则） |
| `row-drop.test.ps1` | `…deleted their rows on 2026-11-17.` | 红（流程事故） |
| `page.plan.toml` | `desc = "…the page \`2026-11-15-can-a-commit-depend-on-unrelated-writes\`."` | 红（页名一段） |
| `rewritten.plan.toml` | 上面第一条删掉日期后的样子 | 绿 |
| `camel-cleanup.measured.sh` | `# 48 compile jobs on a 32-core host (load 88).` / `# Measured on a loaded host (…): 47750 ms of CPU.` | 绿 |

反例不是"看起来干净的句子"，是**这两条案例被删掉日期之后的样子** —— 一个只种痕迹的测试
说不清干净的一侧长什么样。

**红先**：把 `check-process-traces.mjs` 里 `'\b20(25|26)-[01]\d-[0-3]\d\b'` 这一条替代删掉，
`node --test` 立刻 7/8（既有那条 `# Measured on 2026-11-15, and it held.` 先红）；再逐条把五条新案
单独种到临时树里跑，"有判据 exit 1 / 没有判据 exit 0" 五条全中。判据恢复后 8/8。

## 七、没做的，与被拒的做法

- **没有改判据去凑数。** 能把这 638 行大幅降下来的唯一"规则"是"测量日期不算痕迹"，而这条
  `scripts/check-process-traces.test.mjs` 里已经有两行钉死了：
  `# Measured on 2026-11-15, and it held.` 与 `// Green today, measured 2026-11-05: exit 0.`。
  改规则等于把这两条断言改红 —— 那是改判据，不是修缺陷。日期一族在 `e3095acc11`（族 2）已经
  按"删掉读数是什么时候量的"删过 273 行，在 `bd2d84365d`（族 3）删过 285 行"谁决定的"；本车道
  是同一条判据的延续，不是对它的重新解释。
- **没有 allowlist。** 新增的七条是测试里的形状，不是检查器里的例外表；检查器里一个文件、一个
  目录、一个字面量都没豁免。
- **被拒的机械改写：** `, <日期>` 紧跟冒号的那种（`on 2026-11-24: …` → `on: …`、
  `until 2026-11-18: …` → `until: …`），167 行里绝大多数读成残句，整条退回；
  把 `(日期)` 连同前面空格一起删但不看后一个字符，会留下 ` .` 与 ` ,`，改为看后一个字符。

## 八、还剩什么

页面写就时 662 行，日期一族 567 行（写就时另 88 行同时是页路径）。这一族**分批推进，数字随批
更新**：每批一行，"总行 / 日期一族 / 读数一族"三列都是那一刻 `node scripts/check-process-traces.mjs
--root .` 的读数与同一遍逐行判的家族计数。谁再接手，先看最后一行，不看这张表是什么时候写的。

| 批 | 提交 | 总行 | 日期一族 | 读数一族 | 这一批做了什么 |
|---:|---|---:|---:|---:|---|
| 0 | `7ddaa9dfa6` | 662 | 567 | 135 | 立这条判据，按它删 71 行 |
| 1 | `14f3920e1a` | 632 | 537 | 105 | 读数 30 行：把"哪天"换成哪台、哪个负载、哪个窗口、哪棵树 |
| 2 | `11a98019de` | 602 | 507 | 75 | 读数再 30 行：`test/tools/` 20 行、`test/cases/compile/transforms/` 7 行、相邻 3 行 |
| 3 | `596c92d868` | 574 | 477 | 75 | 状态边界 30 行：把"哪天结束的"换成"被什么界定"（第九节） |
| 4 | `7bd3eefdad` | 548 | 451 | 50 | 读数 25 行 + 修 1 行 `bd2d84365d` 的残句（第十节） |

所以这一节原来的 135 与 567 是这样走的：135/567 → **105/537**（批 1）→ 75/507（批 2）→ 75/477（批 3）
→ 50/451（批 4）。

剩下的 451 行按形状分（读数一族已从起点的 135 行降到 50 行，口径与上表同）：

| 形 | 行 | 要做的动作 |
|---|---:|---|
| 读数（`measured …`、括号里的现场） | 50 | 重写从句：把"哪天"换成"哪台、哪个负载、哪个窗口"，或整句去掉读数的现场（批 4 抽掉 25 行，见第十节） |
| 页名的一段 | 76 | 与 `handover-path` 一族同源：把页名换成"the page"，或删掉引用 |
| 状态边界（`until/since/as of …`） | 52 | 重写句子：讲现在是什么，不讲哪天变的；判法见第九节（其中 5 行不是边界，剩 47 行） |
| 有日期的决定（`rule/review/answer of <日期>`、`author`） | 47 | 与族 3 同一动作：留规则，去掉日期与"谁"；批 4 只在格里落了一行，且那一行是修 `bd2d84365d` 的残句 |
| 流程事故（`on <日期> … deleted/merged/hit`） | 30 | 重写：只留"为什么这道闸存在"，不留事故的时间地点 |
| 其余（日期在句中当唯一的"何时"） | 196 | 逐行重写 |

这 451 行**不是规则能降的**：判据已经把它们都判对了，它们是 451 句要重写的话。谁要做，按上表
一行一族地做，做完一批跑一次 `node --test scripts/check-process-traces.test.mjs` —— 那七条里
红的那五条钉着"日期还得被拒"的底线。

## 九、状态边界这一族：30 行抽样、一条判据、只改 a/b/c

这一族碰不得"读数"那三招，原因是位置不同：`until/since/as of <日期>` 的日期**就是那个从句的
主语或宾语**。`Until 2026-11-04 the lift refused a body that captured anything at all` 抽走日期
得到 `Until the lift refused a body` —— 剩下的是个没有起点的从句，读者不知道"until 什么"。
所以判据不是"这一行像不像日期"，是**先问这个状态是被什么界定的**，再决定日期换成什么。

取法：本族 82 行按文件轮转，从 `test/tools/check-no-algo.mjs:33` 与
`test/tools/opt-cost/measure.mjs:162` 起，命中多的文件先占（`transforms.plan.toml` 14 行、
`effects_kept.plan.toml` 8 行、`showcase_s2.plan.toml` 5 行……），同一文件内按行号推进，取满
30 行。派工里点名的第三个起点 `test/tools/check-vjp-parity.mjs:15` 是 `before 2026-11-14`，是
`until` 的镜像形状；按下面这条判据它不是本族成员（本族只收 `until/till/since/as of`），
**它不是本批的一行，也没有被改**，下一批若要把 `before/after` 并入这一族，它是那一批的第一行。

| 类 | 界定的东西 | 行 | 改法 | 例（改前 → 改后） |
|---|---|---:|---|---|
| **a** | 一条裁定 | 5 | 留裁定、去日期；裁定本来就在同一从句里被引用（`design principles 3.5 (198)`、`9.5`、引号里的原话） | `Refused since 2026-11-16: the closure is bound to a name…` → `Refused: the closure is bound to a name…`（下一行就是 3.5 (198)） |
| **b** | 一个提交 | 3 | 换成那个提交在树里的**名字**，不写 sha | `since the 2026-11-22 move` → `after the move into src/stdlib`；`until 2026-11-22` → `until the move into src/`（`10c9d41558`；`under cxx::` 是 `3750540914`） |
| **c** | 代码现状 | 22 | 日期换成"那句代码事实"：上一句已经说了现在的代码是什么，日期是它的期限 | `Until 2026-11-04 it passed the operand instead` → `It passed the operand instead`（上一句已说它现在从 `sa` 起）；`The first list read … until 2026-11-18: that was the creation order` → `The first list read … -- the creation order`；`1. until 2026-11-04 it pinned a crash` → `1. it first pinned a crash`（编号表里 2 是 `then`、3 是 `now`） |
| **d** | 只有"从某天起没人动过" | 0 | —— | 见下 |

**d 是 0 行，不是没找。** 30 行逐行看下来，每一行的段落里都另有一个坐标：一句裁定、一个提交、
或者"现在的代码是什么"——这正是机械删日期会把句子弄坏、而重写总能落地的同一个原因（日期不是
唯一的信息，它只是那句话里最省事的写法）。所以本批没有"列出来不改"的行；真出现只有日期一个
坐标的句子（例如"从某天起没人动过，我也说不出为什么"），它才该进 d，且必须写明为什么没有第二个
坐标可用。

**82 里有 5 行不是状态边界**（判据只认"边界词与日期在同一从句里"，这 5 行的边界词与日期互不相干）：

| 位置 | 是什么 | 归哪一族 |
|---|---|---|
| `test/plans/feat/compile/transforms.plan.toml:1474` | `since no signature of mk is read` 是因果连词；日期在页名 `internal/reports/2026-11-04-what-the-transpose-batch-measured.cn.md` 里 | 页名 |
| `test/plans/feat/compile/transforms.plan.toml:2546` | `since exp(y) was a recomputation` 是因果连词 | 其余 |
| `test/plans/feat/modules/nn.plan.toml:1319` | 日期在页名 `internal/reviews/2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md` 里；`until` 挂在 `segfaulted until a C++ holder rooted its program` 上，没有日期 | 页名 |
| `test/plans/feat/opt/splice_place.plan.toml:168` | 日期在页名 `internal/reviews/2026-11-19-the-0.2.45-blocker-list-by-severity.cn.md` 里 | 页名 |
| `test/plans/probe/refleak.plan.toml:148` | `author review 2026-11-17, item 10` 是有日期的决定；`until` 挂在 `until the run's own exit disposes it` 上，没有日期 | 有日期的决定 |

反方向也有一行：`test/plans/perf/startup.plan.toml:115` 的 `rounded up to the next 500 ms` 里的
`up to` 让"边界词+日期"多算了一行，它是读数。所以**字面 82 行、判定 77 行、批 30 行、剩 47 行**；
批 3 之后字面读数是 52，其中 5 行是上表那 5 行，剩 47 行要重写。

### 读数与自测

| 项 | 批 3 之前 | 批 3 之后 |
|---|---|---|
| `node scripts/check-process-traces.mjs --root .` | **604**（6336 文件 / 167452 注释行），exit 1 | **574**（6336 文件 / 167452 注释行），exit 1 |
| `date` 一族 | 507 | 477 |
| 状态边界（字面 / 判定） | 82 / 77 | 52 / 48（47 行 + `startup.plan.toml:115` 那 1 行读数） |
| `node --test scripts/check-process-traces.test.mjs` | 8/8 | 8/8 |

文件数与注释行数两侧相同，说明这一批只动了词、没有动结构。接缝十模式扫全树：九个计数完全相同，
第十个（`until|since|as of` 紧跟标点）**少一处**，位置正是本批改过的
`test/tools/opt-cost/measure.mjs:162`（原文 `std::cxx::fold since).` 的省略式 `since`），
**新增 0 处** —— 这也正好是这把接缝尺的阳性对照：它只在我动过的行上动了。改动 34 行逐行比词，
丢掉的词只有日期本身（`until`/`since`/`2026`/`11`/`04`）与三处被重写的连接词
（`it was … and`、`that was`、`before`），没有丢任何一句的主张。

`node scripts/check-process-traces.mjs` 一个字节没动；9 个改过的计划仍能解析（430 条用例）。

### 没写进注释的东西

**没有把日期换成 sha。** b 类的三个提交在树里都有名字（`the move into src/`、`after the move`、
`under cxx::`），名字是读者能在代码里查的东西；sha 答的是"这条读数是哪棵树上的"，不是"这个状态
被什么界定"，而且检查器本来就拒 `commit <sha>` 与反引号里的 sha，裸写只是把同一个习惯换个安静
的字号。两个 id 在批 3 的提交正文与本页。

### 剩下的

- 本族还剩 47 行（外加 5 行不属于本族），判法照第四节与本节；
- `test/plans/feat/opt/splice_place.plan.toml:48` 改掉的那句话，在 `:57` 与 `:66` 还有两份一模一样的
  （`The context argument is a Ref handle since …`）；抽样只抽到一份，另两份**按同一句改法**即可，
  本批没动它们，是为了让"30 行"这个数可核。

## 十、读数这一族的第四批：25 行，加一行修复

判据一个字没动，还是第四节那条：日期只回答"这个数是哪次观测到的"，撤掉或换掉它，主张（数、机、
负载、结论）完整无缺。三招照旧：机/负载当锚、窗口当锚（钟点换成 first/second/third）、读数现场
整句去掉。

取法与批 0-2 相同，只有一条加严：**落在系列里的行整组取**。`onnx.plan.toml` 的三条读数
（`:932-934`）、`stdlib_registry.plan.toml` 的两条（`:321`、`:325`）、`startup.plan.toml` 的三条
（`:20`、`:32`、`:115`）都是一次运行的一组；抽两条留一条，文件就会在一处引一次运行、在另一处引
三次。第九节记过这个毛病（`splice_place.plan.toml:48/57/66`），这里不再造一个。

| # | 位置 | 改前 → 改后 | 锚是什么 |
|---:|---|---|---|
| 1 | `test/plans/feat/modules/onnx.plan.toml:932` | `40.77 s of CPU (2026-10-24 02:39, the slow-layer run of the batch point)` → `(the slow-layer run of the batch point)` | 那次运行的身份 |
| 2 | 同上 `:933` | `(2026-10-24 05:22, 136 tests, this entry red on the margin only)` → `(136 tests, this entry red on the margin only)` | 用例数 |
| 3 | 同上 `:934` | `(2026-10-24 05:30, the same plan, 136 tests, green under this budget)` → `(the same plan, 136 tests, …)` | 同上，第三行 |
| 4 | `test/plans/feat/modules/stdlib_registry.plan.toml:321` | `at 180000 ms, 2026-11-04 04:39 +08:00, 1m load 16.5:` → `at 180000 ms, 1m load 16.5:` | 界与该时点的负载 |
| 5 | 同上 `:325` | `at 240000 ms, 2026-11-04 05:19 +08:00, 1m load 30.3:` → `at 240000 ms, 1m load 30.3:` | 同上 |
| 6 | `test/plans/perf/startup.plan.toml:20` | `Measured on the Linux host (AMD EPYC 7343, 32 threads) on 2026-10-23 --` → `… (AMD EPYC 7343, 32 threads) --` | 机器、四个窗口的负载 |
| 7 | 同上 `:32` | `at load 106-130 on 2026-10-23 -- the reading the rule answered` → `at load 106-130 -- …` | 负载 |
| 8 | 同上 `:115` | `load average 25.73/43.89/44.94 when it was taken, 2026-10-31), rounded up` → `… when it was taken), rounded up` | 机器、负载 |
| 9 | `test/plans/feat/showcase_s2.plan.toml:79` | `(PyTorch 2.5.1+cu121, float64, cpu, 2026-11-03, D:\Temp\…)` → `(…, cpu, D:\Temp\…)` | 工具版本、精度、设备 |
| 10 | 同上 `:111` | `PyTorch's reading of the same literals (float64, cpu, 2026-11-03).` → `(float64, cpu).` | 精度、设备 |
| 11 | `test/plans/perf/para_speedup.plan.toml:22` | `Measured on the reference host (AMD EPYC 7343, 32 threads) on 2026-10-23, both sides in one run` → `… (AMD EPYC 7343, 32 threads), both sides in one run` | 机器、同一次运行 |
| 12 | 同上 `:102` | `30 ms of CPU measured on 2026-10-24 -- so it keeps the wall bound.` → `30 ms of CPU measured -- …` | 读数自己 |
| 13 | `test/plans/feat/compile/transforms.plan.toml:946` | `the run of 2026-11-02 18:45 printed exactly the expected stderr lines` → `a run printed exactly the expected stderr lines` | 那一次运行 |
| 14 | 同上 `:1240` | `(measured 2026-11-04, the reading the page on a closure member's cotangent reported)` → `(the reading the page … reported)` | 页 |
| 15 | `test/plans/feat/opt/progen.plan.toml:30` | `the slower 2026-10-24 reading set (150000 / 86560 = 1.73x)` → `the slower reading set (150000 / 86560 = 1.73x)` | 比值与它的两个数 |
| 16 | 同上 `:32` | `The 2026-10-27 measurement leaves that budget at` → `The 83670 ms measurement leaves that budget at` | 读数自己的数 |
| 17 | `test/plans/perf/showcase_c2.plan.toml:188` | `Measured 2026-11-15: on the gate host this entry takes 2.07 s` → `On the gate host this entry takes 2.07 s` | 机器 |
| 18 | `test/plans/feat/tools/selfchecks.plan.toml:175` | `Fast: 0.6 s on the workstation, 2026-11-25.` → `Fast: 0.6 s on the workstation.` | 机器 |
| 19 | `test/plans/feat/tools/plan_refs_sweep.plan.toml:32` | `on 2026-11-22 the linux-x64-nojit leg read 316220 and 340540 ms of CPU (runs 45003684718 and …)` → `the … leg read … (runs …)` | 那条腿与两个 run id |
| 20 | `test/plans/feat/tutorial_examples.plan.toml:286` | `([2, 2, 2, 36, 58, 88, 36, 8], seen on 2026-10-26 in a run with -Jobs 8)` → `(…, seen in a run with -Jobs 8)` | 那个参数 |
| 21 | `test/cases/transforms/split/split_capture_of_a_tensor.cml:33` | `Measured 2026-11-04, \`withport\`.` → `Measured on this tree, \`withport\`.` | 这棵树 |
| 22 | `test/cases/transforms/compositions/nested_grad_min.cml:21` | `so it is recorded with its date instead of being relied on -- read 2026-11-21 on the tree it was read on, std::nvm:` → `so it is not relied on -- read on this tree, std::nvm:` | 这棵树 + 后端 |
| 23 | `test/cases/transforms/compositions/nested_grad_through_a_binding.cml:13` | `Read 2026-11-21,` → `Read on this tree,` | 这棵树 |
| 24 | `test/cases/transforms/nested/nest_quartic.cml:11` | `Read 2026-11-20 09:20:` → `Read on this tree:` | 这棵树 |
| 25 | `test/cases/runtime/adopt_chain/split_three_calls.cml:16` | `yet: measured 2026-11-04 they are 128.0 and 648.0` → `yet: they are 128.0 and 648.0` | 数自己 |

第 22 行是第四节里被跳过的那行：批 0 跳它，是因为单删日期会留下 "recorded with its date" 自相
矛盾；**改整句**（不是从这句里删日期）就没有这个矛盾，所以这批收了它。

### 派工点名的 `test/tools/`：0 行

`test/tools/` 下 17 行带日期的行逐条看过，**没有一行是读数**：它们是页名（`run-tests.mjs:436`
等）、状态边界（`run-tests.mjs:316` 的 `until`）、有日期的决定（`startup-cost.mjs:50`、
`import-audit.mjs:6`、`para-speedup.mjs:58`）与流程事故（`selftest/run.mjs:65`、
`run-tests.mjs:1734/1781`、`runner-output.mjs:183`）。`check-vjp-parity.mjs:15` 的
`before 2026-11-14` 是 `until` 的镜像形状，第九节已点名它归下一族。

唯一一行**长得像读数**的是 `test/tools/check-vjp-ruleset.mjs:21-22`，而它不是读数，是伤：

```
//               cotangent tuple the pullback answers (one member per parameter, author's ruling of
//               2026-11-01 14:30), the cotangent read out of it, and the gradient. The point is
```

`bd2d84365d`（谁决定的族）把这两行的并置文本写回第 21 行，却把第 22 行原样留下，于是同一句出现
两遍、底下挂着一个日期碎片。修法就是那一批本来该写的：括号里的裁定日期去掉，句子只留一遍。它是
这 26 行里唯一不属于本族的一行，记在这里，因为它是派工让这批去看的那一行。

### 读数与自测

| 项 | 批 4 之前 | 批 4 之后 |
|---|---|---|
| `node scripts/check-process-traces.mjs --root .` | **574**（6336 文件 / 167452 注释行），exit 1 | **548**（6336 文件 / 167451 注释行），exit 1 |
| `date` 一族 | 477 | 451 |
| 读数一族 | 75 | **50** |
| `node --test scripts/check-process-traces.test.mjs` | 8/8（HEAD 两份文件的临时副本上跑） | 8/8 |

注释行少 1 是 `nested_grad_min.cml` 两行并成一行；文件数两侧都是 6336，说明只动了词。把两侧的
`--list` 逐行比：消失的 26 个命中正是这批改的 26 行，**新增 0**。

接缝十二模式（`(:`、`,,`、`()`、`.` `;` `:` `)` 前的空格各一条、句号后的双空格、行尾空白、
`until|since|as of` 紧跟标点、`word (`）扫 `src/ scripts/ internal/tools/ test/`：**十二个计数完全
相同**。行集合只有一处动：`nested_grad_min.cml` 少了那一行，三个命中（两个 `()` 一个 `word (`）
各上移一行 —— 同一次出现，换了位置，这也是"尺子只在我动过的地方动"的阳性对照。

逐行比词（26 对）：带数字且不是日期/钟点的 token **0 丢** —— `128.0`、`648.0`、`40.77`、`45.60`、
`25.69`、`46.09`、`30.92`、`136`、`47750`、`26.67`、`20.35`、`47.02`、`235%`、`180000`、`240000`、
`16.22`、`25.73/43.89/44.94`、`1.08x`、`316220`、`340540`、`45003684718`、`45007258569`、`2.07`、
`0.6`、`92.9`、`75.2`、`72.6`、`68.7`、`106-130`、`13.0`、`1.73x`、`1.79x`、`83670`、`86560` 全在。
单位（`s`、`ms`、`%`、`cores`、`threads`）、机器（`AMD EPYC 7343`、`tangshan88`、
`linux-x64-nojit`、`the gate host`、`the workstation`、`PyTorch 2.5.1+cu121`、`std::nvm`）与负载
0 丢。丢掉的词只有日期、钟点，与引导它们的连接词（`on` x8、`of` x2、`the` x6、`measured`/
`Measured` x5），外加两处按判据重写的从句与 `check-vjp-ruleset.mjs` 那段重复的碎片。

11 个改过的计划仍能解析（484 条用例），`.cml`/`.mjs` 的改动逐行检查**全部落在注释行**上，没有一行
动代码。

### 本批没动的，与下一手要接的

- `test/plans/feat/mm/gc_collections.plan.toml` 的 13 行（`:66` + 十二条同样的 arm 句）：抽两行会
  留下十条同形孪生句，全取又是半个批。留给整组取的一批。
- `reshape_adjoint_shape.cml:12-13` 的 `before/after` 与 `inline_two_reads.cml:14` 的
  `before 2026-11-19` 没有并入：它们属 `before/after` 一族，日期界定的是代码状态，`before :` 不
  成句。
- **5 个 `.cml` 的 AST hash 变了。** `test/plans/feat/parse/golden/ast_hashes.txt` 记着这五个文件
  （`split_capture_of_a_tensor.cml`、`nested_grad_min.cml`、`nested_grad_through_a_binding.cml`、
  `nest_quartic.cml`、`split_three_calls.cml`），要在能构建的地方重录：
  `python3 test/tools/parsecheck/check.py snapshot --update --only '<pattern>'`。本车道不构建，
  所以这一笔留给 88 主机；批 1 与批 2 已经用 17 个 `.cml` 留下同样的债，两批都没有重录，点名它们
  的闸是 `feat/parse/frontend.plan.toml` 的 `frontend.ast_record`。这不是本批的新问题，但也不能
  当成没有。