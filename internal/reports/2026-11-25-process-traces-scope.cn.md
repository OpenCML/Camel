# 过程痕迹的范围：1538 条按族切开，删 432 条、判据放过 15 条

- 日期：2026-11-25｜车道 `lane/trace-scope`，树 `D:\Projects\Camel-wt\trace-scope`，基线 `origin/integration` = `c99d7e74f`｜对象：闸门 4，`scripts/check-process-traces.mjs`（`internal/tools/camel-remote.ps1` 的 `process-traces` 目标）

## 1. 读数（同一条命令，改前一个数、改后另一个数）

```
node scripts/gates.mjs --only 4
改前  1985 条  exit 1     6332 文件 / 166960 注释行
改后  1538 条  exit 1     6332 文件 / 166766 注释行
```

1985 → 1538，差 447，两条路，比例约 29 : 1：

**一、删掉 432 条。**三笔，各自只删「记录」，句子照旧：

| 笔 | 条数 | 是什么 | 例子（前 → 后） |
|---|---|---|---|
| 两行指针块 | 416 | 63 个计划里同一句重复 208 次（208 × 2 行）：「child wall clock in the 2026-11-12 record (appendix B of / internal/reports/2026-11-24-test-suite-inventory.cn.md).」 | `…-- 2.46 s of / child wall clock in the …(appendix B of / internal/reports/….md).` → `…-- 2.46 s of / child wall clock.` |
| `src/` 全部 | 15 | 6 条派工编号、5 条日期、`this lane`、`lane/std-nop`、`the ruling's`、一个提交哈希 | `(D-1124-65: one predicate…)` → `: one predicate…`；`Measured on \`lane/std-nop\` with \`opt::cse\`` → `Measured with \`opt::cse\`` |
| `camel-remote.ps1` 的那句假话 | 1 | `which is what this lane replaced` | `…with the reader's own memory, which is what this check exists to keep out.` |

**二、判据放过 15 条（只这一族，见 §3 第 9 行与 §5）。**判据是「工作根是工具自己定义的布局」，条件写进了 pattern：只有带具体 home 前缀的写法才算痕迹。这一族 18 → 2：16 条不再算工作根的痕迹（14 条 `~`/`$HOME` 模板路径、1 条不带根的布局描述、1 条 `.../camel-work/<worktree>` 占位路径），其中 15 条整条离开名单 —— `camel-remote.ps1:1146` 仍红，但那一条是它带的日期，与工作根无关。

分树（改后）：`src/` 0、`scripts/` 148、`internal/tools/` 155、`test/plans/` 756、`test/cases/` 378、`test/tools/` 94、`test/` 其余 7。文件数 527。

## 2. 机制：红是扩口径带进来的（复现，不是转述）

同一棵父树 `f58276dbfd`，两条规则各跑一次（`git archive` 到临时目录，两条规则都带 `--root`）：

```
旧规则（只 src/、只 .cpp/.h/.cml/.cmake…）   1402 文件 88465 注释行 → 0 条   exit 0
新规则（四棵树 + .mjs/.js/.ps1/.sh/.toml + desc/name 散文 + 派工编号/车道/待审/this lane）
                                              6216 文件 157955 注释行 → 1417 条 exit 1
```

`68308a89f6` 的 diff 与这两行逐项对得上：`dirs` 由 `['src']` 变四棵树，`exts` 加五种扩展名，`proseKeys` 加 `desc`/`known_fail_owner`/`known_fail_reason`/`note`/`title`（`name` 后来补上），pattern 加 `lane-…`、`[DC]-\d{4}-\d+`、`待审`、`this lane`。⇒ 这 1988 条不是新长出来的，是**四十天里四棵树按旧口径从未被读过**。

## 3. 分族表（11 族，1538 条，逐条都能落到一行）

「判据」一列写的是**为什么这一类允许存在**；写不出来的，就是不允许，改法一并写在列里。

