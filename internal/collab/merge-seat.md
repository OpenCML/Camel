# 合并位（merge seat）

八格里**两格常驻**，不挪（D-1126-1）。这条页给任何一个合并位看：它自己该做什么、别人该给它什么。

## 两格的分工

| 座位 | 写什么 | 不写什么 |
|---|---|---|
| `seat/merge-1` | 把批并进 `integration`、跑该批判据、过闸、推 `origin/integration` | 不碰 `develop`（只有晋级那一步快进） |
| `seat/merge-2` | **准备**：把待合的批预合到当前 `origin/integration`，报干净/冲突/分级；解 a、b 类 | **不推 `integration`**、不动 `develop`、不开别人的分支做预解（先问） |

`integration` 只有集成步骤一个写者。两格同时推 = 两个写者，所以 2 号位只能准备。

## 晋级：一条命令（`scripts/promote.mjs`，`fsh-dev 0addc031b`）

```powershell
node scripts/promote.mjs start [--tip <ref>] [--publish]   # 起了就走，几秒返回任务号
node scripts/promote.mjs status [<id>]                     # 轮询进度与结果
node scripts/promote.mjs cancel [<id>]                     # 取消
```

- 工作进程**有心跳**：进程死了，下一次 `status` 就判死、清理并放锁 —— 所以“卡住”的判据是 `status` 说它死了，不是“我等了很久”。
- `--publish` **只推量过的那一笔**；没有 `--publish` 就只量不推。
- 默认整层命令用 `camel-remote -TokenClass funnel`（晋级是所有车道都在等的那一趟，令牌池里排在最前）；`summary.json` 里有每个计划的 `plan_path` 与每行 `test_timings`（含 `timed_out`）。
- **超时只由运行器重跑一次**（单独、两倍时限）；晋级只读它写在 `test_timings` 行上的最终状态，不再自己重跑。层命令不是以 0 或 1 退出、或读数缺了上次读数里的用例，整个任务判失败、不发布。
- 第一次真跑要在构建机上核五件：`-FetchFile summary.json -FromThisRun` 能否取到工作树、远端清理 `-Clean` 是否合适、`plan_path` 能否直接喂 `-Plan`、`--publish` 时钩子是否放行。

## 怎么合：一条命令

成批乐观合并（`internal/plans/2026-11-26-optimistic-merge.cn.md`）只有一个工具，`scripts/merge-batch.mjs`。
座位的工作树要干净；车道分支要在座位里能解析（先 `git fetch`）。每条车道给它交回时的那一行读数：

```
node scripts/merge-batch.mjs --seat <座位工作树> --lane lane/a="<读数一行>" --lane lane/b="<读数一行>"
node scripts/merge-batch.mjs --seat <座位工作树> --lanes <lanes.json>            # [{"branch": "...", "reading": "..."}]
node scripts/merge-batch.mjs --seat <座位工作树> --lanes <lanes.json> --push     # 编译通过后推 integration
```

它做的事：把座位放到 `origin/integration` 尖端，依次把车道合成合并提交（第一父是合并时的尖端），文本冲突的跳过、
退回车道自己换底；整批做一次增量编译（默认是对座位跑 `internal/tools/camel-remote.ps1 -TokenClass funnel`，不带计划，只构建）；
编不过就按合并顺序对前缀二分，找到第一个编不过的前缀，拿掉它的最后一条车道，把后面的在最后一个好前缀上重合，
再编一次，直到编过。合并说明列出车道的提交标题，末尾是 `Lane-Reading: <读数一行>`（只改文档的写
`Lane-Reading: docs-only`），推送钩子逐笔查这一行。不带 `--push` 时停在编译之后，只打印汇总：合了哪些、
跳过哪些和原因、编译几次几秒、总共几秒。

推送钩子还查一件事：推上 integration 的这一段里，凡是改了构建要读的路径（`scripts/common.js` 的 `BUILD_INFO_ROOTS`），
从尖端顺第一父往回走，在碰到这样一笔之前必须先碰到一笔"构建过"的提交。`camel-remote.ps1` 在干净的树上把某个提交编过时，
在公共 git 目录下记 `camel-built/<sha>`；`merge-batch` 的那次编译就会留下这条记录。所以手工做的合并或撤回，
推之前也要对座位跑一次 `camel-remote.ps1 -TokenClass funnel`（不带计划），编不过的尖端推不上去。

**本地 `npm run build` 对钩子无效**（2026-11-27 交付夜实测，卡了 40 分钟）：座位在本地编出 `BUILD_EXIT=0` 也**不写** `camel-built/<sha>`，
钩子仍拒，原文是
`pre-push: refusing to push HEAD to refs/heads/integration: <sha>… changes src/tools/camel-cli/config.cpp, which the build reads, and no commit from there to the tip was built.`
改成对座位跑 `camel-remote.ps1 -Path . -Jobs 1 -TokenClass funnel -NoGpuWatch`（`BUILD_EXIT=0`、`BUILD_SECONDS=30`）后一次通过。
⇒ 判据只有一条：**推之前必须有一次 `camel-remote` 留下的构建记录**，本地编过不算。看到上面那句拒文，不要重跑合并，去补那次 remote 构建。

