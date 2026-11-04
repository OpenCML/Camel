# `tier = "milestone"` 的计划该由谁、在什么时候跑？

## 问题

今天这个问句的答案是**「没有人，没有时候」**：28 张计划文件、3518 条读数住在 `milestone` 层，
而**五种能跑计划的入口里没有一个会去跑它**，唯一的入口 `npm run test:milestone`
（`package.json#scripts`）在整棵树里只出现在它自己那一行——没有人调用它。

`test/plans/feat/opt/progen.plan.toml` 的 `tier = "milestone"` 是实测过的样本：`npm run test`
是 `--tier fast`，合并车道的闸门也不带 `--tier`（默认 `fast`），所以那 2 条读数（`GATE`
读到可能没被取到的臂内值就是其中一条）一次也没有被闸门执行过，缺陷就这样到了 `develop`。
**这不是读错数，是根本不产生读数。**

## 数字（2026-11-04，`origin/develop` `33daa3003`）

**一、`tier` 的取值（`git grep -n '^tier' -- test/plans/`）**：`377` 行，落在 `352` 份文件里
（`363` 份计划中 `11` 份一行也没有）。**取值 5 个**，按文件顶部那一行（计划级）：

| `tier` | 计划文件 |
|---|---|
| `fast` | 312 |
| `milestone` | 24 |
| `slow` | 9 |
| `feat` | 6 |
| `debug` | 1 |
| 没写 | 11 |

按「文件里出现过哪个词」数（同一份文件可以出现多次，测试级 `tier` 就写在这里）：
`fast` 314、`milestone` 27、`slow` 15、`feat` 6、`debug` 1。

**二、三层各覆盖哪些 `tier`（判据取自代码，不取文档）**：
`test/tools/run-tests.mjs#testTier` —— 测试自己的 `tier` 优先，否则看计划：`slow` / `milestone` /
`debug` 三个词是「有名字的目的地」，**其余一切（`fast`、`feat`、以及没写）都是 `fast` 层**；
`test/tools/run-tests.mjs#selectTier` —— `fast|slow|milestone` 各取自己那一层，
`all` 取 `testTier !== 'debug'`，`debug` 取 `plan.debug_tier === true || testTier === 'debug'`。
按这个判据读今天的树：

| 层 | 判据 | 计划文件 | 条目 |
|---|---|---|---|
| `fast` | `testTier === 'fast'` | 331 | 8804 |
| `slow` | `testTier === 'slow'` | 15 | 163 |
| `milestone` | `testTier === 'milestone'` | **28** | **3518** |
| `all` | `!== 'debug'`（fast+slow+milestone） | 362 | 12485 |
| `debug` | `debug_tier` 或 `tier = "debug"` | 23 | 2 |

**三、谁会跑它们（闸门侧）**：

| 入口 | 命令 | 覆盖 |
|---|---|---|
| `npm run test` | `node scripts/test.js --tier fast`（无目标＝整棵树） | 只有 `fast` |
| 合并车道的闸门 | `camel-remote.ps1 -Plan smoke,test/plans/feat`，不带 `--tier`（默认 `fast`） | 只有 `fast`，且只在 `smoke`+`feat` 里 |
| `npm run test:all` | `--all --tier all` | `fast`+`slow`+`milestone`，**`debug` 不进** |
| `npm run test:milestone` | `test/plans --tier milestone` | `milestone`，**没人调用** |
| 发布闸门 | `scripts/release-gate.mjs` | fast 整棵树 + slow 按名单；`#printSkipList` 明确打印 `not run here: the milestone layer` |
| CI | `ci-leg.yml` 跑 `node scripts/test.js`；`ci.yml` 跑 `smoke` + 两张编译计划；**无 cron** | 只有 `fast` |

**四、点名**（第 3 步的答案，两种零分开数）：

- **没有任何层会跑的计划：1 条。** `test/plans/debug/view_escape.plan.toml`——2 条条目都写着
  `tier = "debug"`；把它滤掉的是 `#selectTier` 里 `all` 那一行（`testTier !== 'debug'`），
  能选中它的只有 `--tier debug`（要 Debug 构建，`#selectTier` 的 `debug` 分支），而发布闸门对
  这一层写的是「never part of a Release gate」（`scripts/release-gate.mjs#printSkipList`）。
  它自己的 `desc` 也这么写：「runs only under `--tier debug` on the Debug build」。
- **有层、但没有任何闸门会跑的计划：28 条（`milestone`）。** 前三名按条目数：
  `test/plans/feat/mm/gc_poison_sweep.plan.toml`（3272 条：它的计划级 `tier` 写 `fast`，
  未抽样的扫描副本才是 `milestone`——**「`tier` 是文件的属性」这句话在这里读错**）、
  `test/plans/feat/mm/gc_collections.plan.toml`（73）、`test/plans/feat/mm/gc.plan.toml`（44）。
  其中 `perf/**` 19 张、`gpu/tiles_regions` 1 张、`feat/**` 8 张。
