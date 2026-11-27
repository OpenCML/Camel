# 树锁的三层根因：fd 漏进后代、判据只看 pid、输出随本地包装消失

**工单** `task-364`（`D-1127-272 ①`，含本轮追加的第二、三层）。**车道** `lane/checkout-lock`，从
`origin/integration` 切出并**换底到写这一页时的最新**（`task-333` 的 `-QueueOnTreeLock` 在线上），只改
`internal/tools/camel-remote.ps1` 与它的测试 `internal/tools/camel-remote-orphan.test.ps1`；
**没有动任何计划与用例，也没有动 `internal/backlog.md`**。

三笔提交（按主题，sha 换底后会变）：`camel-remote: a dead run's descendants no longer keep the checkout lock,
and a run's output stays on the host` → `camel-remote: the lock's holder list is the other processes, not the run
that asks` → `reports: …`（本页，三笔 `reports:` 合成一笔的叙述见下）。

`3 files changed, 522 insertions(+), 27 deletions(-)`（换底后与 `origin/integration` 的差集只有这三个文件）。

---

## 0 判据本身，一行

**锁上有活人，立刻拒绝并点名；人走，锁即开。**

在宿主上照 11-27 的形状造一个现场（记录里的 pid 已死、它起的 `sleep 300` 还拿着 fd 6）：

```
record after:  [host=tangshan88 pid=2080295 since=… run=99999-20261128000000 cmd=bash …/run.sh]
holders now:   pid=2080299 sleep 300
```

不加 `-QueueOnTreeLock`：

```
[camel-remote] this checkout is busy: another run holds its lock, and this run did not ask to wait for it
  (-QueueOnTreeLock): host=tangshan88 pid=2080295 since=2026-11-27T16:36:06Z run=99999-20261128000000
  cmd=bash …/run.sh (that pid is gone: a process it started may still hold the lock);
  processes on the lock: 2080299(sleep 300 ) -- the run that took the lock is gone and these are what keep it:
  end them and this checkout frees
[camel-remote] misc exit 4 (checkout held by another run; transfer 3.3 s, wait 13.5 s, build 0.0 s, …)
```

`kill 2080299` 之后，**同一条命令、同样不加** `-QueueOnTreeLock`：

```
[camel-remote] this run's output is kept on the host at /home/zjwei/camel-work/.remote-runs/20261128-003818-2226722.out …
[SUMMARY] total: 10, pass: 10, fail: 0
[camel-remote] misc exit 0 (transfer 4.6 s, wait 0.0 s, build 15.4 s, plan 6.0 s, …)
```

改前这两半都是坏的：拒绝时只说"that pid is gone: a process it started **may** still hold the lock"（没有名字，
没有动作），而只要那个后代活着，后面每一趟都 `exit 4` 或长等 —— 今晚 `task-293` 的五趟 Debug 复跑就是这样
白跑的。§2 给这条的机理与改法，§3/§4 给另两层。

---

## 1 第一层：锁的 fd 6 漏进长寿命后代

**机理**：锁是 `exec 6>>"$tree_lock"` 之后 `flock -n 6` 拿到的，内核在**最后一个持有这个文件描述的进程退出**
时放锁。`exec` 之后启动的一切子进程都继承 fd 6，所以"持有者死了、锁还在"只说明还有后代拿着描述符。
2026-11-27 那一夜（宿主 tangshan88）持锁的正是 run 65756 的 npm 子树，而记录里的 pid 早已不在。

**改法**：`exec 6>>` 之后**能活过一步**的子进程全部以 `6>&-` 启动，锁的寿命就等于那个持有它的 shell：