`--build "<命令>"` 换编译命令（`{seat}` 代表座位路径；退出码等于 `--red-exit`，默认 1，才算"源码编不过"，
别的非零码是没跑成，工具停手、不拿掉任何车道）。推送被拒（尖端动了或钩子拒绝）退出码 3：从新尖端重跑。
整批编不过时，工具在二分前先单独编一次起点（集成尖端）：起点本身也编不过，退出码 4，不拿掉任何车道、不推送，
并说明"集成尖端本身编不过"，先修尖端再重跑；起点编得过才开始二分。

## 晋级：一条命令，起了就走，轮询看

晋级是乐观合并的安全网，约两小时一次：对 `integration` 尖端跑整个 fast 层，超时的逐个单独加倍限时重跑，
与**紧邻上一次晋级**的红表按 `plan_id` + 用例名比，写红表、逐例耗时表和晋级记录，没有新红就把 `develop`
快进到**被测的那个提交**。工具是 `scripts/promote.mjs`，**不在前台等它**：`start` 起后台工作进程就返回，
进度靠 `status` 轮询。

```
node scripts/promote.mjs start [--tip <ref>] [--publish]    # 秒回：打印任务号与 status 命令
node scripts/promote.mjs status [<任务号>]                  # 阶段、已用时、日志末尾、判词；缺省看最近一个
node scripts/promote.mjs cancel [<任务号>]                  # 结束工作进程整棵树、清远端运行、放锁
```

- 阶段依次是 `measure`（整层）、`compare`、`records`，带 `--publish` 且无新红再加 `publish`；
  终态是 `done` / `failed` / `cancelled` / `dead`。轮询间隔随意，起一个后台提醒或每几分钟看一眼，**不要**用前台命令等。
- 同一时刻只有一个晋级（一把锁）。锁的持有者已死就当场回收，不会一直等；持有者活着时 `start` 退出码 3 并给出它的任务号。
- 工作进程每 20 秒心跳；`status` 发现工作进程不在了（或 pid 已被别的进程占用）、或心跳超过 180 秒没动，就记为 `dead`，
  结束它留下的子进程树、清理远端运行、放锁。**看 `status` 本身就是清理**，没有要手工回收的孤儿。
- 新红时判词里列出红名，并列出区间内的合并提交（`上次晋级尖端..本次尖端` 的第一父链合并）供二分；工具**不自己二分**，
  二分用 `scripts/bisect-merge.mjs`。
- 三份产物写在任务目录 `<stateDir>/promotion/<任务号>/reports/`：`<日期>-promotion-<n>-reds.tsv`、`-durations.tsv`、
  `.record.json`（记录里 `comparison.against` 写明比的是哪张表；上一次晋级有记录而没有红表时报错而不是去比更早的表）。
  **只有真正发布了的晋级，才把这三份提交进仓库的 `internal/reports/`** —— 带新红的红表进了仓库，下一次就会拿它当基线。
- `--publish` 的步骤：在发布工作树（`--publish-worktree`，缺省取本机设置的 `mergeWorktree`）里把三份产物提交在被测提交之上
  （钩子读的是推送所在树的 HEAD，记录写不进它自己所记的提交），再推**被测提交本身**，不是记录提交；推完读回远端核对。
  记录提交留在分支 `record/promotion-<n>`。
- 命令都可换：`--layer`、`--cleanup`、`--publish-command`（`{worktree}` `{sha}` `{jobs}` 会被替换；
  命令行按空格分词、双引号成组，**直接起程序、不经 shell**）。整层命令必须在 `{worktree}/test/results/fetched/summary.json` 留下运行器的汇总文件（默认命令用 `-FetchFile summary.json -FromThisRun` 把本次运行在构建机上自己名下的那份取回到这里，不会取到同一检出里后来出现的别的运行）。

## 批号（队列页给的号，这里对上靶子）

队列页 `internal/plans/2026-11-25-lane-queue.cn.md` 只写条目名、不带批号；批号活在本页，
因为只有合并位需要它。**快照会过期，判据是合并树，不是 `git cherry`。**

| 批 | 分支 | 2026-11-26 08:15 的状态 |
|---:|---|---|
| 7 | `lane/cc-int8` | 合并零改动（内容已在 `integration`） |
| 8 | `lane/milestone-fast` | 合并零改动 |
| 12 | `lane/docs-pass-forms` | 合并零改动 |
| 11 | `lane/export-matrix-path` | 冲突 b：`scripts/gen_export_matrix.py` 10 hunk，取车道侧 |
| 13 | `lane/walkmachinery-enter` | 冲突 b：1 hunk，取车道侧（取集成侧编译不过） |
| 6 | `lane/test-module-name` | 冲突 **c**：方向相反，停手等裁 |
| 14 | `lane/traces-zero` | 最后一笔，17 提交，需 `-Check ast-hashes --base` |

「合并零改动」是这一页最有用的一条读数：`git merge-tree` 出的树与 `integration` 自己的树逐字节相同 ⇒
**没有待合内容**，不是"合并成功"。`git cherry` 会在这三批上各报一笔 `+` —— patch 不等价 ≠ 内容缺失。

## 每一批必须做的四件

1. **说明写内容。** 合并提交的正文列被合进来的东西：每条车道自己的提交标题加它的读数行，
   由 `scripts/merge-batch.mjs` 写；`Lane branch: lane/x at <sha>` 单独一行不算说明（AGENTS.md）。
2. **`.cml` 改动 ⇒ AST 账。** 批里改过 `.cml` 就在合并后 `-Check ast-hashes --base` 复测；红了窄口重录
   （`-UpdateAstHashes -AstOnly <精确路径逗号连接>`）**作为这一批自己的重录提交**；不许手写 hash、不许全量。