- 一句话：**闸门读的是 `fast`；`all` 会带 `milestone`，但 `all` 只在发布闸门打印的那行「not run
  here」里出现过，不是任何一条车道的命令。**

## 文档与代码不一致（不一致本身就是缺陷，按规矩只记不改）

1. `AGENTS.md`（Test Execution）写「`--tier fast|slow|all` 选一层」——代码的合法值是
   `fast|slow|milestone|all|debug`（`test/tools/run-tests.mjs#TIERS`）。**文档漏掉的
   `milestone` 正是 28 张计划住的那一层**，而 `debug` 漏掉也不显眼，因为它只跑 1 张。
2. `AGENTS.md` 写「`npm run test:all` 跑 `test/plans` 下的**每一张**计划」——代码里
   `all` 明确排除 `debug`（`#selectTier`），所以今天有 1 张计划（2 条）不在里面。「每一张」不成立。
3. `agents/test.md` 写「`--tier milestone` 选中 27 张计划：`tier` 是**文件**的属性」——今天是
   **28** 张、3518 条；而 `tier` 并不只是文件的属性：`jit_framed` 57 条里 6 条、`onnx` 158 条里
   4 条、`showcase_i2` 5 条里 1 条、`gc` 46 条里 44 条是**测试级**写的，`gc_poison_sweep` 则是
   计划级 `fast` + 扫描派生出的 3272 条 `milestone`。27 是表时代的数，两个方向都读错了。
4. 小的那条：`AGENTS.md` 写 `npm run test` 跑「`smoke` 加 `feat/**`」，代码是无目标整棵树
   `--tier fast`，`gpu/`、`probe/`、`test/plans/debug/` 里快层条目也在内。
5. `tier = "feat"` 的 6 张计划：`feat` 不在 `#TIERS` 里，却因为 `#testTier` 把「其余一切」都
   当 `fast` 而静默合法（测试级写未知值会抛）。**旧的词靠兜底活着，没有一个检查说它还对不对。**

## 三条路与代价

**A. 合并闸门无条件加一层 `--tier milestone`（或 `npm run test:milestone`）。**
代价：**每次合并** +28 张 / 3518 条。同一台主机 `-Jobs 8` 的量级锚点：`-Plan all -Tier all`
10050 条用 2875.8 s（`docs/handover/batch-slow-2026-10-26.md`，tip `e7a8b906d`），
`test/plans -Tier slow` 约 2950 条用 1953.4 / 2191.6 s（`docs/handover/batch-slow-2026-10-24.md`）；
3518 条落在这两条之间 = **20–45 分钟**（两条旧读数推的，本 tip 没量过）。改一个注释也付这笔。
另外这一层里有 GC stress、perf、PyTorch 比较，`agents/test.md`「Run the plans your change
touches」明说要主机安静时才跑——**无条件跑等于把禁止项排进每次合并**。

**B. 只跑改动涉及的 `milestone` 计划（先 `git diff --name-only`，再列这些文件所属的
`milestone` 层计划，逐个跑）。**
代价：一次 diff + 一个 lister，毫秒级；跑的是「这张计划」而不是「这一层」，progen 那 2 条是
秒到 2 分钟级（其中 `progen_check` 一条 150 s 预算，`agents/test.md` 记 133 s）。**但代价不恒定
地小**：拿一笔真的 `src/passes/opt/` 改动（`624e41a87` 的 2 个文件）量，选中 197 张计划，其中 6 张
带 `milestone` 条目、合计 **3419 条**——3272 条来自 `gc_poison_sweep` 的扫描副本（它镜像了
`feat/` 下几乎整棵树）。这种改动下 B 会退化成接近 A；**区别在于 B 是 3419 条里看得见的 3419
条（lister 逐张打印），A 是每次都 3518 条**。所以 B 还欠一条：扫描派生的 `milestone` 副本按源
计划的抽样跑（`fast_sample` 今天在 fast 层做的就是这件事），而不是整批跑——那是 runner 的改动，
这一页不动。
漏的是**跨计划影响**：改 `src/passes/opt/` 会选中 197 张计划，其中只有 6 张是 `milestone`，
所以这个方案只在「计划与文件绑得住」时成立；绑不住的恰恰是扫描派生（`gc_poison_sweep`）和
`except` 名单（`perf/**`、`gc*`、`progen`、`tiles_regions` 这些计划**自身的改动**按
`test/plan-selection.json#rules` 的自选规则被排除，回落到整层 `fast`）。**所以这个检查必须把
「洞」打出来而不是静默返回空名单**——今天写好的 lister 就是照这条做的（见下）。