| 子进程 | 行 | 改动 |
|---|---|---|
| token 汇报子壳（每 10 s 报一次 `HOST_TOKENS`/`HOST_ALIVE`，整趟都在） | `camel-remote.ps1:3244` | `) 6>&- &` |
| 构建（`setsid bash -c … host-tokens.sh take-up-to … npm run build`） | `:3295` | 行尾 `… "$build_progress" 6>&- &` |
| 计划（`camel-run plan … -- bash plan.sh`，以及 `--ListKnown` 那条 `tee` 分支） | `:2102`、`:2110` | `( RUNPLAN 2>&1 ) 6>&-` |
| AST 录制（`python3 …/check.py snapshot`） | `:2188` | `… --update… 2>&1 6>&-` |
| `editors/vscode` 依赖安装（`npm ci`） | `:2143` | `… ) 6>&- || echo …` |

**现场读数**（第 3 趟运行 `misc`，`scratch=/tmp/camel-remote-misc-72036-20261128003438`，构建进行中，从另一条 ssh
扫 `/proc/*/fd`，命令按 cmdline 里带本 checkout 路径筛）：

```
MINE pid=2008603 lock=yes bash /tmp/camel-remote-misc-72036-20261128003438/run.sh
MINE pid=2008612 lock=no  bash /tmp/camel-remote-misc-72036-20261128003438/run.sh
MINE pid=2008915 lock=no  bash /tmp/camel-remote-misc-72036-20261128003438/run.sh
```

同一时刻**别的车道**（仍是旧工具起的）树里，形状完全相同的那几种子进程是 `lock=yes`：

```
HOLDER pid=1552246 cmd=bash -c bash "$1/internal/tools/host-tokens.sh" take-up-to "$2" -- npm run build
HOLDER pid=1966812 cmd=tee /tmp/tmp.XvmvZevfKo
HOLDER pid=1565995 cmd=node scripts/test.js test/plans --jobs 16 --tier fast
HOLDER pid=1544023 cmd=bash /tmp/camel-remote-onnx-run-80860-…/plan.sh
```

一对比就是判据：**新工具的 run.sh 之外一个都不持锁；旧工具的 npm / tee / plan.sh / test.js 全在持锁。**

## 2 第二层：判据只看 pid

`flock` 失败时原话是 `(that pid is gone: a process it started may still hold the lock)` —— 说了可能，没说是谁，
读者无事可做。现在从 `/proc/*/fd` 读出**真正拿着这个锁文件的进程**，按 `pid(命令)` 列出；记录里的 pid 已死、
而锁上仍有人时，直接给一句可执行的话。函数在 `:2886`（`tree_lock_holders`）与 `:2910`（`tree_busy_note`），
三条等待/拒绝消息都换成它；同时新增 `HOST_LOCK_HOLDERS=` 供本地侧原样再打一遍（`:2928`、`:2939`、本地解析 `:3674`）。
扫描时把**提问者自己的进程树**排除掉（它刚 `exec 6>>` 过，也在名单里；用 guard 已有的 `orphan_descendants`）。

**造现场的读数**（在宿主上伪造"持有者已死、后代还拿着 fd 6"，正是 11-27 的形状）：

```
record before: [host=tangshan88 pid=2008603 since=2026-11-27T16:34:42Z run=72036-… cmd=bash …/run.sh ]
record after:  [host=tangshan88 pid=2080295 since=2026-11-27T16:36:06Z run=99999-20261128000000 cmd=bash …/run.sh]
holders now:   pid=2080299 sleep 300
```

`-SlotWaitSeconds 15`、**不加** `-QueueOnTreeLock` 跑一趟：

```
[camel-remote] this checkout is busy: another run holds its lock, and this run did not ask to wait for it
  (-QueueOnTreeLock): host=tangshan88 pid=2080295 since=2026-11-27T16:36:06Z run=99999-20261128000000
  cmd=bash …/run.sh (that pid is gone: a process it started may still hold the lock);
  processes on the lock: 2080299(sleep 300 ) -- the run that took the lock is gone and these are what keep it:
  end them and this checkout frees
HOST_LOCK_HOLDERS=2080299(sleep 300 )
[camel-remote] processes on that lock: 2080299(sleep 300 )
[camel-remote] misc exit 4 (checkout held by another run; transfer 3.3 s, wait 13.5 s, build 0.0 s, …)
```

