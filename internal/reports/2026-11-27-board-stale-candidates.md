# Stale candidates on the task board, 2026-11-27

A candidate list only: nothing here is deleted, no status changed, no task judged void. The board had
117 `in_progress` rows, ten `pending` and none completed after the prune; 46 of the 117 are candidates
because their owner is inactive. `pending` was not looked at.

## Criterion, and why it is only one

`owner` is inactive **and** that is the whole test. The obvious third test — "the tree its
`write_scopes` point at saw no commit today" — does not work: the trees are a shared pool, so a tree
written to today proves nothing about the lane that was handed it, and a quiet tree proves nothing
either. Measured: `block-hash`, `witness`, `enter-shape`, `perf`, `cli-rework` all carry commits from
today, several written by a different lane than the one that named the task (`perf` was on
`lane/comment-traces-a`, `console2` on `lane/pass-uri-id` at the time of the count).

Two gates are kept apart. `@idle-before-today` is a task whose id predates today; `@stopped-today` is
one dispatched today whose owner has since gone inactive. Mixing them would read a lane that has just
paused as an abandoned task.

## Where the work left a trace

Three places were checked per task, read-only: a row in `internal/backlog.md` naming the id, any sha
on that row, and whether that sha is an ancestor of `origin/integration`.

· `@done` — a backlog row names it and a sha on it is in integration: deleting the row loses no record,
  because the commit and the backlog row are the record.
· `@open` — no backlog row names it: nothing was recorded as done, so deleting it would lose the work.
· `@unclear` — a row names it but no sha on it is in integration: cannot be judged from these three
  places alone. Listed, not guessed at.

Counts: @done 32, @open 7, @unclear 7. Of the 40 that predate today: @done 28, @open 6, @unclear 6.
Of the six dispatched today: @done 4, @open 1, @unclear 1.

## The 46, one per line