**C. 扩 `fast` 层（把这 3518 条搬进每个人的内循环）。**
代价：`fast` 现 8804 条，加 3518 条 = **+40%**，每个开发者的每次内循环都付；而且搬进来的正是
不许在共享主机上跑的 GC stress / perf / PyTorch 比较。**这是把代价换个地方付，不是修好。**

## 默认建议（已按此继续，不等回复）

**在合并车道的闸门里加一步：先 `git diff --name-only` 取改到的文件，再列出「这些文件所属的
`milestone` 层计划」，逐个跑；不无条件跑全部 `milestone`。** 判据是已有规矩的自然延伸——
「改过的文件所属的每一张计划都出现在读数里」（`agents/test.md`「Run the plans your change
touches」、`test/plan-selection.json#rules` 的 `--changed`）。

这一步的检查器已经落地并在今天跑过：`scripts/milestone-for-changes.mjs`
（计划集来自 `test/runner.mjs#plansForChanges`，层来自 `testTier` 与
`scripts/plan-inventory.mjs#milestoneLayer`，两条规则都不在这里重写）。它把上面 B 的两个洞
当**红**报出来（exit 1 并点名），而不是回一个空名单：

```
$ node scripts/milestone-for-changes.mjs (624e41a87 改到的 2 个文件)
[ms] 2 changed file(s) -> 197 plan file(s) selected -> 6 with a milestone entry (3419 of the layer's 3518 entries)
[ms]   test/plans/feat/linear/jit_framed.plan.toml: 6 milestone of 57 entries (tier fast)
[ms]   test/plans/feat/mm/gc.plan.toml: 44 milestone of 46 entries (tier milestone)
[ms]   test/plans/feat/mm/gc_collections.plan.toml: 73 milestone of 73 entries (tier milestone)
[ms]   test/plans/feat/mm/gc_elder_exhaustion.plan.toml: 22 milestone of 22 entries (tier milestone)
[ms]   test/plans/feat/mm/gc_poison_sweep.plan.toml: 3272 milestone of 3484 entries (tier fast)
[ms]   test/plans/feat/opt/progen.plan.toml: 2 milestone of 2 entries (tier milestone)

$ node scripts/milestone-for-changes.mjs test/plans/feat/opt/progen.plan.toml
[ms] 1 changed file(s) -> 0 plan file(s) selected -> 0 with a milestone entry (0 of the layer's 3518 entries)
[ms] no milestone plan is selected: this change runs no milestone entry
[ms] hole: test/plans/feat/opt/progen.plan.toml (2 milestone entries, not selected by the rules)
[ms] hole: no rule matches any of the 1 changed file(s): --changed would run the whole fast layer
```

第一条是闸门该有的读数：改 `src/passes/opt/` 的人会被要求跑那 6 张里的每一张，progen 的 2 条在
名单上第一眼可见，`gc_poison_sweep` 的 3272 条也在——**不是被平均值吞掉的一个「大约」**。第二条
是**这条建议自己的边界**：按现在的自选规则，编辑 progen 计划本身反而跑不到它——**这一步补不上
那个洞，只把它显出来**；补它要给 `test/plan-selection.json#rules` 一个「计划自身的改动也跑它
自己」的例外，那是另一笔（并且要作者裁定，因为那条 `except` 是明确写的），这一页不动它。

**不动任何计划的 `tier`。** 把 `milestone` 改成 `slow` 或 `fast` 不是修好，是把代价换个地方付
（见 C）。若某张计划的 `tier` 真错了，写在这里：`tier = "feat"` 那 6 张（
`feat/compile/{expected_type_through_calls,fstring,literal_context}.plan.toml`、
`feat/linear/loop_collect.plan.toml`、`feat/modules/json_accessor_error.plan.toml`、
`feat/modules/tensor/named_dims.plan.toml`）用的是不在 `#TIERS` 里的词，只是被兜底成 `fast`。

## 关联

- `docs/handover/reports/2026-11-02-the-milestone-layer-nobody-ran.cn.md`：这一层第一次被量的
  那一夜（`3302/3286/16`，三条 tip 三份红单），以及「报 `[SUMMARY]` 必须带 `-Tier`」；
- `docs/handover/reports/2026-11-03-a-whole-plan-excluded-for-one-entry-in-it.cn.md`：整份计划
  因为其中几条归 `milestone` 而被划走的那一族；
- `agents/test.md`「Run the plans your change touches」：这一层「at a milestone, by the
  coordinator」的说法就写在那里——**那是「靠人记得」，不是判据**，这一页要说的是把它换成判据。