结束那个后代（`kill 2080299`），**同一条命令、同样不加 `-QueueOnTreeLock`**：

```
[camel-remote] this run's output is kept on the host at /home/zjwei/camel-work/.remote-runs/20261128-003818-2226722.out …
[SUMMARY] total: 10, pass: 10, fail: 0
[camel-remote] misc exit 0 (transfer 4.6 s, wait 0.0 s, build 15.4 s, plan 6.0 s, …)
```

`wait 0.0 s` 就是判据的两半：锁上有活人时立刻拒绝并点名；人走锁即开，下一条命令（不加 `-QueueOnTreeLock`）直接进。

## 3 第四层（`task-333` 的后代）：守护只在"run 还在"时才杀

原守护的循环是 `now=$(orphan_ppid "$run") || exit 0` —— **run 的 shell 已经没了就直接退出**，而后代此刻已被
内核收养（`setsid` 起的构建还有自己的会话），父链再也找不到它们。这正是 11-27 "锁还在"的后半段。

现在：每轮把看到的后代写进一个集合（`:2740` `orphan_remember`），结束时结束**并集**——当时的后代加上所有
被记下来的 pid——外加它们各自的**进程组**（`:2750` `orphan_end_pids`，`setsid` 起的构建只有进程组能带走；
跳过守护自己的组，否则它杀不到第二遍）。先 `kill -STOP` 再扫，是因为**停住的进程仍保留父链**。

测试里新增的 E 用例就是这一条：run 自己的 shell 在两轮轮询之间被 `kill -9`，它起的两个子进程（其中一个
在独立会话里）此后是谁的孩子都不是，仍然必须被结束（见 §5）。

## 4 第三层：输出随本地包装一起消失

**先回答"为什么那一趟的目录里没有 `*.out`"**：宿主上**确实有**这个 tee 目标，只是它不在 run 的目录里 ——
guard 的副本落在 `~/camel-work/.remote-runs/<stamp>.out`（run 自己的 scratch 在 `/tmp` 下，跑完会被 `rm -rf`，
这份副本不会）。现场：

```
$ ls -t /home/zjwei/camel-work/.remote-runs/*.out | head -3
/home/zjwei/camel-work/.remote-runs/20261128-003929-2312954.out
/home/zjwei/camel-work/.remote-runs/20261128-003338-1966268.out
…
```

**一个必须知道的边界**：守护只保留**最新 20 对**（`.out`/`.err`）。今晚并排跑的车道有十几个，一趟崩溃后再跑
二十趟，那一份就被剪掉了 —— 这大概是 debug8 那份"找不到"的第二个原因（第一个是找错了目录）。**这一条我没有
改**：上限 20 是资源决策，属于决策者/协调者，不在这张卡里。

**本轮的改动**：run 一开始就把这份副本的**路径**打到输出里（`:2826`），收尾再打一次（本地侧解析 `:3674`，
并写进 run 记录 `hostCopy`），所以人看到的第一行和最后一行都指着它：

```
[camel-remote] this run's output is kept on the host at /home/zjwei/camel-work/.remote-runs/20261128-003442-2008603.out (the .err beside it), also after this connection is gone
…
[camel-remote] this run's output on the host: /home/zjwei/camel-work/.remote-runs/20261128-003442-2008603.out (the .err beside it)
```

顺带修掉一处让"20 对"偶尔变成 21 的竞争：`exec > >(tee …)` 的 `tee` 是异步起的，剪枝按"盘上有什么"数，
于是它可能在数完之后才 `open`。现在先把这一对文件建出来再剪（`:2794` 附近 `: >> "$base.out"`），
`orphan_begin` 里的注释写了原因。

### 判据：本地包装被中途掐断，仍能从宿主取回那一趟的 `[SUMMARY]` 与退出码