```
task-3 | Paper A: edit/view productions and the block body compiled as a program | lane-editview-a | r2 | @idle-before-today | @done | backlog rows 1, shas 1, in integration 1
task-32 | 查 fib(30) 的 nvm 793 ms / fvm 155 ms 为何比历史基线慢三四倍（找回退化可直接修） | lane-backend-speed | r3 | @idle-before-today | @done | backlog rows 2, shas 2, in integration 2
task-34 | 重载选择：when 不成立的候选要在判“有歧义”之前筛掉（stream 的 zip 与 prelude 的 zip） | lane-arrangeable | r2 | @idle-before-today | @unclear | backlog rows 2, shas 0, in integration 0
task-44 | stdlib 的 `return f` 触发自己的警告，把 63 条用例的输出打脏（28 条晋级新红） | lane-editview-a | r2 | @idle-before-today | @done | backlog rows 1, shas 2, in integration 2
task-45 | 把 probe.editview 那 4 条写未实现名字的探针标成已知失败并写原因 | lane-editview-a | r2 | @idle-before-today | @done | backlog rows 3, shas 5, in integration 5
task-51 | edit/view 前端 C：块尾自动提交与 replace(exit(d), v) | lane-editview-a | r2 | @idle-before-today | @done | backlog rows 1, shas 3, in integration 3
task-57 | edit/view A 的缺口：把块头 d 绑到句柄与草稿上 | lane-editview-a | r2 | @idle-before-today | @unclear | backlog rows 1, shas 0, in integration 0
task-70 | 慢层真红第四族：`feat.tools.debugger` 三条逐条判类 | lane-witness | r11 | @idle-before-today | @done | backlog rows 2, shas 8, in integration 8
task-77 | resnet 全批运行吃光默认数据块空间：判“本来就该这么大”还是“有块被留住” | lane-backend-speed | r2 | @idle-before-today | @done | backlog rows 3, shas 2, in integration 2
task-82 | 遍即函数 第 1 步：遍声明照常导出、命令行与 import 同一解析、删两张别名表 | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 1, shas 1, in integration 1
task-89 | debugger：`var` 单元格的名字绑定到初始值节点，形状还从 `var int` 退成 `int` | lane-witness | r2 | @idle-before-today | @done | backlog rows 2, shas 8, in integration 6
task-91 | 资源审计偏差三连：时钟按写算、`pure` 允许读、未声明资源的原生操作按全局效应排序 | lane-arrangeable | r2 | @idle-before-today | @done | backlog rows 3, shas 3, in integration 2
task-97 | 修 `--timeit` 第 2 次迭代的类型检查误判（元组返回被读成多返回值），perf-baseline 多日红的根因 | lane-witness | r4 | @idle-before-today | @unclear | backlog rows 1, shas 0, in integration 0
task-99 | 确认块空间三行读数不破既有 mm 计划，并为“原地累加”取两个比例（2997 个 add 块载荷里有多少是单写者） | lane-backend-speed | r2 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-100 | 把 `time` / `process` 的 `@effect` 散文与生成表改成“写 clock”，并修被行号牵连的三处引用 | lane-editview-a | r2 | @idle-before-today | @unclear | backlog rows 1, shas 1, in integration 0
task-107 | `std::profile opstats=true` 的算子树没有输出（`op_stats.table` 两条红，与 Range 无关） | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 3, shas 4, in integration 4
task-108 | `inspect --reuse`：算子节点的 uses/last-use/别名读数（新开关，`--copies` 逐字节不变） | lane-backend-speed | r2 | @idle-before-today | @done | backlog rows 1, shas 2, in integration 1
task-111 | `var` 单元格的真缺陷：当前值与 `var int` 形状（`dap_session` 1 of 106 的阻塞项） | lane-witness | r3 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-113 | `lane/cpp-noise`：`std::cpp` 独立程序的故障报告走 stderr —— 取证与读数 | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 1, shas 3, in integration 3
task-117 | console 改成“一个资源”之后：3 条 rename/教程红要跟上（2 条身份期望 + 1 条 uses 承诺） | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 2, shas 4, in integration 4
task-122 | 324 条“位置过期”的红：只改位置数字的重录（三类分开验） | lane-block-hash | r3 | @idle-before-today | @done | backlog rows 3, shas 7, in integration 6
task-124 | `std::defunctionalize` 不替换流类型：任何“流 + opt”的程序都红（12 条 + 影响面 14 处 stdlib） | lane-backend-speed | r2 | @idle-before-today | @unclear | backlog rows 1, shas 0, in integration 0
task-133 | `lane/pass-as-function` 换底并收口：删掉“模块可注册别名”那条路（基上缺它） | lane-arrangeable | r2 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-138 | `buildStreamLoop` 不填携带状态、且把机器元组当期望类型：`for … in <range>` 这一族 7 条红 | lane-backend-speed | r2 | @idle-before-today | @done | backlog rows 2, shas 4, in integration 4
task-139 | Camel 侧 `opt::cse` 与 C++ 侧分叉：`by_camel` 的 flags 序列是 `[0,2,2,2,2]` | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 1, shas 1, in integration 1
task-141 | `lane/editview-a2` 补块作用域那半 + 重录 ast 哈希（裁定 A 之后重判为“要补”） | lane-editview-a | r4 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-151 | 规则左边写了 `with` 实参就该只匹配同样实参（现在不看它） | lane-witness | r2 | @idle-before-today | @done | backlog rows 2, shas 4, in integration 3
task-154 | `selfplay.cml` 约 1/4 概率 0xC0000005 崩溃（既有运行时缺陷） | lane-backend-speed | r2 | @idle-before-today | @done | backlog rows 2, shas 6, in integration 5
task-165 | task-165：stdlib 红表收尾 —— A 类 9 行 + B 类 6 行照实测重录 | lane-block-hash | r2 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-172 | task-172：验收 lane/editview-a2 @ c09931ce37（parse 的块头取模块） | lane-backend-speed | r2 | @idle-before-today | @open | backlog rows 0, shas 0, in integration 0
task-192 | task-193：丢弃返回值的判据放到调用点 —— 读选中候选的 `with` 形参类型（void ⇒ 报 UnusedPureResult，无约束 F ⇒ | lane-block-hash | r2 | @idle-before-today | @unclear | backlog rows 1, shas 0, in integration 0
task-194 | task-195（3b 图车道）：编辑会话的两个入口 —— 记种类/碰过区域/通知监听者 + 发布时分档检查 | lane-enter-shape | r4 | @idle-before-today | @done | backlog rows 6, shas 4, in integration 4
task-196 | task-197（3b 事实车道·2）：五样分析迁进事实框架、删掉各自的缓存（五套 → 一套） | lane-backend-speed | r2 | @idle-before-today | @done | backlog rows 2, shas 2, in integration 2
task-197 | task-198（3b 事实车道·3）：足迹规则只写一次 + 未知访问一种表示 + 纯度判断清零（33 处） | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 4, shas 4, in integration 4
task-204 | C1（CC 六格）：遍即函数 第 1 步收尾（删 passAliases 与 isPassDeclaration 特例）+ 第 2 步（遍只在算子表登记一次 | lane-arrangeable | r7 | @idle-before-today | @done | backlog rows 5, shas 3, in integration 3
task-226 | 足迹遍历只留一套：`FootprintWalk` 改收 `GraphView`，建图器对草稿取视图调用，删 `BodyReach` | lane-block-hash | r2 | @idle-before-today | @done | backlog rows 2, shas 2, in integration 2
task-227 | B1 效应事实片 1：`core.effects` + `core.nodeEffects` 完整迁移（登记 + 切读者 + 删旧 `KindSpec`，同 | lane-arrangeable | r6 | @idle-before-today | @done | backlog rows 1, shas 1, in integration 1
task-233 | C7：`onnx::model` / `with <model>` 的 `onnx::run` / `with <out>` 的 `onnx::save`； | lane-block-hash | r7 | @idle-before-today | @done | backlog rows 3, shas 1, in integration 1
task-235 | 解析器：删掉仍接受冒号返回类型与箭头 lambda 的分支（走普通语法错误），补一条负例用例，重生成语法参考两张表 | lane-backend-speed | r3 | @idle-before-today | @done | backlog rows 1, shas 1, in integration 1
task-236 | 查清 9 条 SIGABRT（probe.editview/probe.blockscope）：上次晋级 killed=0、本轮 killed=9 ⇒ 界定 | lane-arrangeable | r2 | @idle-before-today | @done | backlog rows 1, shas 2, in integration 2
task-250 | C1 step 2: pass kernels live under the #op URI as an OperId, and the exemption | lane-witness | r3 | @stopped-today | @done | backlog rows 3, shas 10, in integration 9
task-251 | C2: a program calls a pass as a function | lane-editview-a | r2 | @stopped-today | @done | backlog rows 3, shas 10, in integration 9
task-256 | Compile-view: name the frozen graph and the draft, and keep the type of the ke | lane-arrangeable | r2 | @stopped-today | @done | backlog rows 1, shas 10, in integration 9
task-258 | graph_draft_ledger.never_rises: the red that holds promotion back | lane-editview-a | r2 | @stopped-today | @unclear | backlog rows 1, shas 0, in integration 0
task-259 | gct Tree: eight of the ten Node-keyed side tables are dropped when a node is c | lane-arrangeable | r2 | @stopped-today | @done | backlog rows 1, shas 2, in integration 2
task-260 | Regenerate the AI reference, and bisect why the normalized GIR hash moved | lane-witness | r2 | @stopped-today | @open | backlog rows 0, shas 0, in integration 0
```