3. **红要分级。** 语料/记录/旧切点（同类）可自修；**方向相反**（c）停手报，不自己解。
4. **零改动也要报。** 报"没有待合内容"比报"合并成功"有用。
5. **撤一笔合并之前，先查金标有没有被单独按它重录过（`D-1127-207`）。**
   `git log -G <金标文件>` 找"只改金标、按那笔的新形状重录"的提交：有，就**把它和那笔一起撤，或当场重录**。
   不然编译器退回去了、记录留在新形状上，两边落到两侧 —— 2026-11-27 的例子：19:04 一笔把 16 条块探针按"块根成为节点"重录，
   19:56 revert 掉那笔编译器改动，集成尖端的 `ast_record` 就凭空多出 16 条"新红"，看着像既有红，其实是自己造的。

## 接手一条分支 = 冻结它

2026-11-26 08:20 的实例：1 号位把 `lane/traces-zero` 解到 `4be8d3152`，同一条分支又往前走了 5 笔
（`848c8f0d96`，含它自己的 207 行重录）⇒ 解了 28 分钟的解冲突作废，整批重挂到新尖端重做一遍。

**规矩：合并位说"我要合这条分支了"那一刻起，那条车道不再往上提交。** 要接着做就等这一批落地、
从新的 `integration` 开下一页；急着做就明说"这一批先合到旧尖端，后面的另起一批"。
车道交回时的 tip 就是合并位要合的那个 tip，两边不是同一棵树时不许动手。

## 别做的事

- 不 `git stash`（共享给一百多个工作树）。
- 不杀 git 进程；`index.lock` 用 `scripts/check-index-lock.mjs` 查持有者。
- 不重写历史提交去补说明；缺内容的在报告页补账。
- 别人的分支上做预解，先问协调者。
## 零待合内容的分支：存档后删，不要合（D-1126-3）

`merge-tree` 出的树与 `integration` 自己的树**逐字节相同** ⇒ 内容已进树，**合进去什么都不会变**。
这种分支**不要合**：合并位会正确地拒绝它（`the base and this tree are the same commit … there was no change to measure`，exit 2），
因为门禁不肯把“没测”写成“绿”。**处理办法是存档后删**：