做法：跑 `stdlib_faults`，等宿主副本里出现 `RUN_EXIT=`，随即 `Stop-Job` 掐掉本地 pwsh（ssh 随之断）。
本地留下的最后一行是检查器的一行；**宿主副本**（`/home/zjwei/camel-work/.remote-runs/20261128-003856-2259277.out`，
37378 B，mtime `2026-11-28 00:39:17`）里有：

```
357:[SUMMARY] total: 112, pass: 107, fail: 5
…
RUN_EXIT=1
TREE_STATE=clean stage=after-plan
__CAMEL_REMOTE_CHECKS_START__
[camel-remote] --- check check-doc-links: node scripts/check-doc-links.mjs --citations ---
no unverified citation: 1204 of 1204 symbol citation(s) verified, 2663 path citation(s) across 7236 file(s)
[camel-remote] --- check check-doc-links: exit 0 ---
__CAMEL_REMOTE_EXIT__=1
```

取法（不需要工作站那一侧）：`ssh 88zjwei "grep -aE '^\[SUMMARY\]|__CAMEL_REMOTE_EXIT__=' ~/camel-work/.remote-runs/<stamp>.out"`。
同一时刻查宿主：这一趟的进程**全没了**、misc 的锁上**一个持有者都没有** —— 守护把 run 及其后代收干净了（§3）。

## 5 读数

**自身 smoke**（本机，全部 0 failed；**每次换底之后整套重跑**，最后一遍与交回时的树同一份字节：

```
camel-remote-orphan.test.ps1              21 checks, 0 failed, 0 skipped     （改前 16 checks；新增 6 条 fd 静态检查 + 2 条 E 行为检查）
camel-remote-args.test.ps1                36 checks, 0 failed
camel-remote-targets.test.ps1             24 checks, 0 failed
camel-remote-cleanup.test.ps1             64 checks, 0 failed, 42 skipped
camel-remote-cleanup-sweep-cost.test.ps1  21 checks, 0 failed, 18 skipped
camel-remote-fetch.test.ps1               exit 0
```

`camel-remote-orphan.test.ps1` 的新增项：6 条"每个能活过一步的启动点关掉了 fd 6"（读脚本本身，因为另一种验法是让一趟
运行死给你看），2 条 E（run 自己被 `kill -9`、子进程已被收养仍在独立会话里 ⇒ 必须被结束；宿主日志有那一行）。
另外把两条**原来就有的**竞争修成确定的：`tee` 数完之后才建文件（`pairs` 会数出 21），以及一个用例的 `tee` 在下
一个用例清空目录之后才起来（`copy_err` 比到别的 run 的文件上）—— 后者用 `quiesce`（等本 scratch 无进程）解决。

**`-Check`**：每一趟运行的收尾检查器就是它。换底后（tip `7eb3a5a7d5`，`2026-11-28 00:47:41`）单跑一次
`-Check check-doc-links`，读数：

```
[camel-remote] --- check check-doc-links: node scripts/check-doc-links.mjs --citations ---
no unverified citation: 1204 of 1204 symbol citation(s) verified, 2663 path citation(s) across 7238 file(s)
[camel-remote] --- check-doc-links: exit 0 ---
[camel-remote] checks: 1 run, 0 failed (2 s)
[camel-remote] misc exit 0 (transfer 5.1 s, wait 0.0 s, build 15.2 s, plan 6.6 s, load (min 1m) 42.63; total 27 s)
```

（换底前同一读数：`checks: 1 run, 0 failed (4 s)`，`1204 of 1204`、`2663 path citation(s) across 7236 file(s)`。）

**计划读数**（第 3 趟，`-Plan test/plans/feat/modules/stdlib_faults.plan.toml`，宿主机 tangshan88，`2026-11-28 00:34:38` 起）

```
[provenance] tip d25f01eca4… | binary built 2026-11-28T00:39:00+08:00 | tree /home/zjwei/camel-work/misc | tier fast
[SUMMARY] total: 112, pass: 107, fail: 5
[camel-remote] misc exit 1 (transfer 4.5 s, wait 0.0 s, build 15.2 s, plan 9.5 s, load (min 1m) 34.96; total 29 s)
```

