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
- 默认整层命令用 `camel-remote`；`summary.json` 里有每个计划的 `plan_path` 与每行 `test_timings`（含 `timed_out`）。
- **超时只由运行器重跑一次**（单独、两倍时限）；晋级只读它写在 `test_timings` 行上的最终状态，不再自己重跑。层命令不是以 0 或 1 退出、或读数缺了上次读数里的用例，整个任务判失败、不发布。
- 第一次真跑要在构建机上核五件：`-FetchFile summary.json -Latest` 能否取到工作树、远端清理 `-Clean` 是否合适、`plan_path` 能否直接喂 `-Plan`、`--publish` 时钩子是否放行。

## 怎么合：一条命令

成批乐观合并（`internal/plans/2026-11-26-optimistic-merge.cn.md`）只有一个工具，`scripts/merge-batch.mjs`。
座位的工作树要干净；车道分支要在座位里能解析（先 `git fetch`）。每条车道给它交回时的那一行读数：

```
node scripts/merge-batch.mjs --seat <座位工作树> --lane lane/a="<读数一行>" --lane lane/b="<读数一行>"
node scripts/merge-batch.mjs --seat <座位工作树> --lanes <lanes.json>            # [{"branch": "...", "reading": "..."}]
node scripts/merge-batch.mjs --seat <座位工作树> --lanes <lanes.json> --push     # 编译通过后推 integration
```

它做的事：把座位放到 `origin/integration` 尖端，依次把车道合成合并提交（第一父是合并时的尖端），文本冲突的跳过、
退回车道自己换底；整批做一次增量编译（默认是对座位跑 `internal/tools/camel-remote.ps1`，不带计划，只构建）；
编不过就按合并顺序对前缀二分，找到第一个编不过的前缀，拿掉它的最后一条车道，把后面的在最后一个好前缀上重合，
再编一次，直到编过。合并说明列出车道的提交标题，末尾是 `Lane-Reading: <读数一行>`（只改文档的写
`Lane-Reading: docs-only`），推送钩子逐笔查这一行。不带 `--push` 时停在编译之后，只打印汇总：合了哪些、
跳过哪些和原因、编译几次几秒、总共几秒。

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
  命令行按空格分词、双引号成组，**直接起程序、不经 shell**）。整层命令必须在 `{worktree}/test/results/fetched/summary.json` 留下运行器的汇总文件（默认命令用 `-FetchFile summary.json -Latest` 把构建机上最新一次运行的那份取回到这里）。

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
③ `git worktree remove <路径>`（**不许 `--force`**）→ `git branch -D <分支>`
   —— 这里 `-d` 会拒绝（它不是祖先），所以用 `-D`；依据是第 ①/② 步的读数，不是“我看它像已合”
④ 报：五条各自的标签名、tip、删除前后的 `lane/*` 计数
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