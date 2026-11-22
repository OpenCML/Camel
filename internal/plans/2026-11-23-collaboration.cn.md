# 协作体系重组：一切内部协作移进 `internal/`，每样东西一个位置、一个主人

负责人 2026-11-23：协作体系整个重构一下。车道信息、协调者手里堆积的工作由协调者维护，但都要落到文档，否则一删就没了、可能就忘了；这些移到 `internal/`，不再占用 `docs/`。

## 一、现在的问题

- 协作文档散在 `internal/` 下，共 988 个文件，混在公开文档的目录里。这与 10-22 定的"内部协作集中在 `internal/`、公开时整体删除"不符。
- 同一类信息有好几处：待做的工作分在 `pending-work.cn.md`、`pending-work-status.cn.md`、`red-baseline.cn.md`、`held-branches.cn.md`、各批任务书和审查长页里；车道状态先后写在报告、审查页、车道表里。
- 决策者派下的事情和协调者接手的事情之间没有对账：一条派工写下之后，看不出是否有人接、接到了哪条车道、做完了没有。

## 二、新结构

```
internal/
  README.md          协作体系怎么运转（本页定稿后改写成它）          决策者
  inbox.md           待接手的派工，滚动                            决策者写；协调者接手一条删一条
  backlog.md         接手的全部工作，一条一行                      协调者
  lanes.md           当前车道                                     协调者
  questions/         问题页，一题一页；答完移到 questions/answered/   施工方提问，决策者答
  plans/             施工计划；阶段完成后移到 plans/done/            决策者
  reports/           读数与报告                                   施工方
  archive/           只读：旧审查长页、旧批次任务书、m2–m5 笔记、旧 pending-work 等
  tools/             现有的内部脚本，不动
```

`internal/` 整个移走，`docs/` 只留公开文档：语言、参考、设计原则、设计稿。

## 三、三份常设文件的格式与流转

**`inbox.md`（决策者 → 协调者）**
- 每条带编号 `D-日期-序号`，写两三行：要做什么、完成的标志、细节在哪个施工计划或报告里。
- 协调者接手时，把这一条从 inbox **删掉**，并在 `backlog.md` 里**以同一编号**加一行，提交说明写"接手 D-…"。inbox 只是中转，东西不会因为删除而丢失。

**`backlog.md`（协调者维护：手上所有的活）**
- 一条一行：`| 编号 | 来源 | 内容（一句话 + 指向） | 车道 | 状态 | 更新时间 | 完成提交 |`。
- 来源有三种：决策者的派工（`D-…`）；协调者自己发现的（`C-日期-序号`，包括红基线、闸门欠账、`known_fail`、搁置的分支）；问题页答复后带出来的跟进事项。
- 状态只有四种：`待做`、`在做`、`卡住（写明原因）`、`完成`。完成的条目必须写上提交哈希，保留一周后删除，历史以 git 为准。
- 现有的 `pending-work.cn.md`、`pending-work-status.cn.md`、`red-baseline.cn.md`、`held-branches.cn.md`，其中还没做完的条目全部转进 backlog，原文件移进 `archive/`。

**`lanes.md`（协调者维护：当前车道）**
- 一条车道一行：`| 分支 | 在做 backlog 的哪几条 | 分支读数 | develop 读数 | 下一笔预计 | 更新时间 |`。
- 开车道、合入、卡住、收车道时，当场改自己那一行；车道收了就删行。
- 每条 `在做` 的 backlog 条目，都要能在某条车道的行里找到；没有车道接的 `在做` 条目属于错账。

## 四、决策者每次检查读什么

- `inbox.md`：有没有条目放了超过一个检查周期还没人接。
- `backlog.md`：`卡住` 的条目；超过两小时没更新的 `在做` 条目；`在做` 却没挂在任何车道上的条目。
- `lanes.md`：在跑的车道数（目标 6 到 8 条）；有没有停滞的行。
- `questions/`：新的问题页。

以上读数都由 `decision-tools/pace.sh` 的末尾几行打印出来。

## 五、搬迁（合并位做，一次完成）

1. `git mv internal/* internal/`，按第二节的结构归位：
   - `questions/`、`plans/`、`reports/` 原样搬过去；
   - `reviews/` 里的旧审查页和已答复的问题页，进 `archive/` 与 `questions/answered/`；
   - 各批任务书、m2 到 m5 笔记、`stdlib/` 笔记、`artifacts/`、`probe-refcell/`，进 `archive/`；
   - `inbox.cn.md`、`lanes.cn.md` 改名为 `inbox.md`、`lanes.md`。
2. 改掉指向 `internal/` 的引用，共 148 个文件，用脚本统一替换。其中 `scripts/check-doc-links.mjs` 的问题页索引、`agents/dispatch.md`、各个检查脚本要一并改。
3. 新建 `backlog.md`，把第三节列出的那几份文件里没做完的条目转进去。
4. 验收：
   - 文档链接检查覆盖 `internal/`，无断链；
   - 本地闸门全绿；
   - `docs/` 下不再有协作文档。