`fail: 5` 就是那 5 条 `stdlib.fault.zip_with.{nvm,fvm,jit,prl,opt}` 基线红（这张卡不碰计划）；`wait 0.0 s` 说明
前一趟结束时锁是干净的。宿主负载 30–46（别的车道在跑），**这些是"活着且不回归"的读数，不是性能读数**。

三次运行的账（时间戳是宿主的本地时间，副本在 `~/camel-work/.remote-runs/`）：

| 时间 | 这一趟是什么 | 副本 | 读数 |
|---|---|---|---|
| `2026-11-28 00:34:38` | 第三趟 `stdlib_faults`（锁/输出的常规读数） | `20261128-003442-2008603.out` | `[SUMMARY] total: 112, pass: 107, fail: 5`，`wait 0.0 s` |
| `2026-11-28 00:38:18` | 结束造出来的 `sleep 300` 之后，同一条命令 | `20261128-003818-2226722.out` | `[SUMMARY] total: 10, pass: 10, fail: 0`，`wait 0.0 s`，`exit 0` |
| `2026-11-28 00:38:56` | 本地包装在中途被 `Stop-Job` 掐掉 | `20261128-003856-2259277.out`（37378 B，`[SUMMARY]` 在第 357 行） | `__CAMEL_REMOTE_EXIT__=1`，退出码与 `[SUMMARY]` 都还在 |
| `2026-11-28 00:47:41` | **换底后**（tip `7eb3a5a7d5`）`-Check check-doc-links` 那一趟 | `20261128-004741-2979055.out` | `[SUMMARY] total: 10, pass: 10, fail: 0`，`checks: 1 run, 0 failed (2 s)`，`wait 0.0 s`，`exit 0` |

## 6 未取的现场

**缺什么**：`-Config Debug` 在 `misc` checkout 上的一次完整 Debug 构建，加同类 Debug 计划的复跑读数
（`task-293` 那条"不再 `exit 4 noslot` / 不再长等"）。

**为什么没取**：取它要在宿主 `tangshan88` 上做一次 Debug 构建（这台 checkout 没有 Debug install），而当时
1m load 30–46、并排十几个车道在跑；按协调者的口径"负载高时不并排跑重活"，这一条**主动没跑**，不是漏了。
而且它的失效机理（旧工具的后代拿着 fd 6、记录里的 pid 已死、消息不点名）已在 §1–§4 三处逐一断掉，
再跑一次 Debug 取的是"同一条判据换条路"，不增加判断。

**什么时候补、怎么补**：负载落到 8 以下，或下一班接手时，照 §3/§4 取法跑
`-Config Debug -Plan <同类 Debug 计划> -SlotWaitSeconds 3600`，判据是"出现 `[SUMMARY]` 且**没有**
`exit 4 (checkout held by another run …)`、`wait` 不是分钟级"。

## 7 边界与没做的事

- 只改 `internal/tools/camel-remote.ps1` 和 `internal/tools/camel-remote-orphan.test.ps1`；计划、用例、`src/**` 全没动。
- 分支 `lane/checkout-lock`，**没推**（`HEAD..origin/integration = 0`）。之前在 `lane/dead-citation` 上交回的
  `task-305`/`task-356` 那三个提交没动、没 rebase（它们已交回）。
- `.remote-runs` 的 **20 对上限**没改（资源决策，已上报决策者）：如果一个失败的 run 之后又跑了二十趟，那份副本会
  被剪掉 —— 这是"读不到"的第二个原因，第一个是找错了目录（见 §4）。
- 宿主上有旧工具起的 run 仍在跑（那些 npm / tee / test.js 仍在持锁）；它们结束前，**这条线的修复对它们无效** ——
  修复只对用新工具起的 run 生效。它们不是这条线的 PID 记录，我没有去杀。