```
① 打存档标签（本地，不推）：`git tag archive/<日期>-<lane> <tip>`
② 工作树必须干净、无 `.git/index.lock`、无活会话
③ `git worktree remove <路径>`（**不许 `--force`**）→ 删分支**连同它在 staging 裸库里的 ref**：
   `pwsh -NoProfile -File internal/tools/camel-remote-cleanup.ps1 -Branch <分支>`
   先看不带 `-Apply` 的清单（tip、内容还在哪、要删什么），再加 `-Apply`
   —— 这里 `-d` 会拒绝（它不是祖先），所以这个脚本用 `-D`；依据是第 ①/② 步的读数，不是“我看它像已合”
   —— gate 留下的 `refs/heads/<分支>` 是下一次推同一分支只推几个对象的原因（不然要重推整段历史），
      所以**分支没了它也就不该留**：脚本一条 ssh 删掉 ref，并把两边的条数打出来
④ 报：五条各自的标签名、tip、删除前后的 `lane/*` 计数与 staging ref 计数
```

**常驻规矩：**经 `b/<lane>` 重挂副本合进去的车道，**合完当轮就按上面这一步删原分支**，不要留到计数里去。
2026-11-26 的实例：5 条（cc-int8 / cc-onnx-loop / docs-pass-forms / milestone-fast / overload-guard）挂在“未合 21”里，
其中没有一条是待办；删掉后闸门 21 → 16。

## 车道的绑定与清理：一条车道 = 一个工作树 + 一个 teammate

绑定表**不记在这里**：绑定只记一处，就是插件按格子算车道的 `oversight_bind({agent, slot: merge|dev|misc|buffer, lane?, worktree?})` / `oversight_unbind({agent})`；条带显示「开发 x/8」与合并、杂项、缓冲各几条。下面这张表只说明每个格子该放什么，**具体的 agent↔车道对应以插件那张表为准**：

| 格子 | 放什么 | 命名 |
|---|---|---|
| `merge` | 成批合并位（1 号）、晋级位（2 号） | `seat-merge-<n>` |
| `dev` | 瘦身 4 + 论文难项 3 + 提速 1 = 8 格 | `lane-<主题>` |
| `misc` | 常驻杂项（只动文档、脚本、工具、测试，不改语义） | `lane-misc` |
| `buffer` | 加急修复、唤起换底、探针、临时活 | 任意 |

- **「车道不足」只看开发位**：`dev` 少于 8 才报，合并/杂项/缓冲不参与这条提醒。
- 开一条车道＝`spawn_teammate` 之后**立刻** `oversight_bind`；合进 `integration` 后 `oversight_unbind`。

- **车道用 `spawn_teammate` 建，不用 `subagent`**：`subagent` 建出来的会话**无法按 id 寻址**（`send_message` 回
  `active teammate … not found`），只能重新派一个 —— 那就是冷启动。`subagent` 只用于一次性探针与读数活。
- 续做、换底、按审查改都**唤起同一个 teammate**；唤起时第一句让它重读 `camel-lane` skill。
- **合进 `integration` 后解绑**（从表里划掉），再按上一节清工作树与分支。

### 两条禁则（都是“读数正确、对象错”）
- **不许动还没交回的车道的工作树，哪怕它干净。** 一条刚开的车道分支，它的尖端**就是**基线 ⇒
  `git merge-base --is-ancestor` 为 0、`dirty=0`，于是“已合 + 干净”两条都成立，但那条车道正在里面干活。
  **判据是“车道方交回了没有”，不是“树干净不干净”。**
- **`Lane-Reading:` 必须是说明的最后一个 trailer 段落**；夹在正文中间会被 `pre-push` 拒
  （`merge(s) without a Lane-Reading trailer`）。
- **`Lane-Reading` 的内容硬条件（决策者 `D-1127-142` 定，2026-11-27）**：改某模块的枝，
  这一行至少含**三样**：① 该模块自己的计划读数 ② **用到它的 examples** 的读数 ③ `registry` 检查
  （`stdlib-declarations --check` 一类）。缺哪样就在合并说明里写明“未覆盖，因为……”，
  否则合并后红会出现在别人的计划里，而说明上是绿的。

### 换底与退场
- 在跑的车道**每次交回前**换底，被点名落后超过 100 笔时**当轮**换底；**暂停前换一次、恢复后再换一次**。
  换底是 `rebase`，不把 `integration` 合进车道分支。
- **没做完的车道不存档退场**：要停先问协调者。存档只保住内容，停掉的是路线图上还在的事。

## 合并位从任务板取活，不等口头点名

合并队列就是共享任务板上 `status=in_progress`、owner 是合并位、且车道已交回的那些任务。

```
① team_task_list({ status: 'in_progress' }) —— 只看 owner 是自己、且描述里写了 tip 的任务
② 每条任务都写了要合的 tip 与判据；**tip 不等于分支当前尖端就不动手**（车道交回后又提交过 ⇒ 让它重交）
③ 一次 `merge-batch` 把这一批全合上（一笔构建），`--push`；说明里每条车道一行，`Lane-Reading:` 放最后一段
④ 合完对每条任务 `team_task_update(action: 'complete')`，并在报告里给出合并提交、构建读数、新尖端 sha
⑤ 文本冲突的车道：`release` 回 pending（owner 清空）、在描述里点名冲突文件，**不自己解**
```

**`write_scopes` 是各车道自己工作树内的相对路径**，所以不同车道之间“根目录重叠”是正常的；
真正要警觉的是**同一棵工作树里两条任务写同一个文件** —— 出现这种警告，先停手问协调者。
## 每批带新 `.cml` 时：先合、再复录、复检、最后才编

一批里只要有新的 `.cml`（新用例、新探针、或改动了被 AST 记录覆盖的文件），合并之后 `test/plans/feat/parse/golden/ast_hashes.txt` 就会 stale。四步，顺序不能倒：

```
① 合：git merge --no-ff <lane>；golden 文件若冲突，一律取 integration 侧（golden 的 owner 是录制方，不是车道）
② 复录：camel-remote.ps1 -Path . -UpdateAstHashes -TokenClass funnel -NoGpuWatch  ⇒ 输出里必须见到 kept 0 recorded line(s) of the N
        （kept 0 = 整表重算；有 kept 说明你在一个不是尖端的树上录，或者只点了一部分文件）
③ 复检：camel-remote.ps1 -Path . -Check ast-hashes -TokenClass funnel -NoGpuWatch ⇒ ast hashes: N file(s) checked, 0 stale
④ 才编 / 才推：golden 是 test 数据、不是构建输入，所以这一笔本身不要求重编；但**同批的代码改动**仍然要一次构建记录
```

判据的形状（三行都要贴进交回里）：

```
ast hashes: <N> file(s) checked, 0 stale
[camel-remote] ast-hashes exit 0
[provenance] tip <录制的那个 sha> (clean)
```

`-Check ast-hashes` 的四个词分开读，处置完全不同（本晚实测，`bang-suffix` 带进三份新语料时）：

| 词 | 含义 | 处置 |
|---|---|---|
| `mismatch` | 文件在，记录里的 hash 不是它现在的 | 查：内容变了还是记录旧了；查不清就停 |
| `missing` | **记录里没有这一行**（通常是新 `.cml` 语料或新探针） | 直接 `-UpdateAstHashes` 加，不需要 `-AstDrop` |
| `gone` | **文件没了**，记录里还剩一行 | 才要弄清楚为什么没了；确实该删才 `-AstDrop` |
| `stale` | 上面三者之和 | 只看数字会误判成"文件被删了" |

原文形状（三份新语料，一行一条）：`missing  test/cases/opt/cxx_isolation/algebra_bound.cml  recorded -  now 7e1edfc5e433a74a ok`
—— `recorded -` 就是"记录里没有"，不是"文件没了"。合完复录读 `AST snapshot: 4682 lines recorded, 3 added and 0 removed`。

两个坑：

- `-Check ast-hashes` 的退出码可能是 **1**，而 ast 本身是 `exit 0 / 0 stale` —— 那个 1 来自它**连带跑的 `check-doc-links`**。两个检查共用一个退出码，会让"0 stale"和"exit 1"同时出现；看到这种组合先分开读各自的 `exit`，别把 rc 当成 ast 的判词。
- 数字跟着**尖端**走：换个尖端，stale 行数就该变（本晚实测：`1c226f5d99` 上 3 行，`faa0167b1a` 上 10 行，合完 `rule-branch` 后 12 行）。照抄上一轮的差数会漏录。

## 推完的核对是回读远端，不是"我 push 了"

推送成功的判据是**回读**：`git ls-remote origin refs/heads/integration` 给出的 sha 就是你这一批的尖端。
本地 `rev-parse HEAD` 是**你手上的** sha，不是**远端的** sha —— 今晚出现的一次错位（本地有对象、远端没有）
差点让晋级位拿一个只存在于对象库里的提交当起跑点。

```
git push https://github.com/OpenCML/Camel.git HEAD:integration
git ls-remote origin refs/heads/integration   # 这一步是判据；上一行的成功输出不是
```

而且报给协调者的必须是**回读的那个 sha**，连同 `HEAD^{tree}` 与构建记录是否在。单父笔、缺 `Lane-Reading`、
记录落在第二父 —— 这三种拒绝都发生在 `push` 之后、`ls-remote` 之前，只有回读能把它们和"推成功"分开。

**`push` / `fetch` 非 0 时先读末行**（本晚两次"卡住"其实都不是技术问题）：

| 末行长什么样 | 是什么 | 怎么办 |
|---|---|---|
| `schannel: failed to receive handshake, SSL/TLS connection failed` / `unable to access '<url>'` | **网络**（exit 128） | 重试同一条命令；`fetch` 一下再看远端有没有动 |
| `pre-push: refusing to push …` / `husky - pre-push script failed (code 1)` | **钩子的判定** | 读它点名的原因（第一父不对 / 没构建 / 缺 `Lane-Reading` / 走到评论痕迹），**不要重推** |
| `! [rejected] … (non-fast-forward)` | 远端在你这一批之后动过 | `fetch` → 在新尖端**重放 arrival** → 再推 |
| `! [rejected] … (fetch first)` | 同上，但远端 ref 还没取 | 先 `fetch` 再说 |

一句话分界：**`pre-push:`/`husky` 开头才是评价你这批内容，别的都先当网络。**
"卡住"要逐项排到那一行才能定性 —— 换底？构建？逐名？钩子？网络？五项排完再报 ETA，别把网络那一下报成"车道卡了"。

## 合之前先 `fetch`，并在**远端尖端**上重放 arrival（2026-11-27 两次实测）

一晚之内错了两次，两次是同一个错：**手里的树不是远端那个尖端**。

一次是回退之后重跑读数，`origin/integration` 还指着回退**之前**的 sha，`git checkout --detach origin/integration`
拿到的就是那一层 ⇒ 交出去的 "回退后 10 fail / 7 SIGABRT" 其实是**回退前**的读数。这跟"读数不存在"同价。
一次是两笔 golden 建在旧底上，钩子拒得明明白白：

```
pre-push: refusing to push <sha> to refs/heads/integration: the oldest merge in the push has first parent
          <旧尖端>, and refs/heads/integration is <新尖端>.
pre-push: integration was merged into the seat instead of the lanes into integration;
```

两条检查项，做在**动手之前**：

```
1. git fetch https://github.com/OpenCML/Camel.git '+refs/heads/*:refs/remotes/origin/*' --prune
2. git rev-parse --short origin/integration      # 这就是这一批的第一父
   git checkout --detach origin/integration      # 然后在这上面 git merge --no-ff <lane> -F <msg>
3. 被钩子拒了"first parent 不对"，不要重推：把每一枚 arrival 在新尖端上**按原顺序重放**，
   重放后 `git log --first-parent --format='%h %p %s' -4` 逐级核对第一父，再复检 `-Check ast-hashes`、再编。
```

同理，贴任何读数都要带两行：`git rev-parse HEAD`（读的是哪棵树）与 `git ls-remote origin refs/heads/integration`（远端在哪）。
树不对时，读数不是"偏一点"，而是**另一个问题域的读数**。

## 成批合并后、推送前：在该批碰过的计划上跑一遍，与尖端比红名单

**这一步现在由工具做、由钩子查。** `merge-batch` 在编译通过后，用座位自己的 `node scripts/test.js --changed --list`
（基准是这一批起点的 integration 尖端）选出该批碰过的计划，经 `camel-remote.ps1 -Plan … -TokenClass funnel` 在座位上跑，
读出红的用例名，与 `internal/reports/` 里**编号最大**的 `*-promotion-<n>-reds.tsv` 比；有新红就打印（一行一条，带计划）、
不推送、退出码 5，不自己找肇事车道，由合并位判。没有新红就在 `<git 公共目录>/camel-read/<sha>` 写读数记录
（`plans=`、`reds=`、`baseline=`、`new=0`）。推送钩子沿第一父往回走，改了构建输入的区间必须先碰到 `new=0` 的读数记录
才能推；只改文档的区间不要求。**车道自己的读数不能代替这一步**——它是在车道分支上量的，不是在合并后的尖端上量的。
手工做的合并也一样：推之前要有这条记录，补法是对该尖端跑 `merge-batch`（或同样的计划读数），不要绕。
下面的手工步骤只在工具跑不了时才用。

这条规矩来自一次真实的漏失：一批合进来时，我贴的是**该车道自己的读数**加一句 `no new red against <sha>`，
并没有在**集成后**重跑那批碰过的计划 —— 于是那批带进来的 +80 条红，直到下一轮整层快层才被发现。
一句话：**车道上的绿、和集成后的绿，不是同一个绿**；而且**同一个绿，还必须在同一个尖端上** ——
与上一轮快层比、与车道分支比、与另一个工作树里看到的比，都不是这条规矩要的那个比较。

```
a) 在该批碰过的计划上跑一遍：
   git diff --name-only origin/integration...HEAD -- test/** 反查出 plan 目录，只跑这些
b) 与**同一个 integration 尖端**上同一批计划的红名单做差集（不是与上一轮快层比，也不是与车道分支比）
c) 差集非空 ⇒ **停下，报给协调者**，附三样：差集里的用例名、每一条在尖端上的读数、该批碰过的源码
   由协调者判性质：预期变化（该批正确改了语义、期望要按设计原则 706 重钉）还是回归（revert -m 1）
   差集为空 ⇒ 才编、才推
```

跑之前先核**计划路径存在**：把不存在的路径交给 runner，它不报"没有这个计划"，而是**空跑 2.7 s 后 exit 1**
（本晚实测：`-Plan test/plans/feat/docs/ai_reference.plan.toml` 不存在，真名是 `test/plans/feat/docs_ai_reference.plan.toml`）。
看到一次"计划秒返回 + exit 1 + 日志里没有 `[SUMMARY]`"，先 `git ls-files` 核路径，别当成用例红了。
目录型目标同理：`test/plans/feat/docs_links`（目录）与 `test/plans/feat/docs_links.plan.toml`（文件）是两个目标，
给目录而目录里没有 `*.plan.toml` ⇒ `[PLAN ERROR] … holds no *.plan.toml`、exit 2，**一个用例都没跑**。

**指名一个计划文件时，它会跑该文件的全部层**（含 `slow`）—— 所以"尖端上这条红"与"全快层里有这条红"是两件事：

- `docs.snippets_compile` 是 slow 用例，`-Tier fast` 从不跑它；它在指名 `docs_links.plan.toml` 时才出现，
  而它的红是既存的 C7 缺陷（`src/stdlib/onnx.cml:78/:114` 两条 `@example` 的 `UnresolvedReference`）。
- 报账时写清"这条不在快层名单里"，别让下一轮读者以为它"消失了"。反过来，**别拿一次指名运行的名单去改快层基准**。

## 构建/跑测撞上 `state=noslot`：先当陈旧锁，加 `-QueueOnTreeLock`，不要杀 pid

```
[camel-remote] this checkout is busy: another run holds its lock, and this run did not ask to wait for it (-QueueOnTreeLock):
               host=… pid=… since=… run=… cmd=bash /tmp/camel-remote-…/run.sh  (that pid is gone: a process it started may still hold the lock)
[camel-remote] state=noslot: this checkout is held by another run, so this run did not start -- nothing above is a reading.
[camel-remote] merge-seat-1 exit 4 (checkout held by another run; … build 0.0 s)
```

三步：

1. 看钩子报的 `pid`：**`(that pid is gone)`** 就说明持锁进程已经不在了，锁是残留 ⇒ 不是"这台机器跑不动"。
2. 同一条命令加 **`-QueueOnTreeLock`** 重试（本晚实测：加完 `BUILD_EXIT=0 BUILD_SECONDS=15.0`）。
3. **不要**去 kill 那个 pid：它不是你的记录，而且已经消失；真正要修的是"锁没有一个活着的持有者"这件事本身（`task-364` / `lane/checkout-lock`）。

`state=noslot` 时日志里那句 `nothing above is a reading` 很重要：**一次 `noslot` 的退出码不是读数**，别把它记成"构建失败"。

## 合并冲突：取"含得更多"的那版，再把另一版的规则手工补回，并用 `numstat` 自证

本晚一次真实冲突（`lane/runner-diagnostic-file` 与 `lane/dead-citation` 改同一个计划文件）：前者把 13 条 `expect_diagnostic_line`
改成 `expect_diagnostic_file`，后者把 21 条改成 `file` 并加消息断言、但**不含** `zip_with`（因为前者的规则当时还不在尖端）。
两笔都"对"，冲突只是同一行被两种写法都碰过。

```
1. 取"含得更多"的那一版：git show <lane>:<path> > <path>      # 21 条 file + 消息断言
2. 把另一版独有的规则手工补回（这里是两条 zip_with 的 file 钉法）
3. 自证没有夹带：git diff --numstat HEAD^1 HEAD -- <path>   # 相对"含得少"的那版应是它自己的改动量
                 git diff --numstat HEAD^2 HEAD -- <path>   # 相对"含得多"的那版应只剩补回的那几行（这次是 2 2）
4. 冲突解决必须写进合并记录：两笔各自改了什么、为什么取这版、补回了什么、为什么要"合完再跑"
```

第 3 步是防"整文件行尾翻动"（Windows 上 `Set-Content` 会把 LF 写成 CRLF）：`numstat` 若等于全文件行数，说明行尾被改了，
那不是解决冲突，是重写文件 —— 用 `[System.IO.File]::WriteAllText` 或 `git checkout --theirs` 之后再定向 `-replace`。
第 4 步是让"顺序"有据：`dead-citation` 的 5 条 `zip_with` 红只有在 `runner` 的规则落地后才该转绿，
所以合并记录里写的是"**合完再跑那 5 条**"，而不是"枝上已经绿了"。

## 合之前先判：这一笔动的是不是量具（元层）

车道的读数再好，也不代表它的**量具改动**可以进：量具变了，**换底前后两表就不在同一把尺上**，
于是"换底前后都是空红表"这句话本身失效 —— 它证明的是"同一把尺下没变"，而这把尺正在被改。

一条判据：**看它是"加一条专用规则"还是"放宽全局匹配器"**。后者哪怕注释理由正当，也是"测得更少的绿"。
判例（`lane/scalar-derivs` 的 `test/tools/run-tests.mjs`，`DIAGNOSTIC_FILE_SUFFIX`，本晚未合、退回车道）：

| 症状 | 为什么要退 |
|---|---|
| 任何相对 `expect_diagnostic_file` 都走退化路（`endsWith('/'+suffix)`） | 另一个树根下的副本（`vendor/copy/stdlib/math.cml`）也会过 |
| 退化没有白名单（没收窄到 `stdlib/`） | 风险面比需求大得多 |
| 模块级 Map 由跑用例时写入、跨用例累积 | 并行时同进程内的写入顺序不定 |

处置：**元层（`test/tools/**`、`scripts/**`、`internal/tools/**`、hooks）合不进来不是我的判断，是归口** ——
报给协调者/决策者，成卡挂着；我能做的只有"说不合，并给出上面这张表"。
铁律 18 的例外只有"语法、转义、注释、拼错的名字"这类**改法唯一的一行级错误**；给工具加一条比对规则不属于它。

第 (c) 步的处置是"停下报协调者"，**不是"该批不入"**：一批如果正确地改变了语义，它的期望本来就该跟着改，
写成"差集非空就不入"会把这类批永久挡在外面；正确做法是挡在推送前、由协调者判性质，
然后**同批带上重钉一起进**。

## 纯文档区间的放行，不依赖"前面恰好有一笔编过"

`task-332` 用三组空跑钉过（`lane-backend-speed`，tip 为当时的 integration 尖端，先把该尖端的 `camel-built/<sha>` 记录**挪开**、保证区间里零记录）：

- 纯文档合并（只加 `docs/**` 一个文件，`Lane-Reading: docs-only`）⇒ 钩子 `EXIT=0`、无输出，**不要求记录、不重编**；
- 改 `src/compile/**`（构建输入）的合并 ⇒ `EXIT=1`，原文点名该文件并说"no commit from there to the tip was built"；
- 文档 + 源码混合 ⇒ 同拒。

因此 `docs/pitfalls`、`docs/pitfalls2` 那两次能过，不是"恰好 `6bd8b5e281` 有一份记录"，而是规则本来如此：钩子逐提交走第一父区间，**只有"没有记录且碰构建输入"的提交才拒**。

两条前置要知道：

1. 直推 integration 的单父提交会先被更早一道闸拒（"is a lane commit that went straight in … every arrival is a merge"）⇒ 这条判据只能在**合并提交**上测；
2. "哪些路径算构建输入"只有一份清单（`scripts/common.js` 的 `BUILD_INFO_ROOTS` / `BUILD_INFO_ROOT_FILES`），钩子复用它 —— 不要新建第二份。

## 判定"某笔合并被撤掉了没有"：`--is-ancestor` 不是那个判据

撤销是**新增一笔**（`git revert -m 1`），不是改写历史；被撤的合并**仍然是祖先**。所以

```
git merge-base --is-ancestor <被撤的合并> origin/integration   # 撤完仍然 exit 0
```

2026-11-28 01:29 我用这条判"撤销还没落库"，而实际上撤销位已经在 `9c6f270c87` 落库了 —— 读数本身没错（那一刻那笔合并确实还没被撤），但**这条命令永远答不了"撤了吗"**。

要用这三条之一：

```
git log --oneline --first-parent origin/integration | Select-String '^.*Revert merge'
git ls-tree -r --name-only origin/integration -- src/stdlib/onnx      # 撤完 ⇒ 该子树里只剩 8e29d45d1c 有的文件
git diff --stat <被撤合并的父> origin/integration -- <那个子树的路径>   # 撤干净 ⇒ 空
```

判据句：**"撤销的痕迹是一笔新提交，不是祖先关系"**。这也是为什么"撤干净"必须用内容比（子树 IDENTICAL），不能用历史形状比。

## 判断远端一条 run 死活：看远端 run 目录，不看本地镜像日志

远端跑的层，本地 `D:\Temp\*.log` 只是镜像，**stdout 有缓冲**：日志 mtime 可以停十几分钟而 run 正在飞快推进。2026-11-28 01:33–01:45 我按"日志 12 分钟没长"差点判一条晋级层挂住，实际它当时正在跑（`transcript.txt` 的 mtime 跟到了当前分钟、`pass/` 下已有 7276 个用例目录、日志 128KB → 520KB、子进程 etime 00:00）。

判断三样，缺一不可：

```
<run 目录>/transcript.txt 的 mtime           —— 跟到当前分钟 ⇒ 在跑
<run 目录>/pass/ 与 fail/ 下的目录数          —— 增长 ⇒ 在跑
远端 worker 子进程 etime                     —— 00:00 这种"刚起"⇒ 在跑
```

结论句：**"读不到 ≠ 没变化"不允许用来判死，只能用来判"还没读到"**；要判死必须有远端三样一起不动。

## 新红的第一责任人：车道，不是读数（2026-11-28 立，`D-1128-6`）

读层把"缺 AST 记录"读成新红是**对的**，不要为此放宽判据。

- 批次里有车道新增了 `.cml` 用例而没有同步 AST 记录行 ⇒ 读数必红 ⇒ 拒推。处置顺序是**退回那条车道补记录**，不是"先合了再说"。
- 晋级表在两个尖端之间**永远够用**：闸门上线后，除纯文档外新红进不来；`baseline` 取表不必改成"每批先跑 base"。
- 真要临时验记录机制，做法是把"旧表 + 已确认的旧红 + 本批新红"合成一张临时表当基线（本夜验证过：`96 red, 0 not in …`、`new=0` 正常写出），但这只是验证，不是常态。

## 改到内存与生命周期的车道：交回必须带"同一程序连跑多次"的读数（2026-11-28 立，`D-1128-42`）

**这条是合并位的收件条件，不是建议**：凡是改到 **facts、gc、草稿与图的接手、槽位表** 这一类的车道，交回里**必须**带一份"同一程序连跑多次"的读数；**没有这份读数，合并位不收**（退回车道补读数，不是先合了再补）。

**为什么**：`D-1128-42` 之前的一次真事 —— 一条改"版本线接手"的车道交回时贴了一次 `capacity 18/18`，我据此判它修好了；`cap-probe` 后来把同一程序（`mixed n=500`）连跑 42 次：`integration 0/42` 崩、**这条车道的版本线那笔 `28/42` 崩**、尖端 29/42 与 24/42。同一二进制连跑 capacity 三次是 `18/18 → 17/18 → 16/18`。**单次全绿在这类 bug 上不构成证据**，而且它损害的是堆（`malloc_consolidate(): unaligned fastbin chunk detected` / SIGSEGV），不是一条断言。

**读数长什么样**（`cap-probe` 那次是标准形状，照抄）：
```
同一程序 mixed n=500，同一组选项，`expect_exit=-1` 让 runner 打真退出码与原文，连跑 42 次
integration  <sha>      0/42 崩
车道第一笔   <sha>      0/42 崩
肇事那笔     <sha>     28/42 崩
车道尖端     <sha>     29/42、24/42 崩（两次）
崩溃原文：malloc_consolidate(): unaligned fastbin chunk detected (rc=134) / SIGSEGV (rc=139)
规模阈值：n=20/60 各 4 次全绿；n≥200 才出现，n=500 概率最高
capacity 同二进制连跑 3 次：18/18 → 17/18(tf) → 16/18(fvm+tf)
```
**在哪跑**：**工作站上跑，不上构建机**（负责人定过：构建机不跑高负载慢测试；合并闸门只跑快层）。所以这份读数**不由 `merge-batch` 产出**，也不需要进闸门 —— 它是车道交回义务。

**兜底不变**：每日全量闸门里的 gc-stress 那一层照常，它只兜"漏网的"，不能替代车道的连跑读数。

**一条附带的定位纪律**：这类崩溃先上 **ASAN 或 gdb 拿到写越界那一行再改代码**；`-StackTrace` 构建在这条路径上可能压根不出回溯（`cap-probe` 实测 stderr 只有 malloc 那句），凭猜改会和这个 bug 缠一夜。

## 比红有两种口径，别混用（2026-11-28 立，`task-383`/`task-368` 各证一次）

**快层批**（`-Tier fast`，整棵 `test/plans` 或一个目录）⇒ 对 **`newestRedTable()`** 逐名比，这是合并闸门与晋级用的口径。

**单计划文件为目标**（`-Plan test/plans/....plan.toml`）⇒ **会连慢层一起跑**（`agents/test.md`：a plan file named as the target runs in full, whatever `--tier` says），此时对名表比会凭空多出差集。正确做法是**纯净尖端对照**：同一台机、把 `origin/integration` 干净检出（`--detach`、无本枝改动）跑同一目标，**两边红集合完全相同才算"新增红 0"**（`task-383` 的读数就是这么做出来的：枝上 11 红、纯净尖端同 11 红）。

**两条已经踩过的坑**
- `feat/modules/onnx` 里 5 条（`export_loop_for`、`ort_core_slice`、`ort_loop_loaded_weights`、`pass_export_equals_export_model`、`resnetbn_train_step_export`）都是 **`tier = "slow"`** ⇒ 按计划文件跑会看到它们，按快层跑**一条都不会被选中** ⇒ **它们永远不会是晋级的甲**，只能记成"慢层的红"。
- 反过来：把"慢层红"当甲会让合并位退回一条根本没改坏东西的车道；把"名表落后于尖端"当甲会让闸门一直不开。判之前先问一句：**这条红所在条目是哪个 tier、这次选中的是哪个层**。

**名表能看见什么、看不见什么（`D-1128-43` 更正；这一节原先把方向写反了）**
- **看得见**：一条**红而不在表里**的用例 —— 判法就是"红而不在表里即新增"。所以"尖端为绿、不在任何表里、被这枝弄红"这条，**名表照样判成新增**，`merge-batch` 读整个快层时会被它拦下（`task-389` 的 `fillslots.permuted_order_runs` 属于这一类，不需要靠对照才看得见）。
- **看不见**：**在基线表里、后来已经转绿、又被这枝弄红**的用例 —— 名字在表里，会被当成"已知红"放过。**干净尖端对照捞的正是这一类。**
- 所以：**对照只在车道自查与二分时用，不进合并闸门**（每批多跑一整层的代价不值）；闸门仍只读名表，读的是整个快层。
