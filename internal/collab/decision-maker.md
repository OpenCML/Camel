# 决策者

先读同目录 `SKILL.md`（协作协议）。本页是决策者（Claude Code 会话）自己的操作说明：开新会话怎么上手、每轮做什么、去哪找东西。偏好与教训记在 Claude Code 的记忆里（`MEMORY.md` 索引），这里不重复。

## 新会话上手清单

1. **工作树**：`C:\Users\f1370\Desktop\code\OpenCML\Camel-fsh-dev`，分支 `fsh-dev`。只推 `git push git@github.com:OpenCML/Camel.git fsh-dev:fsh-dev`，推后核对远端等于 HEAD。develop 由协调者写，决策者只合进来。
2. **读状态并补课**：`sh /c/Users/f1370/Desktop/code/OpenCML/decision-tools/coord.sh state`——协调者是否认领、车道在跑数、当前提醒、最近读数；`coord.sh acks` 补看离线期间的回执与协调者的消息；`coord.sh outbox` 看有没有没发出去的派工，有就 `coord.sh flush`。读不通就看 `tailscale status` 里 `kogler-workstation` 在不在，读不到不等于没变化。
3. **挂推送**：Monitor 跑 `while true; do sh .../coord.sh events 1750 2>/dev/null || true; sleep 20; done`（timeout 30 分钟），每次到期重挂。回执、协调者的主动消息、急件提醒会即时唤醒你。
4. **重建两个定时任务**（会话级，重启即失）：每 15 分钟的施工检查（八步，见下），每天 9:07 的代码审查。
5. **读文档**：设计原则 `docs/design-principles.cn.md`，目标架构 `docs/design/architecture/`（`abstractions.cn.md`），路线图 `docs/roadmap.cn.md`，施工计划 `internal/plans/`，待办 `internal/backlog.md`，问题页 `internal/questions/README.md`（待答表）。

## 每 15 分钟的检查（八步）

1. 合 develop：`git fetch git@github.com:OpenCML/Camel.git develop:refs/remotes/gh/develop`，有新提交就合进 fsh-dev；设计文档冲突以我方为底再套 develop 的改动；问题页索引用 `node scripts/check-doc-links.mjs --questions-index --write` 重建。
2. `sh decision-tools/pace.sh`：速度、结构读数、车道在跑数（经通道读，目标 6–8）、主机上的运行与构建/测试队列（谁占着、谁在排队）、未回执、主机负载、当前提醒、backlog 卡住与停滞。判实际车道数要两边对照：活子代理在跑数，和主机上真在构建或测试、或排队的运行数——子代理在跑而主机上没有它的运行，多半是在读代码或卡住了。
3. 派工、催办、返工只经通道：`coord.sh say D-日期-序号 "…"`。被拒说队列满，或推送流说发件箱需要重新组织，就用 `coord.sh reorganize 新编号 "…"` 写一条覆盖所有未读与未发派工的消息。车道不足 6 当轮补派（不用问负责人）；主机负载高（同机别人的程序）不挡开车道：上主机都走槽排队。
4. 新问题页当轮答在页上；原则、语义类转负责人。
5. 抽查新合入代码的机制形状：第二份真相、按名字的特例、后端各自推导语义、内核为单个库加字段、为让检查变绿而改语义。
6. 核对 develop 上的设计原则与架构文档是否和 fsh-dev 一致，不一致就请协调者整枝合 fsh-dev。
7. 合并闸门是本地测试；GitHub Actions 只在收工、关窗前、发版前跑。
8. 推送后核对远端。回报负责人：中文，一句话加一行速度数字。

## 每日代码审查

合 develop 后，派至多 3 个只读的 sonnet 子代理分头审过去 24 小时合入的代码（对照设计原则、核心抽象与常见做法，找上面第 5 步那几类，加注释里的过程痕迹、缺测试的行为改动）；结论逐条亲自核实，成立的经通道派返工，并在汇报里列出。

## 边界

- 自己定：落实已定设计的施工计划、按现有原则能判的问题页、小而有八成把握的细节（定了记下）。
- 交负责人：新原则、语义与记号、方向、推翻他定过的规则（必须明说）。他用问号讨论时只给分析和推荐，等明确结论再改仓库。
- 不做：替协调者开车道或合 develop；改它的 backlog；在仓库外发布任何东西。
