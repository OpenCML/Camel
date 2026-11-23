---
name: camel-dev-ops
description: Camel 语言项目（OpenCML/Camel）的日常开发运维背景：谁负责什么、算力在哪台机器上、仓库与工作树位置、派工与合并闸门的固定流程、以及已经踩过并修好的坑。当需要为 Camel 派子代理、合并 handover 分支、在 Linux 主机上构建或跑计划、准备 PyPI 发布、清理工作树、或回答"我们的构建/测试走哪条路"时使用。
whenToUse: 用户提到 Camel 的派工、合并、闸门、发布、88 主机、工作树清理、面板、handover 分支、camel-remote.ps1、fsh-dev 同步时使用；或需要在多代理协作下判断"这项工作该在哪台机器、哪个目录、哪条命令做"时使用。
---

# Camel 开发运维

**这个文件**在仓库里**（2026-11-23 移入）。它以前是本机私有背景、不进仓库，而那正是它该被移进来的理由：一份只活在某台机器上的流程，别人看不见，于是同一个问题会被问第二次、同一个坑会被踩第二次。Camel 的公共规则在 `AGENTS.md` 与 `agents/*.md`；这里记的是它们没写、而每天都要用到的部分：分工、机器、路径、流程、坑。

**它怎么被用**：这是协调者（多代理协作里那个派工、审、跑闸门、发布的人）的操作说明。它作为 DSH 的 skill 装载时，`C:\Users\<user>\.dsh\skills\camel-dev-ops\` 是从这里的副本链过去的 —— **所以改这里，然后同步过去；不要改副本。** 反过来，副本里若出现了这里没有的内容，那是它该回到这里的一条。

## 流程的唯一事实源在仓库里（2026-11-23 起）

**关于车道、闸门、派工、合并的规矩，唯一的事实源是仓库里的 `agents/dispatch.md`。** 本节以下和本文件其余各节只留操作细节；**任何一条流程如果与 `dispatch.md` 冲突，以 `dispatch.md` 为准，并把这里改成指向它。

- **原因**：这些规矩以前只活在本文件里，作者看不到。于是出现过两次——他问「车道表里为什么没有那两行」「`144→146` 是新增还是挪动」——**答案我这儿有，他那边没有，问题就变成了返工。**
- **协作页搬迁（`13198383b`）**：`docs/handover/` 已整体移到 `internal/`。现在的分工是：
  - `internal/inbox.md` —— **作者派工的唯一入口**。接手一条就删掉那一条，以同一编号转进 `backlog.md`。
  - `internal/backlog.md` —— **协调者手上全部活的唯一清单**（含自己发现的红基线、闸门欠账、`known_fail`、搁置分支）。**不在 backlog 里的活，等于不存在。**
  - `internal/lanes.md` —— **当前车道**，开/合/卡/收当场改自己那一行。
  - `internal/questions/`（一页一问）、`internal/reviews/`、`internal/README.md`（流转与格式）。
- **`docs/handover/` 下任何路径都已失效。** 定时任务、派工提示、插件配置里若还写着它，读到的会是空目录 —— **而空目录会说「没有问题页」，听起来像「没有问题」。**

## 决策者通道（camel-oversight 插件）

决策者在另一台机器上，经 Tailscale 读你这个会话在做什么，并直接给你发消息，不必等你合 `fsh-dev` 才看到派工。插件在 `internal/tools/camel-oversight/`。

- **开新会话的第一件事：调一次 `oversight_claim`。** 进程里有很多会话，插件只认最后一个认领的会话为协调者；不认领，决策者的消息就无处投递。**只有协调者会话认领**，车道子代理和别的会话不要调用它。
- **带 `[decision-maker · D-…]` 前缀的用户消息是决策者的派工**，与 `internal/inbox.md` 里同号的条目同等对待：负责人的话高于它，其余照派工办。消息里通常只有要点，细节以 inbox 那一条为准；inbox 里还没有这个编号，说明 `fsh-dev` 有新提交未合，先整枝合 `fsh-dev` 再读。
- **每条都用 `oversight_ack` 回执，当轮就回**：
  - `taken`：已接手，并照常从 inbox 删掉、以同号记进 `backlog.md`；
  - `declined`：不做，`note` 写理由；是原则或语义问题的，另写一页 `internal/questions/`；
  - `done`：做完了，`note` 写合入的提交。
  - 回执是决策者确认你收到的唯一方式，聊天正文里的回答他看不到。
- **两种送达**：普通消息排在下一轮开头，不打断正在跑的这一轮；`steer` 送达的是急件，在下一步就要处理。
- **你主动找决策者用 `oversight_tell`**，它会立刻推到决策者那边：卡住了、出现了他必须马上看到的读数、某个结果会改变他接下来派什么。回执不用它（用 `oversight_ack`）；原则或语义问题照旧写 `internal/questions/` 的页，再用它告诉他页名。急的才标 `urgent`。
- **决策者能看到的**：你的会话是忙是闲、最近几轮调了哪些工具、怎么结束的、哪些消息还没回执。所以车道表与 backlog 照常维护，不必另外向他汇报状态。

## 0. 铁律（每次被唤醒先读这一节）

**永远不向用户提问。** 不调用 `ask_user_question`，不写"要不要/选哪个/等你确认"，不把待决定项留给用户回复。

- **原因**：用户不在，问一句就停一条线；这条已经浪费过半天。**用户只做两件事：给资源、授权破坏性操作。**
- **系统输出约束**：不许用"让我确认一下 / 请问 / 你希望"结尾；任何不确定，先自己量，量不出来就写成问题页。
- **问句只有一个出口**：`internal/questions/`，**一页一问**，页里必须有三样——问句一句、让它是问句的读数、两三条路各带代价；**并写死一条默认建议**。
- **同一轮就按默认建议开工**，不等回复。作者推翻时只回退依赖那一层。
- **判断"该不该问"的尺子**：能靠读数回答的，一律自己回答；只有"两条路都自洽、代价只有作者能权衡"才算问句。
- **降级通道**：真要向用户要东西（新授权、新算力、新目标），写在**当轮回复正文**里，**不停工、不等待**。

**常驻定时共四条（5 / 10 / 30 / 60 分钟，见 §6），其中 5 分钟那条是唯一「一有动静立刻动手」的通道。**
**§0 的三条自检并进了 10 分钟那条**，不再单独开提醒。

**合并位是一条车道，不是我（2026-11-18 作者纠正）。**
8 格 = 最多 7 格实现 + **1 格合并车道**。我有整晚把自己当合并位，于是节奏变成
「车道报 → 我读 → 我合 → 我再派」——**一次只有一条在流动，7 格永远空着**。
- **我不合并。** 合并由那条合并车道做：它跑六计划、按我点的批次合、推。
- **合并不挡派工。** 一条车道报来，我**立刻派下一条实现**，再把它的分支交给合并车道——**两件事并行，不是先后。**
- 派工本身的模板在 `references/dispatch.md`（见 §9），**不要再手写派工提示**：手写一条几千 token，
  于是「填满 8 格」变成 token 问题，而我拿「前提没定」当理由停在 1/8 四小时。

**合并位不许自己跑长测试，闸门位单独一格（2026-11-18 作者问出来的）。**
根因是**把两种延迟分布差两个数量级的工作塞进同一格**：合并 4 条命令 10 秒，六计划一轮 3–5 分钟
（主机排队时无上限——见过 `1m load 20–47`、`[SLOT] waited 190.8 s`）。塞在一起，短的那件永远等长的。
- **分法：6 实现 + 1 合并位 + 1 闸门位（作者 2026-11-18 定为「6 实现 + 2 车道」）。**
  **按延迟分格，不是按数量分**：6 格给「短而确定」的实现活，2 格给「长而串行」的合并与测量。
  7 实现是虚的——只要有一条是长活（如改 `resources.cpp` + 四类读数，20+ 分钟），第 7 格就是替它顶班。
- **`develop` 只能有一个写入者。** 合并位**全权唯一有权 `git push origin develop`**；
  闸门位**不合不推**，只跑六计划、读全量读数、按名字分类红。
  **两条车道同时 push ⇒ 后推的必被拒（non-fast-forward），然后 rebase 再推——每合一条撞一次，
  而它撞的时刻正是它该前进的时刻。** 今晚能连推 6 次零冲突，正因为只有一个写者。
- **闸门位破线时转成「审计合并」**（`U>20` 或 `maxBehind>100`）：那时**它接管 push，合并位停下**。
  一次只有一条在写，这个不变式不破。
- **合并位禁跑任何 >60 秒的命令。** 真要跑，一律 `run_in_background: true` 拿 `job_id`，
  **继续干别的**，要结果时 `job_output(wait: true)`——否则它的回合被阻塞调用占住，
  **父代理发的消息它收不到、也答不了**，而我在外面按分钟计时催它。
- **顺序：先推，再测。**「已合未推」是最贵的状态；测出来红是**下一条**要修的东西，不是这一条不该推。
- **每一笔带新问题页的合并，合完推完之后跑一次只读 `node scripts/check-doc-links.mjs --questions-index`。**
  它会报 `the pending table is hand-edited: … (N page(s) in the directory, M kept row(s))` 并 **exit 1**。
  **2026-11-19 这一晚抓了两次**（`ssa` 两页、`wide` 一页）：**车道记得写页，却忘记那张表是生成的。**
  规矩三步：① **只读在前**——先看它报什么再决定重生成，因为「表被手改过」这件事本身就是信息；
  ② 重生成（`--write`）之后**必须再跑一次只读**，看到 `lists N row(s) … one row each` 且 exit 0 才算完；
  ③ 合并提交里写一句「索引是重生成的，不是手写的」。
  ⇒ 一般的教训：**任何由生成器写的表，车道提交时都会漏掉它**——而只读模式是唯一能发现它的东西。
- 我看到「工作树停在合并中间」或「`develop` 25 分钟没动」，**都不足以判「卡住」**：前者要同时查
  `git log` 里有没有那个 merge commit（正在解 vs 已解），后者要先问它在跑什么。
  **2026-11-18 我凭这两个信号各误判一次**，对作者说了「合并位卡住了」，而它 30 分钟里合了 6 条推了 6 次。

**分支卫生是一条硬闸，不是建议（2026-11-14 用户定）：**
**未合并的车道分支 > 20 个，或任一未合并分支落后 `origin/develop` > 100 commits ⇒ 立刻停派新分支，转入清理与审计合并。**

- 触发判据看 **commits 数，不是天数**。分支「老」不要紧，**落后多少**才要紧：落后 100+ 的分支，合并时冲突面已经不可控，
  而它自己的 2 笔改动也会因为「基线太旧、跑不出真红」变得没法验证。
- 触发后按这个顺序收拾，**不许把「清理」理解成删分支**：
  1. **先把手上正在合的那条合完并推掉**——已合未推比未合更贵；
  2. 逐条 `git merge origin/develop` 进落后分支，解冲突、**小跑**（它自己的计划 + `-Changed`）、合、推；
  3. **合不进去或语义已被后来的改动取代的，不许硬合**：写一句「哪一笔被什么取代了」，再决定丢弃还是重派。
- 台账脚本（10 分钟与 60 分钟两条都跑它）：

```powershell
$m='D:\Projects\Camel'
$rows=@()
foreach($b in git -C $m for-each-ref --format='%(refname:short)' refs/heads){
  if($b -in @('develop','main','release')){ continue }
  $a=git -C $m rev-list --count "origin/develop..$b" 2>$null
  if($a -eq '0'){ continue }
  $rows += [pscustomobject]@{ b=$b; ahead=[int]$a; behind=[int](git -C $m rev-list --count "$b..origin/develop" 2>$null) }
}
'U=' + $rows.Count + ' maxBehind=' + (($rows | Measure-Object behind -Maximum).Maximum)
$rows | Sort-Object behind -Descending | ForEach-Object { '  {0,-24} ahead={1,-3} behind={2}' -f $_.b,$_.ahead,$_.behind }
```

**不许在这个脚本里加任何前缀过滤（2026-11-16 事故）。** 我在临时命令里加了 `-or $b -notlike 'lane*'`，
于是 `hold/vjpgrad ahead=2 behind=311` **一直在闸门外看不见**：我报了整整一晚的 `maxBehind=42`，
真实值是 `311`，而 `>100` 这条硬闸**早就破了**——破线期间我还在持续派新分支。
**闸门的判据是「有没有未合的内容」，不是「它的名字像不像车道」。** 三条不是 `lane*` 的分支：
`hold/vjpgrad a=2 b=311`、`tmp-saved a=1 b=19`、`wzj-dev a=0`（已合，删掉）。

两个方向都会漏，**必须两边都数**：
- 用 `worktree list` 数 ⇒ 漏「**有 ref 无树**」的（`hold/*`、`tmp-saved` 就没有树）；
- 用 `for-each-ref` 数 ⇒ 漏「**有树无 ref**」的（一条纯测量的车道可以没有任何分支，如 `e9fix` 那条线只有一个 `tmp-e9-pin`）。
**一条车道的产物若是「读数」而不是「提交」，它必须在报告里显式写出「本线不产分支」**，否则它在账上会凭空消失一格。

**本节最容易忘、且每次都付了代价的三条**（其余在 §6）：

- **「读不到」≠「没变化」。** `git fetch` 退出码必须先打出来，管道吞掉 `Permission denied` 之后的一串「无变化」全是盲的。
- **查不到就落到默认值，是今晚最隐蔽的一类缺陷。** 它们全部表现为「读数干净」：`--changed` 读不到基线时静默回落成整层；
  `known_fail` 的词匹配到一句已经不是原因的话；判据用 `git grep` 数词而 member 是文件路径；3518 条里 3272 条是镜像副本。
  **凡「选择/匹配/统计」类代码，都要问一句：失败时它落到哪里？那个默认值会不会刚好看起来是对的？**
- **每合完一条就推一次，不要攒。** 攒着推看起来省事，实际上是让「已合」与「已到达」脱钩，
  而脱钩期间所有基于 `origin/develop` 的读数都在骗人。

## 1. 分工

| 角色 | 谁 | 做什么 |
|---|---|---|
| 协调者 / 审查者 | 我 | 派工、审查、合并、跑闸门、维护任务书、发布。**不空等**：没有 agent 在跑就补派 |
| 施工方 | 子代理，最多 8 个并发 | 在自己的工作树里实现一个 `Hxx` 任务，写中文报告，提交英文信息，**不 push、不碰 develop/main** |
| 语言作者 | 用户 | 设计原则的裁定、未决问题的最终决定 |

- 用户说"加速"就是真的可以并发铺开；额度不紧张，机器是瓶颈。
- 用户的硬约束：**不在本机构建**（会蓝屏）、**编译与测试各一个互斥量**、**只留必要的工作树**、
  **不往 git 历史里拉屎**、**本机的测试规则包装不上传**。
- 报告用中文写，但**不进 `develop` 的树**（见 §5「报告走 git notes」）；提交信息用英文写"这组改动的目的"，
  用 `git commit -F <file>`，不要 `-m` 里塞多段。
- **报告长度的教训（2026-10-22）**：143 个任务往 `internal/reports/` 写了 **153 个文件、33,000 行、
  3.5 MB**——比整个标准库迁移的改动（4,600 行）多七倍，占那两个角色的提交里 **192 个 `docs`/`chore` 提交**。
  检查/验证本身（85 个测试提交）不多，**多的是叙述**。所以：报告给结论、证据、数字、没做的，**上限 60 行**；
  长论证进 `docs/technical/`；看板只留一行。

## 2. 算力

| 机器 | 规格 | 用途 | 限制 |
|---|---|---|---|
| 本机 `D:\Projects` | Windows，28 核 | 编辑、git、派工、面板、发布 | **不构建**：0x50 PAGE_FAULT 蓝屏；`dotnet build` 没有 SDK |
| `ssh 88zjwei` | 32 核 EPYC 7343 / 503 GB / 1.8 T（余 ~530 GB）/ 8×RTX 3090 / Ubuntu 22.04 | 全部构建与测试 | 无免密 sudo；校园网认证门户，**访问不了 GitHub 与 pypi**（要 `ssh -N -R 127.0.0.1:1080 88zjwei` 反代）；CUDA 驱动有、**没有 nvcc** |

主机工具链（都是绝对路径，脚本里已导出）：

```
~/camel-eval/conda/envs/clang/bin/x86_64-conda-linux-gnu-clang++   # clang 23.1.2
~/camel-eval/venv-build/bin/{cmake,ninja,conan}                     # 4.4.3 / 1.13.2 / 2.33.0
~/camel-eval/venv-bench/bin/python                                  # 3.10 + onnx 1.23 + onnxruntime 1.23
node 20.20.2；docker 可用
```

主机的 `~/.cache/pip` 有 25 GB，**不要清**——那是离线安装的唯一来源。

## 3. 位置

| 路径 | 是什么 |
|---|---|
| `D:\Projects\Camel` | 主检出，**`develop` 归它**（2026-10-22 起）；闸门也从这里跑 |
| `D:\Projects\Camel-wt\verify` | 集成树，**游离 HEAD**：合并、解冲突在这里做，推送用 `git push origin HEAD:develop`（helper 拒绝游离 HEAD，所以闸门指向主检出） |
| `D:\Projects\Camel-wt\<任务>` | 每个任务一个工作树，分支 `handover/Hxx-*` |
| `D:\Projects\Camel-wt\camel-remote.ps1` | 远程构建/测试入口（见 §4） |
| `D:\Projects\Camel-wt\camel-build.ps1` / `camel-test.ps1` | 本机串行 helper（互斥量 + 面板状态），备用 |
| `D:\Projects\Camel-wt\watch-fsh-dev.ps1` | 后台守候 fsh-dev，一变就退出并通知我 |
| `D:\Projects\panel-deck` | PanelDeck（.NET）桌面面板，Camel 面板在 `src\PanelDeck\default-panels\camel\panel.html`，运行中的 app 读 `%APPDATA%\panel-deck\panels\camel\panel.html`（改完复制过去，自动重载；`POST http://127.0.0.1:4520/reload` 强制） |
| `88:~/camel-work/Camel.git` | 裸库：`git push` 的中转远端 |
| `88:~/camel-work/<工作树名>` | 每个工作树一个常驻检出，带自己的 `build/` 与 `out/` |

分支：`main`（发布）、`develop`（集成）、`fsh-dev`（作者的文档/设计/任务书）。合并一律 `--no-ff`，
永不 rebase、永不 force-push `main`。

## 4. 构建与测试：只走 `camel-remote.ps1`

```powershell
# 全量闸门（合并前）：smoke + feat 全层
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\verify -Plan smoke,test/plans/feat -Jobs 8

# 单项验证
pwsh -NoProfile -File ... -Path <wt> -Plan test/plans/feat/modules/graph.plan.toml -Jobs 8
pwsh -NoProfile -File ... -Path <wt> -Plan smoke -NoBuild          # 复用已有构建
pwsh -NoProfile -File ... -Path <wt> -Clean                        # 收尾：删掉它在 88 上的检出
```

- 传法：`git push` 分支到裸库 → `git diff HEAD --binary` 补丁 + 未跟踪文件 tar → 一次 `scp -r` →
  主机 `checkout -f -B` 后叠补丁。树干净且已在那个提交上时自动跳过检出。
- 退出码原样带回；面板状态带 `remote: "88zjwei"` 显示为 `88`，本机显示 `本机`。
- **并发用 `-Jobs 8`**：`-Jobs 12` 下 `gpu_preference.no_device.*` 会撞 10 秒超时而假红。
- 禁止在 agent 提示里写 `cmake --build`、`npm run build`、`node scripts/test.js`；不要跑
  stress / progen / GC-stress / PyTorch 重测试。

## 5. 一个任务的完整生命周期

1. **派工前核对**（血泪）：`git log --oneline origin/develop --grep="Hxx"` + 看任务书那行 +
   `reports/Hxx*.md` 是否存在。任务书常年滞后，已有三次按过期任务派空工。
2. **开工作树**：`git worktree add D:\Projects\Camel-wt\<wt> -b handover/Hxx-<slug> origin/develop`。
3. **派子代理**：提示必须自包含——`AGENTS.md`、设计原则、任务原文、主机命令、禁止项、报告要求、
   验收标准、"任务已完成则核实并记录，不要重做"。
4. **审查**：读 diff（不看 agent 的自述）、看报告、看数字；代码风格与设计原则是否被绕过。
5. **合并**：`git merge --no-ff handover/Hxx -m "<这组改动合起来做了什么>"`；冲突按内容解
   （文档：双方章节都留；代码：两边的 case 都留；任务书：取新表）。
6. **生成物折进合并提交**：合并前在主机重录 `ast_hashes.txt` / `docs/stdlib-reference.md`，
   作为合并提交的一部分入库——**不要再单独提交 `chore: regenerate`**（那 16 个提交是纯噪音，
   而且"每个提交都绿"照样成立）。
7. **闸门**：§4 全量；**0 fail 才算绿**，14 个 skip 是正常的。
8. **推送**：`git push origin HEAD:develop`（verify 游离）；主检出 `git pull --ff-only`。
9. **报告走 git notes**：
   ```powershell
   git notes --ref=handover add -F report.md <合并提交>
   git push origin refs/notes/handover:refs/notes/handover
   git fetch origin refs/notes/handover:refs/notes/handover   # 换机器要显式拉
   git notes --ref=handover show <合并提交>
   ```
   派工时**不要求 agent 提交报告文件**，只要它在最终消息里给出全文；我转成 note。看板仍留一行。
10. **收尾**：`git worktree remove --force <wt>`（顺手 `camel-remote.ps1 -Clean`），汇报腾出多少空间。

## 5.5 AST 金标：不要缩窄

`test/plans/feat/parse/golden/ast_hashes.txt` 覆盖**仓库里每个 `.cml`**（2,569 条：测试 1,978、示例 255、
`stdlib` 72、benchmark 45、其余教程等）。有人提议"缩窄到只看 parse 用例"——**不要**，量过：

- 一次声明迁移只改 **1–6 行**（真正在 `stdlib/` 那一段里）；缩窄省的正是这几行；
- 缩窄会丢掉 `stdlib`（迁移对象本身）、`examples`、`benchmarks` 的 AST 覆盖——**这些是用户程序的样子**，
  也正是"某次改动悄悄换了图"最容易藏的地方；
- golden 提交"多"是**提交数**问题，不是体积问题（范围总计 +579/−276 行）。解法是 §5 第 6 步（折进合并提交），
  不是砍覆盖。

真要让 diff 好读，只能按区域拆文件（`stdlib` 一段、用例一段），但那只是排版，收益小。

### 5.6 金标什么时候必须重录：三种情形，第三种最容易漏

推之前先看 `frontend.ast_snapshot` 与 `ast_hashes.txt`，**判据是「这次提交会不会改变编译器读到的 AST」，不是「有没有改 `.cml`」。**

1. **增删 `.cml`** —— 显然。
2. **改了会导致读数变化的期望** —— 例如把一条用例从「跑起来」改成「被拒」，行号一变，快照就动。
3. **一个字节都没改，读数照样变** —— 2026-11-14 实测：`guardid` 把类型层「守卫的身份」从指针改成模块+文本，`bb00e95` 推出去时没带金标，`frontend.ast_snapshot` 红在 **`stdlib/test_support.cml`、`concurrent_calls.cml`、`concurrent_recursion.cml`** 三个**该提交没碰过**的文件上。原因：AST dump 打印的是编译器读到的东西，**类型身份一变，所有用到那个类型的文件都会移动**。

所以任何改动「名字/类型的身份、名字解析、打印」的提交，都要假设金标会动，**重录再推**。这条与「合并迟到比实现迟到代价大」并列：一次漏录会让 `develop` 红一段，而红的那几条与提交内容看起来毫无关系，下一个人会以为是自己弄坏的。

**同一类错误的第三种形态（`2957a4d` 那次）**：合并提交的标题被写成 `placeholder` 也是同一件事——**推之前没有把「这次推出去的到底是什么」读一遍**。

## 6. 环境里的常驻定时

**只有四条，按「多久必须看一眼」分层；每多一条就是把注意力切碎一次。** 2026-11-04 夜里最多时开过 7 条，
其中三条在做同一件事（数 agent、判谁该派、读 §0），而**真正没人做的是一件最要紧的事：`origin/develop`
有没有前进。** 结果是 18 笔已合工作压在本地 2 小时，作者那边的监测先报警。**所以这一节的第一原则是：
定时任务的数量要少，但每一条要覆盖一个「不覆盖就会静默出错」的量。**

| 间隔 | 名字 | 判什么 | 不超阈值时的回话 |
|---|---|---|---|
| **5 min** | 远端到达 + 主检出 | `develop` 前进？`ahead`>6？主检出脏/落后？`fsh-dev` 新字？ | 一行 `<sha> age=… ahead=…` |
| **10 min** | 车道审计 | 车道数 `min(R,L)`；合并位有没有人；**`U` 与 `maxBehind` 是否破线（破线即停派）**；§0 三条自检；不够就补派 | 一行 `R=… L=… U=… maxBehind=…` |
| **30 min** | 例行兜底 | 全量读数、任务书、催办 | 只报变化 |
| **60 min** | 清理巡检 | `A>20` / `O>10` / `Wr>5` / **`U>20` 或 `maxBehind>100`（后者触发必须停派并审计合并）** | 超阈值才说话 |

### 6.1 5 分钟那条（唯一「一有动静立刻动手」的通道）

**一次命令取全，只判三件事，别在这条里扩展到别的职责。**

```powershell
$m='D:\Projects\Camel'; git -C $m fetch origin --prune 2>&1|Out-Null
if ($LASTEXITCODE -ne 0) { 'fetchExit=' + $LASTEXITCODE; exit 0 }   # 读不到就停，别往下判
$dev=(git -C $m rev-parse origin/develop).Trim(); $age=(git -C $m log -1 --format='%cr' origin/develop)
$ahead=(git -C $m rev-list --count origin/develop..merge-train-20261104 2>$null); if (-not $ahead) { $ahead='?' }
'porc=' + ((git -C $m status --porcelain)|Measure-Object).Count + ' head=' + (git -C $m rev-parse --abbrev-ref HEAD)
'dev=' + $dev + ' age=' + $age + ' ahead=' + $ahead
```

- **判 A（develop 到达）**：与 `D:\Projects\Camel-wt\.develop-watch` 比。**`ahead > 6` 或 `age` 超过 25 分钟
  ⇒ 立刻催合并车道推**——**合并车道会把「等其余车道合完再一起推」当成效率，而规矩是每条一落地就推一次。**
- **判 B（主检出）**：脏就存 patch + `restore`；干净但落后就 `merge --ff-only`。**主检出不前进，
  在它上面量的每个读数都是旧的**（2026-11-04 我拿 `688a07164` 的基线报了半晚上，而远端已经 18 笔之后）。
- **判 C（fsh-dev）**：与 `.fsh-dev-watch` 比，变了才动。**注意方向**：`develop..fsh-dev` 的差异可能只是
  我们自己的提交还没进 fsh-dev；**判作者有没有新字看 `log <old>..<new>` 的作者提交，不是 diff 的文件数。**

**三个不可省略的读法**：
1. **`fetch` 的退出码必须先打出来。** `git fetch … | Out-Null` 会把 `Permission denied (publickey)` 吞掉，
   于是一串「无变化」全是盲的。**「读不到」≠「没变化」，这是两条完全不同的读数。**
2. **`ahead` 取不到时写 `?`，不许当 0。** 训练树不在时 `rev-list` 失败，把失败读成 0 就是「一切正常」。
3. **`age` 用 `git log -1 --format=%cr` 拿相对时间**，不要自己算时间戳。

### 6.2 10 分钟那条（只数、只补派）

- `R` = `[running]` 条数；`L` = `worktree list` 里非 `develop` 且未合进 `origin/develop` 的树数；
  **真实车道数 = min(R, L)**。
- **`U` = 未合并车道分支数，`maxBehind` = 其中最落后的 commits 数**（脚本见 §0「分支卫生」）。
  **`U>20` 或 `maxBehind>100` ⇒ 本轮不派任何新分支**，只做清理与审计合并；这是唯一的「有空格也不派」的例外。
- **8 格里最多 7 格实现 + 1 格合并。** **合并迟到比实现迟到代价大**，因为合并一跑就锁会话。
- **§0 的三条自检并在这条里**（问过用户没 / 卡在等回复没 / 这一小时每条线有没有落一笔代码或一次测量），
  **不再单开一条 15 分钟的**。
- **满了就只回 `8/8 满载`，不要多说一个字。空位是唯一的信号。**

### 6.3 不在这四条里做的事

- **跑到一半的闸门、解冲突、推送——一律不在会话里做。** 一次十几分钟，会话在那期间答不了任何事。
  **会话只做判断，不做持有锁的长活。**
- 后台 `watch-fsh-dev.ps1`（每 60 秒 `git ls-remote`，SHA 一变就退出，输出带新提交列表，8 小时封顶）
  是**推送级**的即时通知，与上面四条不重叠：它是「作者一动就有数」，四条是「每 X 分钟自查一次」。

## 6.4 四条定时的实际配置（改规则必须同时改这里和定时本身）

**这一节是四条提醒的「权威副本」。** 规则只写在这里不算落地——**必须同时用 `schedule_update` 改对应的那条**，
否则下一个被唤醒的我读到的还是旧判据。**改完回头看一次：SKILL 与定时里的数字、阈值、回话格式是否一字不差。**

| id | 间隔 | 名字 | 回话 |
|---|---|---|---|
| `schedule-654e707b-c392-4969-a705-f3e37224dc4b` | 300 s | develop 到达与主检出 | 一行 `<sha7> age=… porc=… head=…` |
| `schedule-d453ce11-fb73-47cd-822a-f0343b53d76f` | 600 s | 车道数与补派 | 一行 `R=… L=… U=… maxBehind=…`（满载未破线只回 `8/8 满载`） |
| `schedule-66540517-e3d7-4410-9c03-18737228909e` | 1800 s | 全量读数与任务书 | 只报变化 |
| `schedule-8eaa4cc1-7005-4909-a9a4-c7481758f49e` | 3600 s | 清理巡检 | 超阈值才说话；破 `U`/`maxBehind` 线回 `U=… maxBehind=… 停派，已转审计合并` |

- 60 分钟那条管 **`A`/`O`/`Wr`**（清理）与 **`U`/`maxBehind`**（**停派**）两类闸门——**它们不是同一件事**：
  前者是「删掉已经合进来的东西」，后者是「有活没合进去、不许再开新的」。
- 10 分钟那条也带 `U`/`maxBehind`，因为**破线时当轮就要停派**，等不到 60 分钟。
- 30 分钟那条**不放阈值**，只做读数与催办——阈值集中在 10 与 60，避免同一条规矩在两个地方读出两个答案。

**两个度量缺陷（2026-11-18 清理车道量出来的，改判据不改定义）：**

- **`O` 数进了作者的 `origin/fsh-dev`，所以它不可能到 0。** 那条分支是作者的设计与任务书常驻分支，
  **永远不删**。看到 `O>=1` 且清单里有 `fsh-dev`，**先减掉它再数**：真残留 = `O - 1`。
  同一轮报的另外两条（`origin/ckb-dev` 一年前 `a=1/b=13699`、`origin/handover/h484-flip-audit` 四周前 `a=3/b=9200`）
  是真残留，但**删 origin 分支要另一次授权**，只报清单。
- **`Wr>5` 会被「N 条线在跑」满足，不是被「N 棵废弃树」满足。** 填满 8 格时 `Wr` 必然 >5，
  那条阈值此时**不代表有垃圾**。所以清理车道的正确产出是**逐棵分清「活线」与「垃圾」**，
  不是「把 `Wr` 压到 5」；**`Wr=7` 且七条 `[running]` ⇒ 本轮「零删除」就是正确结果**。
- **第③步（这条线还活着吗）只有协调者看得见**：子代理的 `list_agents` 只返回**它自己的**子代理，
  兄弟线全部不可见。所以清理车道**量不到第③步时必须零删除并要名册**——这个「卡住」是正确行为，
  不是失败。派工时把 `[running]` 名单**直接写在提示里**。
- **`O` 的审计脚本本身会坏，而它坏起来会给出一个正常-looking 的数**（2026-11-18 协调者写的脚本被清理车道查出）：
  `git ls-remote --heads origin` 给出的 ref 是 **`refs/heads/<name>`**，直接喂 `git merge-base --is-ancestor refs/heads/X origin/develop`
  时**它在本地解析不了**，于是每个 head 都返回非 0，`O` 就变成「全部 origin 分支数减掉几条」——
  本轮读出 `O=11`，真值 `O=3`（而那 11 恰好是 origin 上**已合**的分支条数，看起来完全合理）。
  **正确写法是 `origin/<name>`**，或先 `git fetch` 后对 `git for-each-ref refs/remotes/origin` 取短名。
  **判据**：`O` 的清单必须能逐条列出名字 + `ahead`/`behind` + 最后提交日期；**列不出名字的 `O` 不是读数**。
- **PowerShell 变量名大小写不敏感，`$a` 会静默覆盖 `$A`**（2026-11-18 清理车道自查出来）：
  量五个量时用了 `$A`（已合未删的本地分支）与循环里的 `$a`（`ahead` 计数，`ArrayList`），
  于是 `$A` 被覆盖成 **1**，清单印成 `0lane-wide` —— **一个看起来合理的数，配一个看起来合理的清单**。
  改名为 `$aheadN`/`$mergedCount` 后重测为 **5**。
  **判据**：五个量**各用互不相同的、带语义的名字**（`$mergedCount`/`$orphanCount`/`$worktreeCount`/`$unmergedCount`），
  **不许用 `$A`/`$a`/`$O`/`$o` 这种一位变量**；并且**每次量完把清单也印出来** ——
  数对了而清单空，或者清单对而数不对，都说明其中一个被覆盖了。

## 7. 参考文件

- `references/host-88.md` —— 主机细节、排障、离线网络。
- `references/workflows.md` —— 合并闸门、发布到 PyPI、面板部署的具体命令与期望输出。
- `references/pitfalls.md` —— 踩过的坑与正确做法（新坑往里加）。
## 5.7 清工作树：先问「这条线还活着吗」

2026-11-14 事故：清理脚本用「`ahead == 0` 且树干净」当判据，把 `inline4` 的树和分支在**车道正在缩程序时**整个删掉（那时它还没提交，所以两项都满足）。**「没有提交」不等于「没有工作」。**

固定三步，缺一步不许 `worktree remove`：

1. `ahead == 0`（无未合提交）；
2. `git status --porcelain` 为空（干净）；
3. **该树对应的车道当前不在 `list_agents` 里 `running`** —— 这一条是新增的，前两条今天证明不够。

另外：**正在跑的线要自己把可复现物落到树外**（`D:\Temp\` 或主机 `/tmp`，并在报告里写明路径），因为树随时可能被重建成更早的 sha，主机上的 `~/camel-work/<lane>` 也可能一起消失。

## 5.8 写文件一律绝对路径：相对路径落在主检出

2026-11-14 事故：一条实现线用 `[System.IO.File]::WriteAllText` 加**相对路径**写文件，.NET 按**进程 CWD**（= 主检出 `D:\Projects\Camel`）解析，于是三个半成品文件落进了 `develop` 的工作区。后果不是「文件放错地方」，而是：

- `camel-remote.ps1` 传的是**工作区**，脏文件会被一起构建 ⇒ **合并车道那一次门禁量的是别人的半成品**；
- 门禁的含义是「这个提交被量过」，掺进别人的脏文件后这句话就不成立；
- 半成品彼此矛盾时（`NodeKind` 里还没有那个名字）会直接把构建打断。

规矩：
0. **这条在 2026-11-16 晚上犯了三次**（`lane-ssas1` 的 `builder_static.cpp`、`lane-refleak` 的 `exec_plan.cpp` 124 行、
   `lane-refleak` 的 `ref.plan.toml`）。三次的共同成因**不是**脚本自己 `cd`、也不是 git 少带 `-C`，也不是「它忘了用绝对路径」，
   而是：**`subagent` 不传 `cwd` 时，子代理会话的 CWD 就是主检出 `D:\Projects\Camel`**，
   于是凡写相对路径（`src/...`、`test/plans/...`）就落在主检出里。**而主检出此刻正被合并车道拿去跑闸门。**
   **派工时必须传 `cwd: "D:\Projects\Camel-wt\<lane>"`** —— 这是根治，不是提醒。
   判据不是「我确认过了」，是**两行输出**：`git -C <你的树> status --porcelain` 里能看到你的改动、
   `git -C D:\Projects\Camel status --porcelain` 为空。
1. **改任何文件用 `edit`/`write` 工具 + 绝对路径**，不用 .NET 静态方法、不靠 shell 的 CWD。
   **机制（`lane-refleak` 2026-11-16 认定，比我原先「相对路径」的说法准）**：
   `cd <树>;` 只改 PowerShell 的 location，**不改进程 CWD**。`[System.IO.File]::ReadAllText/WriteAllText`
   这类 .NET 调用按**进程 CWD**（= 会话工作目录 = 主检出）解析相对路径；`edit` 工具的路径由 FS 后端按会话工作目录解析，**坑一样**。
   后果不只是「写错地方」：**读也是读错的**——它读到的是主检出的旧内容，于是出现「改了却没变化」。
   **所以：路径一律绝对，.NET 调用也一样。**
2. **每次改文件前 `git rev-parse --show-toplevel`**，应是自己的 `Camel-wt/<lane>`。
3. **主检出只许合并车道写**。合并车道在每次门禁前先 `git rev-parse --show-toplevel` 与 `git status --porcelain`；除 `internal/tools/camel-remote.ps1` 之外只要有任何一条脏文件，**先停下报协调者，不要开始构建**。

## 9. 派工提示：模板在 `references/dispatch.md`，不要再手写

**2026-11-18 教训**：我每个子代理都手写一段派工提示（基线、判据、红线、报告格式、主机命令），一条几千 token。
于是「填满 8 格」变成了 **token 问题而不是能力问题**——我停在 1/8 四小时，用「前提没定」当理由，
实际是写不出第四条好提示。**作者一句话点破：「把派工提示写进 SKILL，直接告诉子代理去读 skill 不完了」。**

**从那以后派工正文只需三样，其余让子代理自己读 `references/dispatch.md`：**

```
① 基线 sha（`origin/develop` 的七位短 sha）
② 这条车道要判的**那一件**事，以及它是 §5 的哪一**类**判据
③ 红线里要排除的具体文件 + 哪条车道正在改哪一段
```

**派工正文照抄 `references/dispatch.md` §6**，只在 `<...>` 处填。**一条派工从此是 8 行，不是 80 行。**

**`subagent` 必须传 `cwd: "D:\Projects\Camel-wt\<lane>"`**（§5.8：不传则子代理 CWD 是主检出）。

**这一页的维护责任在我**：某条车道踩了一个新坑，就把那一条加进 `dispatch.md` 对应小节，
下一次派工自动带上。已经加进去的五条见它 §7。