## Three shapes that look abandoned and may not be

```
1. One owner holds five to eleven rows (lane-block-hash 11, lane-backend-speed 10). That reads more
   like one lane being handed a chain of work than eleven forgotten tasks.
2. A large revision (task-70 r11, task-204 r7, task-227 r6) means the row was rewritten several times:
   a row waiting on a ruling looks the same from outside as a row nobody took.
3. The six dispatched today are @stopped-today: their owner paused, it did not leave. Judging them by
   the same gate as the older rows would call a pause an abandonment.
```

## Second cut: is the commit the task, or just a row that mentions it

These three classes are a criterion, not a conclusion. The first cut called a task `@done` when a
backlog row named it and a sha on that row was in integration. That is not enough: a row often names
several ids, and a sha on it can belong to a different task entirely. So for each of the 32 the sha on
the row was read back with `git log -1 --format="%h %s"` and compared with the task subject.

· `@remove` — the commit title states the work the subject states. The commit is the record, so the
  board row adds nothing.
· `@keep` — the title is about something else, or the id is only mentioned in passing. The row is then
  the only trace this task has: do not delete it.
· `@mixed` — the title is a plan or a partly-overlapping change, so the sha cannot be called this task's
  delivery. Not guessed at.

Counts: @remove 11, @keep 20, @mixed 1.

