# 计数故障归还：12 个调用点 / 5 个后端文件，审查页记 16 —— 差额是 4 行注释，单点该落在哪一处？

**日期**：2026-11-19
**提出者**：车道 O（`lane-facts`，收敛清单第 ⑤ 件「计数的故障归还执行层一处」）
**状态**：待作者裁定（本页写死默认建议，按默认当轮开工；**这一轮的开工就是不合代码**）
**触发**：审查页记「各后端调用 16 处（nodevm 4、fastvm 6、gotovm 2、jit 1、taskflow 3）」，车道 O 数到 12 个调用点。先把口径对齐，再问单点。

## 1. 对账：16 是行命中，12 是调用点

同一棵树（`943d97aee` + 车道 O 的 4 个文件），`git grep -c applyRefEntriesOfOpenLoops`：

```
src/passes/sched/fastvm/fastvm.cpp                 6   = 4 调用 + 2 行注释
src/passes/sched/nodevm/nodevm.cpp                 4   = 3 调用 + 1 行注释
src/passes/sched/taskflow/taskflow.cpp             3   = 2 调用 + 1 行注释
src/passes/sched/fastvm/gotovm.cpp                 2   = 2 调用
src/passes/sched/fastvm/jit/runtime/trampoline.cpp 1   = 1 调用
                                                  --
                                                  16   = 12 调用 + 4 行注释
```

审查页那四个逐文件数字（4 / 6 / 2 / 1 / 3）**与本表的行命中逐个相符**，所以差额就是这 4 行只是点名这个函数的散文：

```
nodevm.cpp:871     // line later (camel/execute/loop_frame.h, `applyRefEntriesOfOpenLoops`).
fastvm.cpp:903     // `applyRefEntriesOfOpenLoops`).
fastvm.cpp:942     // line and the slots that name the value go with it (exec_plan.h, `...`).
taskflow.cpp:1854  // (camel/runtime/exec_plan.h, `applyRefEntriesOfOpenLoops`).
```

**⇒ 「16」是页的措辞问题（按行数），不是设计问题（按调用点）。真正的 N 是 12 / 5 个文件。**
声明在 `include/camel/runtime/exec_plan.h`，定义在 `src/execute/exec_plan.cpp`，两者都不在这 16 行里。

## 2. 形态：12 处都是同一句三行

```
applyRefEntriesOfOpenLoops(frame);   // 归还这个 frame 上还开着的循环所持有的计数
framePool_.release(frame);           // 再释放
```

差别只在外层：nodevm 的 3 处各包一层 `framePool_.foreach(...)` 的 lambda，taskflow 2 处中一处传的是
`callee` 而不是 `frame`，jit 那 1 处传 `newFrame`，gotovm 2 处传 `frame`。**没有一处少还、也没有一处多还。**

## 3. 问句

**「故障展开时的归还」应该只有一处，那一处放在哪里？** 三条路：

```
X（默认） 不改。12 处保持现状：每处两行，语义清楚，故障路径不在热路径上。
Y        放进核心 FramePool::release：所有释放自动归还。
         代价两层：core 反向依赖 runtime 的展开逻辑；且每次 frame 释放都多一次间接调用
         （正常路径每轮都走，不只是故障路径）。
Z        新增一个 execute 层入口（与 loopEnd 同一处，即 camel/execute/loop_frame.h）：
         backends 各留一行 `giveBackOpenLoops(framePool_)` ⇒ 12 个调用点变 5 个。
         代价：仍要改 5 个后端文件，且没有把「12」变成「1」，只是把「12 行两件事」变成「5 行一件事」。
```

## 4. 默认建议（按此当轮开工）

**X：不改，本轮不合代码**，并把口径固定在 §1 的 12 / 5。

理由是 §3 里只有 Z 真正减少调用点，而 Z 减少的是**行数不是位置数**（12→5 仍然每个后端各一处），
同时它要求 5 个后端文件在同一次改动里对齐——那是作者级的调度决定，不是一次「收成一个」。
而 Y 的代价与收益方向相反：把一次故障路径的显式归还，换成正常路径上每次释放的间接调用。

**这一页的价值不是提一个改法，而是把 12 与 16 对齐**：审查页的 16 若被当成「有 16 处要收」，
下一步就会去找 4 处不存在的调用点。**⇒ 建议把审查页那一句改成「12 个调用点 / 5 个后端文件」。**

**若作者要 Z，本页只给第一步**：先在 `loop_frame.h` 加 `giveBackOpenLoops(FramePool &)` 与
`giveBackOpenLoops(Frame *)`，让 nodevm 的 3 处先改（它自己注释里已经写着这一处该在 `loop_frame.h`），
量一遍六个计划，再推其余 4 个后端；`loopEnd` 与它同一个头文件，两者放在一起读才对得上。

## 5. 相关页

- `internal/reviews/questions-2026-10-29.md`「每日审查（2026-11-18）」第 6 条 —— 本页的出处（那一句记的是 16）
- 车道图（`fsh-dev` 分支上的 `2026-11-18-lane-map.cn.md`，本分支不含此文件）车道 O 第 ⑤ 件
- `internal/questions/2026-11-19-the-gate-value-has-five-readings.cn.md` —— 同车道的第 ④ 件

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

选 X，不改；审查页那句改成"12 个调用点 / 5 个后端文件"。依据核心抽象第 13 节："各后端先满足自己的设计目标，能共用的适当共用，不强求"。故障展开时的归还属于各后端的帧管理，执行车道做第 13、14 节时再看要不要合。
