# 实测：单点已经落在 `loop_frame.h`（页的「Z」），12 个调用点现在是 8 处、其中 5 处走同一入口

**测的是哪一页**：`docs/handover/questions/2026-11-19-the-open-loop-give-back-is-12-call-sites.cn.md`
（页问：故障展开时的「归还」应该只有一处，那一处放在哪里？三选一 X/Y/Z。）

**这一页的结论（一句话）**：树上已经按 **Z** 做了 —— 入口是
`include/camel/execute/loop_frame.h:233` 的 `giveBackOpenLoopsOfEveryFrame(FramePool &)`
（同文件 `:241` 还给了单帧的 `releaseWithOpenLoops`），nodevm 3 处、gotovm 2 处已经改走它；
taskflow 2 处与 JIT trampoline 1 处仍直接调 `applyRefEntriesOfOpenLoops`。
**所以页里「12 个调用点 / 5 个后端文件、16 是行命中」那一张对账表，今天已经不成立**；
剩下要作者定的只有「那 3 处直接调用跟不跟着走」。

## 四样东西

| 项 | 值 |
|---|---|
| 总体 | `origin/develop` 的 `src/` 与 `include/` 全树（整词/子串都按 `applyRefEntriesOfOpenLoops` 这一个名字数） |
| 时刻 | 2026-11-19，检出 `c049f90ec`（`lane-ledger` = `origin/develop`） |
| 来源 | `git grep -n` / `git grep -c`，命令行与命中行原样抄在下面 |
| 前提 | 「调用点」= 不以 `//` 开头、真正调用这个函数的那一行；声明（`exec_plan.h:621`）与定义（`exec_plan.cpp:1763`）不算调用点 |

## 1. 按文件数：今天只有三个后端还在叫这个名字

```
$ git grep -c "applyRefEntriesOfOpenLoops"
include/camel/execute/loop_frame.h:3
include/camel/runtime/exec_plan.h:1
src/execute/exec_plan.cpp:1
src/passes/sched/fastvm/jit/runtime/trampoline.cpp:1
src/passes/sched/taskflow/taskflow.cpp:3
```

对照页 §1 的那张表（`fastvm.cpp 6`、`nodevm.cpp 4`、`taskflow.cpp 3`、`gotovm.cpp 2`、
`trampoline.cpp 1`，合计 16 行 = 12 调用 + 4 行注释）：

- `src/passes/sched/fastvm/fastvm.cpp` **0 处**（页记 6）
- `src/passes/sched/nodevm/nodevm.cpp` **0 处**（页记 4）
- `src/passes/sched/fastvm/gotovm.cpp` **0 处**（页记 2）
- `src/passes/sched/taskflow/taskflow.cpp` **3 处**（2 调用 + 1 行注释，与页相同）
- `jit/runtime/trampoline.cpp` **1 处**（1 调用，与页相同）
- 新增的 `include/camel/execute/loop_frame.h` 3 处（2 调用 + 1 行注释）—— 页里没有这一项，因为页写的时候它还不存在

## 2. 按调用点：8 处，其中 5 处经同一个入口

```
$ git grep -n "applyRefEntriesOfOpenLoops"
include/camel/execute/loop_frame.h:235   camel::runtime::applyRefEntriesOfOpenLoops(frame);
include/camel/execute/loop_frame.h:243   camel::runtime::applyRefEntriesOfOpenLoops(frame);
include/camel/runtime/exec_plan.h:621    void applyRefEntriesOfOpenLoops(camel::core::context::Frame *frame) noexcept;   ← 声明
src/execute/exec_plan.cpp:1763           void applyRefEntriesOfOpenLoops(camel::core::context::Frame *frame) noexcept {   ← 定义
src/passes/sched/fastvm/jit/runtime/trampoline.cpp:409   camel::runtime::applyRefEntriesOfOpenLoops(newFrame);
src/passes/sched/taskflow/taskflow.cpp:1855              camel::runtime::applyRefEntriesOfOpenLoops(callee);
src/passes/sched/taskflow/taskflow.cpp:1913              camel::runtime::applyRefEntriesOfOpenLoops(frame);
```

后端的归还点（`git grep -n "giveBackOpenLoopsOfEveryFrame"`）：

```
include/camel/execute/loop_frame.h:233   inline void giveBackOpenLoopsOfEveryFrame(camel::core::context::FramePool &pool) {
src/passes/sched/fastvm/gotovm.cpp:970   camel::execute::giveBackOpenLoopsOfEveryFrame(framePool_);
src/passes/sched/fastvm/gotovm.cpp:1009  camel::execute::giveBackOpenLoopsOfEveryFrame(framePool_);
src/passes/sched/nodevm/nodevm.cpp:872   camel::execute::giveBackOpenLoopsOfEveryFrame(framePool_);
src/passes/sched/nodevm/nodevm.cpp:897   camel::execute::giveBackOpenLoopsOfEveryFrame(framePool_);
src/passes/sched/nodevm/nodevm.cpp:917   camel::execute::giveBackOpenLoopsOfEveryFrame(framePool_);
```

⇒ **8 处归还调用点 / 4 个后端文件**（nodevm 3 走入口、gotovm 2 走入口、taskflow 2 直接、
trampoline 1 直接）。页里那句「12 个调用点 / 5 个后端文件」今天应写成
「8 处 / 4 个文件，其中 5 处经 `loop_frame.h` 一个入口」。

## 3. 落地的两笔，都是 `develop` 的祖先

```
$ git log --oneline -3 -S "giveBackOpenLoopsOfEveryFrame"
52c3b9852 refactor: `loop_frame.h` gives back an abandoned frame's loops, so a fault path calls one entry
a0dcffd76 refactor: fastvm and gotovm hand a frame back through the same entry as nodevm
$ git log -1 --format="%h %ad %s" --date=iso 52c3b9852
52c3b9852 2026-11-19 01:25:15 +0800 refactor: `loop_frame.h` gives back an abandoned frame's loops, so a fault path calls one entry
$ git log -1 --format="%h %ad %s" --date=iso a0dcffd76
a0dcffd76 2026-11-19 02:43:15 +0800 refactor: fastvm and gotovm hand a frame back through the same entry as nodevm
$ git merge-base --is-ancestor 52c3b9852 origin/develop ; git merge-base --is-ancestor a0dcffd76 origin/develop
  -> ancestor of origin/develop: YES（两笔都是）
```

`loop_frame.h:229-232` 的注释把口径也写在了一起（「the twelve fault paths … the review counted
16 lines, of which 12 were call sites」）—— 那是**落点那一笔写下的历史说明**，不是今天的计数。

## 4. 还剩哪一件要作者

**页 §3 的 X/Y/Z 之争已经由树答了 Z**；没答的是这件事的尾巴：**taskflow 的 2 处与 trampoline 的 1 处
要不要也改走 `giveBackOpenLoopsOfEveryFrame` / `releaseWithOpenLoops`**。
这是「5 行一件事」的第 2 批，页 §4 里写着「先 nodevm、量六个计划、再推其余 4 个后端」——
所以它属于作者级调度（要不要现在推），不是「单点落在哪一处」这个已经落地的问题。