```
task-3   | Paper A: edit/view productions                    | e16717d31 | collab: a dispatch is a task, and the merge seat takes work from the board | @keep
task-32  | fib(30) nvm 793 ms / fvm 155 ms slower than before   | aa2d3dbaa | Merge lane/transfer into integration | @keep
task-44  | stdlib `return f` warns on its own, 63 cases dirtied  | db6ca4622 | stdlib(opt): dead_values ends when nothing is left to erase | @keep
task-45  | four probe.editview probes marked known-failing      | d1d4ef3bb | test: a named run does not pass its name to the runs its cases start | @keep
task-51  | edit/view C: tail commit and replace(exit(d), v)      | 29a27d65b | stdlib(opt): cse finds the first equal node by the draft's own order | @keep
task-70  | slow-layer reds: three debugger cases judged by class  | 8749ac78c0 | runtime: a conversion and a copy are calls of :op/cast and :op/copy | @keep
task-77  | resnet batch eats the default block space             | e0cfa48a23 | probe(mm): judge whether resnet's block space is what it should be | @remove
task-82  | pass-as-function step 1: declarations still exported   | 291e32b76 | plans: passes are functions in naming, registration and calling | @remove
task-89  | debugger: the var cell names its initial node          | 26d9a37e85 | Merge fsh-dev: a session that is gone leaves nothing in memory | @keep
task-91  | resource audit: clock counted as a write, pure as a read | 1d9ebcbc8 | docs, plans: modifiers are the equations writes satisfy | @keep
task-107 | std::profile opstats=true prints no operator tree      | 535d928be7 | test(tensor): build the array a range names through its one conversion | @keep
task-108 | inspect --reuse: uses / last-use / alias readings      | 7881492071 | inspect: read what every operator node was told about its result's storage | @remove
task-113 | lane/cpp-noise: std::cpp fault report goes to stderr   | c65c0bf6b6 | console: the console is one resource | @keep
task-117 | after console became one resource: 3 reds to follow     | c65c0bf6b6 | console: the console is one resource | @remove
task-122 | 324 stale-position reds: digits only, three classes     | 05bdceadfc | test: the prose that says what `pure` forbids says no write | @keep
task-138 | buildStreamLoop does not fill the carried state         | 36140ad994 | fix: hand a stream loop the state its body's return gives | @remove
task-139 | Camel-side opt::cse diverges from the C++ side         | d5f0ac1233 | docs: the pages stop saying a translating pass consumes the graph | @keep
task-151 | a law written with `with` arguments should match them  | 79d21aa586 | transpose: a law that writes a <> value on its left side answers that node | @remove
task-154 | selfplay.cml crashes 0xC0000005 about one run in four   | 36140ad994 | fix: hand a stream loop the state its body's return gives | @keep
task-194 | 3b graph lane: the two entry points of an edit session | f0ba3eb30 | Schedule: until the CC deadline six slots go to CC features | @keep
task-196 | 3b facts lane 2: five analyses into the facts framework | f0ba3eb30 | Schedule: until the CC deadline six slots go to CC features | @keep
task-197 | 3b facts lane 3: footprint rules written once          | 9a4f8ac4bc | backlog: the footprint walk unifies on the view | @remove
task-204 | C1: close step 1 (delete passAliases) and step 2        | 8f472bed1d | compile: a pass is the declaration the module implements no operator for | @remove
task-226 | one footprint walk: FootprintWalk takes a GraphView     | 9a4f8ac4bc | backlog: the footprint walk unifies on the view | @remove
task-227 | B1 effects facts: core.effects and core.nodeEffects     | 71dcbe43bd | Merge lane/facts-kinds: four kinds join the facts framework | @remove
task-233 | C7: onnx::model, with <model>, onnx::run               | c05c2de39 | CC plan: C7, the in-program ONNX model run through ONNX Runtime | @mixed
task-235 | parser: drop the colon return type and arrow lambda     | eba9e377a | Principles: the examples write the return type after => | @mixed
task-236 | find the nine SIGABRTs in probe.editview/blockscope     | 36419e2003 | Merge fsh-dev: a lane works its card without stopping for context | @keep
task-250 | C1 step 2: pass kernels under the #op URI as an OperId  | 215094fb18 | Merge fsh-dev: the host's heap is read every beat | @keep
task-251 | C2: a program calls a pass as a function                | 215094fb18 | Merge fsh-dev: the host's heap is read every beat | @keep
task-256 | compile-view: name the frozen graph and the draft       | 215094fb18 | Merge fsh-dev: the host's heap is read every beat | @keep
task-259 | gct Tree: eight of ten node-keyed tables dropped on copy | bd97680b77 | gct: a copied subtree keeps the names and the block head its nodes carry | @remove
```