| # | 族 | 条数 | 分布 | 判据（允许 / 不允许的理由） | 例子 | 结论 |
|---|---|---|---|---|---|---|
| 1 | 日期 | 700 | scripts 86、tools 62、cases 158、plans 324、其余 70 | 读数本身可以留，**日期不能**：`measured 2026-10-24, 32 tests` 里的日期是那一天那台机器那个负载的坐标，阈值或机器一变它就不跟着变，读者会把去年的数当今天的前提。 | `// started (measured 2026-11-02, a shell without LD_LIBRARY_PATH)` | 删日期，读数与单位不动 |
| 2 | 裁定归属 | 397 | scripts 13、tools 5、cases 168、plans 197、其余 14 | 约束可以留，**「谁在什么时候说的」不能**：`(the author's ruling of 2026-11-03)` 是指向一页会过期的记录的指针，而句子本身通常已经把那条规则写完；删指针，规则还在。 | `// the author's ruling withdrew both: the port says \`T[...]\`` | 删指针，保留规则句 |
| 3 | 提交哈希 | 146 | scripts 14、tools 5、cases 29、plans 93、其余 5 | 历史在提交信息里；代码里的 `\`05e1145a9\`` 是让读者去 `git show` 猜一段已经写进注释的话。 | ``// moved class with nothing rewritten and reded the old record (`05e1145a9`)`` | 删，或改成「上一次读取时」「另一版」 |
| 4 | 交接页路径 | 101 | scripts 20、tools 10、cases 1、plans 62、tools/* 8 | `internal/` 的页不进树、会被裁；一句话要以一页会消失的文件为前提，它就不是这个树在说的话。 | `(internal/archive/m4-autodiff-migration-table.cn.md) is written by hand` | 删路径，句子独立成句 |
| 5 | 车道代号 | 111 | scripts 5、tools 28、cases 19、plans 59 | 车道是一次性座位（`lane-code` 76 + `this lane` 32 + `lane/…` 3）：对后来的读者，「this lane」指向一个不存在的人；对当时的读者，它是「谁改的」而不是「代码做什么」。 | `\`lane-stale\` (8d0de4e3e) cleaned six such sentences out of \`test/plans/\`` | 改成被改的那件事本身 |
| 6 | 派工编号 | 37 | scripts 4、tools 27、cases 1、plans 5 | 编号是 backlog 里的一行；`(D-1123-83)` 在 backlog 收档之后指向空。 | `-Wc++11-narrowing is the one that cost a day (D-1123-83)` | 删编号，保留「为什么加这条检查」 |
| 7 | 主机 | 8 | scripts 0、tools 3、cases 1、plans 3、tools/* 1 | 机器会换；`88zjwei` 和 `/home/zjwei/camel-work` 记的是作者在哪台机上读的，不是代码的性质。 | `-BashHost 88zjwei`、`(host \`88zjwei\`)` | 写成 `<host>` / 「构建主机」 |
| 8 | 角色词 | 11 | tools 8、cases 1、plans 2 | 分两半：`(负责人 2026-11-15)` 是「谁说的」，不许留；camel-oversight 的 `decision-maker` 是**工具连的另一端**（同一个文件里 `dmLastSeen` 就是它），是接口名不是作者记录 —— 这一半见 §5 第 3 条（本轮没写进 pattern）。 | `// camel-oversight: the collaboration engine between the decision-maker (another machine)` | 前一半删日期；后一半见 §5 第 3 条 |
| 9 | 工作根 | 2 | tools 2 | **该留，已判**：`camel-work` 是工具自己定义的工作根（`$HOME/camel-work/tmp`、`~/camel-work/Camel.git`、`.../camel-work/<worktree>`），代码就是对着这棵树工作的，注释在说布局；记一条主机上的目录是另一回事，写成具体 home：`<home>/<user>/camel-work/tmp/lane-1/src`。条件是**前缀**不是这个词。剩下的 2 条正是后者（清单样例行、路径归一化例子）。 | `#   2026-11-24T02:40:00+08:00	checkout	/home/u/camel-work/tmp/lane-1/src`（仍红） | 条件写进 pattern，正反两个用例已加 |
| 10 | stage / step | 17 | scripts 3、plans 11、tools/* 3 | 绝大多数不是「refactor 走到第几步」，是计划自己的词：规则流水线的一段（`the bump rule is stage 0`）、模型的第二段（`the stage-2 model`）、baseline 脚本的参数（`\`baseline/grow.py --stage 4\``）。**同一个词在两个域里**，正是判据要求写清的那种。 | `desc = "… the bump rule is stage 0, so it rewrites …"` | 见 §5 第 1 条，本轮不改 |
| 11 | 铁律编号 | 8 | scripts 3、tools 4、tools/* 1 | `iron rule 17` 是协作协议里一条**常设**规则的编号（和 `design principles 5.4` 同类），不随日期失效；但编号本身也是一页文档的地址。 | `(iron rule 17)` | 见 §5 第 2 条，本轮不改 |

## 4. 派工里点名的三问，实测答案

**（a）`test/plans` 的 `desc`/`name` 里出现「pass / 车道 / 派工」这类词 —— 真痕迹还是误报？** 误报，**不存在**：探针的 pattern 里没有这三个词（`grep` 一次即可核）。反过来量了一次，两个方向都给读数（临时树，`--root` 指过去）：

```
只有普通词：desc = "A pass that runs twice ..., and the lane the runner puts it on: the dispatch
            says the 派工 number is the case's, and the 车道 is this branch."
   → scanned 1 files, 2 comment lines, no trace   exit 0
同句加一处记录：… the one the author's ruling of 2026-11-03 named (D-1123-17).
   → 1 条，exit 1
```

⇒ `desc`/`name` 的 291 条全是真记录（日期 87、裁定归属 113、提交哈希 42、车道 25、交接页 12、stage 10、派工编号 2），**没有一条来自普通词汇**。散文该读，现在读得准。

**（b）派工编号出现在计划与用例的注释里 —— 是注释还是决策记录？** 是决策记录，**不是**注释：`(C-1123-17)` 不含任何关于代码的信息，它只把读者送到 backlog 的一行。同一个数在 `src/` 的 6 处也一样 —— 见（c）。⇒ 37 条全删，理由不是「看起来没问题」，而是「这条注释删掉之后，句子说的是同一件事，而读者少一个会失效的地址」。

**（c）`src/` 那 14 条 —— 该留？** 实测**否**：15 条我逐条读过（6 派工编号、5 日期、`this lane`、`lane/std-nop`、`the ruling's`、一个哈希），每一条都是可以整句去掉记录而语义不变的，删完后 `src/` 是 0 条、读数为 0。这一格与派工的预期相反，实测压过了预期：没有一条 `src/` 的记录是「引擎的常数非它不可」。若某条将来真属于这一类（比如一条测量值只有带日期才可信），那是 §5 要判的形状，不是这 15 条。

## 5. 判据只放过一族，其余三条候选没写成条件

数字从 1985 掉到 1538：**432 条来自「删掉不该留的」**，**15 条来自「判据承认这一类该留」**，没有一分来自关探针、调阈值或按目录豁免。两条路各自点名如下。

**承认的那一族：工作根（18 → 2）。**条件是一句能被打穿的话：*工作根是工具定义的布局，工具们一律写成 `$HOME/camel-work`、`~/camel-work`、`.../camel-work`；一次运行的主机目录才是记录，它写成 `<home>/<user>/camel-work/...`。*所以 pattern 从 `camel-work` 改成 `(?<=/home/[a-z0-9_-]+/)camel-work` —— **条件在前缀，不在这个词**，按文件名/按目录的豁免一条没有。它随手带进两个方向：

```
先看到红（加用例、pattern 还没改）：
  ✖ a clean tree stays clean with --list
    # The root is $HOME/camel-work/tmp and the bare is ~/camel-work/Camel.git.
    → 被拒（1 listed）
后看到绿（pattern 改完，8/8）：
  cases   里那条 `<home>/<user>/camel-work/tmp/lane-1/src` 仍被拒
  clean   里那条 `$HOME/camel-work/tmp` / `~/camel-work/Camel.git` 通过
```

剩下 2 条仍红，因为它们是「记录」那一半：`camel-cleanup.sh:40` 是清单文件的样例行（连时间戳一起），`:102` 是路径归一化的例子。这一族的下一次修剪就是这两行。

**没承认的三条候选，以及为什么这一轮不写进 pattern：**

1. **stage / step（17 条）**：可以写成「refactor 的 stage 带小数点，流水线的 stage 是整数」，但 `refactor stage 2` 不带小数点就漏了；这条条件现在挡不住反例，不能进 pattern。
2. **铁律编号（8 条）**：和 `design principles 5.4` 同类，但 `iron rule N` 的分节在哪一页、会不会重编号，这一轮没量；量不到的不写成条件。
3. **`decision-maker`（9 条）**：在 `internal/tools/camel-oversight/` 里它是工具连的另一端（同一个文件里 `dmLastSeen` 就是它），是接口名；但同一个词在别处也可以是「谁说的」，要写成条件就得先量出「这个词在此处是接口名」的可判形状，这一轮没有。

判据要求「每条都要能回答为什么这一类允许存在」——上面三条我现在答不成，所以它们还在 1538 里，按「不该留」计，不是被放过。

## 5b. 探针自己的规矩：条件写进 pattern，不许加白名单

`scripts/check-process-traces.mjs` 的头写着：一个命中落在别的域里时，问出那条能区分两者的条件，把条件写进 pattern，**不许加白名单**（前两次是两个例子：shell 注释里的 `/*` 是通配符，`lane-1`/`<lane>-<time>` 是占位）。工作根这一族是第三次，写法与那两次一致：条件在前缀，加上正反两个用例。剩下三条没写成条件，就没写。

## 6. 剩下的 1538 条怎么清（下一轮按族施工）

不是「再跑一次正则」。我实测过两种机械改法，都**被否掉**，证据留在这一格，免得下一个人再走一遍：

- **按 token 删 + 收边**（把 `lane-x` 换成 `the change`、`88zjwei` 换成 `<host>`、日期直接挖掉再收空格）：1500 行改动，92 行丢了标识符 —— `// (`C-1123-17`).` 变成 `//.`，`(host \`88zjwei\`), \`std::gir\`` 变成 `//, \`std::gir\``，`` `merge-lane-docs.ps1` `` 变成 `` `merge-the change.ps1` ``。收空格那条规则还会吃掉省略号（`wrote ... re-hashed` → `wrote... re-hashed`）和 `for :op/mul_d` 的冒号前空格 —— 而 `desc` 里的省略号是**被用例钉住的文本**。
- **按子句删**（只删「纯记录」的括号段或逗号段，保留有内容的）：506 行，剩下 175 行仍红，并且两行一句的段落被拆开时把后半句留在下一行（`… not of an order` / `fixes it (…)`）。

⇒ 施工单：**一族一提交，按文件读、按句子改**；`test/plans` 的 `desc` 是 TOML 字符串又是被引用的文本，改之前先把那一族的 `desc` 全导出来逐条读。量出来的量级：138 个计划带 756 条（其中日期 324 条落在 85 个计划里，裁定归属 197 条），一个计划 1-13 条，最大的一个 95 条。`src/` 已经清完，可以当这一族的样板（15 条，逐条语义不变，见 §1 第二笔）。

## 7. 两句话

- 红的 1988 条不是四十天里长出来的，是四棵树四十天里没被读过一次；`camel-remote.ps1` 里那句「the tree carries none of those comments」当时就没有依据，现在改成了它真正在做的事（`internal/tools/camel-remote.ps1:687-700`）。
- 这一轮只交「删得干净的 432 条」「判据承认的 15 条」和「每一族为什么」；除工作根那一族之外没有一条是被判据放过的，因为其余三条候选的形状都没写成能被反例打穿的条件。