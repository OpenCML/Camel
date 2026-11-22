# 远程 helper 把 plan 的 stderr 整段丢掉了：这条修只活在本机，够不够？

日期：2026-11-03　状态：**已按"2>&1 原样转发"修在 helper 里（不在版本控制）；要不要搬进仓库待裁**

## 问什么

所有闸门读数都只走一个入口：`D:\Projects\Camel-wt\camel-remote.ps1`。它**不在版本控制里**
（文件头自己写着这一点），所以它的任何行为都没有历史可读、没有 diff 可审。

今天量到：plan 阶段写往 **stderr** 的每一行都不出现在调用方的输出里，而退出码照旧到达。
于是"红了"能看见，"为什么红"看不见——这比假绿更难发现，因为红是真的，只是**没有理由**。

在 host 侧补了 `$runPlan 2>&1`（让 plan 的 stderr 在主机上就并进它自己的 stdout，再随
stdout 出来）。**这一修只存在这一台机器上**：`camel-remote.ps1` 不在版本控制里，
`D:\Projects\Camel-wt\camel-remote.ps1` 是它唯一的副本——换机器、重建工作区、或者有人
"清理一下再写一份"，这条修就没了，而这个洞也重新打开。问题是：**要不要把它变成仓库里的东西？**

## 读数（同一棵树 `01e6eee08`，88 上，`-Tier fast -Jobs 4`）

| 探针 | 直接在 88 上跑 | 经 helper |
| --- | --- | --- |
| `node scripts/test.js test/plans/feat/docs_links …`（改前：扁平目录 → 0 计划） | stdout 4 行（`[tier] fast: 0 plan file(s)…`），stderr 1 行：`[FAIL] this run selected no test at all: 0 plan file(s) were reached and none of their entries is in this layer`，退出 1 | `BUILD_EXIT=0` 后直接 `RUN_EXIT=1`：那 1 行**不见**，也没有 `[SUMMARY]` |
| `node scripts/test.js test/plans/feat/nope.plan.toml …`（拼错的计划路径） | stdout 空，stderr 5 行：`Error: ENOENT … at walkPlans (…run-tests.mjs)` 那段栈，退出 1 | 同上：`BUILD_EXIT=0` / `RUN_EXIT=1`，**0 行**含 `ENOENT` 或 `Error:` |
| 同一拼错路径，经**修好**的 helper | — | 那 5 行原样到达（`Error: ENOENT … at walkPlans`），`RUN_EXIT=1` 不变 |

第二行是更常见的那一种：一个手误的 `-Plan`，操作者看到的是一个没有一句话的红。第三行说明
`2>&1` 就是那条裂缝的补丁——它不改变退出码，只让理由到达。

## 三条路

| 路 | 代价 |
| --- | --- |
| 1. 就留在 helper 里（当前做法，注释写明为什么不能删） | 零成本；但只要 helper 被重建、被复制到另一台机器、或被"清理一下再写一份"，这条修会无声消失，下一个人重新踩 |
| 2. 把 helper 搬进仓库（`scripts/` 或 `tools/`） | 一次性：约 1200 行 PowerShell 入库，要先回答"它是本机运维脚本还是仓库工具"；换来修法可审、可回滚、别人复制得到 |
| 3. 转发之外再落盘（主机上留 `plan.log` + 一行指针） | 多一份可事后读的证据，能回答"那次红到底打了什么"；代价是 88 上的日志要有人清（`-Clean` 时要一起清），否则又是几十 GB |

## 默认

**走路 1，并把洞写进 helper 自己的注释**（已做：`$runPlan 2>&1` 上方四行，写明这是实测出来的、
不能当成风格删掉）。理由：`-Check` 的每个 checker 已经逐字打印，丢的只是 plan 那一半，转发就够；
落盘没有读者。若有人**第二次**踩到同一处，那就是走路 2 的信号——那时该搬仓库，而不是再改一次
`2>&1`。