# 自检什么时候变贵了：成本已逐项归因，没有余额，也没有可比的前后读数

## 一、排除链

这台闸机主机上，`runner.backend_matrix` 那条自检（`test/tools/selftest/run.mjs`）一轮的 CPU 是 **68.57 s**（墙钟 101.4 s），直接不套包装跑是 **62.32 s**（User 43.75 + Sys 18.57）。按层排除：

| 层 | 读数 | 占整轮 |
|---|---|---|
| 外层运行器自身 | `[SLOT]` 显示 `children 72.3 s`、`no-child 0.0 s`、`probes`/`verifications`/`log writes`/`serial`/`empty capacity`/`after the last release`/`loop stalls` **全部 0.0 s** | 0% |
| 153 个包装进程报出的 CPU（`child-cpu.mjs` 读 `cutime+cstime`，即**它们的子进程**） | 3.25 s | 5% |
| 包装器本身（68.57 − 62.32） | 6.25 s | 10% |
| `git` 通道（58 × `status --porcelain` ＋ 58 × `rev-parse HEAD`，各 0.03 s / 0.005 s） | 2.0 s | 3.2% |
| `node` 冷启动（153 × 0.015 s；一个真实包装器 0.03 s ⇒ 153 × = 4.6 s） | 2.3–4.6 s | 4–8% |
| 一条最便宜的叶子（`camel` 跑只 `println` 的程序，三次） | 0.015–0.02 s | — |

**哪些数来自改动过的树**：上表里 `git` 那一行（116 次调用 = 58 轮 × 2 次，2.0 s、3.2%）是在**主机侧临时加了计数行**的树上取的，测完已 `git checkout --` 复原、`git status` 空；它只用于"git 不占大头"这一句排除，不支撑任何别的结论。其余各行取自干净的树。

内层 `feat/consistency` 全量（1278 条，255.7 s）的 `[SLOT]` 原文：

```
children 247.1 s, no-child 8.3 s (pre-spawn 8.2 s, post-exit 0.0 s), probes 0.2 s,
verifications 11.1 s, log writes 0.3 s, serial 0 tests locking 0.0 s, empty capacity 0.4 s,
after the last release 0.1 s, loop stalls 0.1 s
```

`children` 96.6%、`verifications` 4.3%、`pre-spawn` 3.2%，其余 ≤0.4 s —— **每一层都是 `children` 占满**，所以成本在叶子上，不在任何一个运行器字段里。

## 二、事故经过

自检的 liveness 上限被抬过两次：180 s → 240 s（`98a500e4f`，理由写的是"the work it bounds grew fourfold"）→ 400 s（理由：45.8 / 61.7 / 54.5 三次读数取四倍最高值）。作者随后点名：**"the self-test timeout rose twice without the cause found -- find it"**。

查完之后，"the work it bounds grew fourfold" 这句话不成立：**范围 10-24 至今只从 47 处长到 48 处**（`run.mjs` 引用的计划文件数），实跑子运行约 **153** 次。

## 三、结论

**这台闸机主机上，自检的成本 = 153 次子运行 × 约 0.44 s（68 s ÷ 153），逐项可归因，没有余额**；它 10-24 至今的范围只从 47 处长到 48 处。

**上限只能按这台机的读数定**：400 s 是四次最高读数（247 s）之上的第一个界。**要报"变贵了"，必须先有同一台机、同一并发宽度的前后两个读数 —— 今天没有**：2026-10-24 记的 12.41–12.69 s 是**别的主机、`-Jobs 8`** 上量的，与这台机 `--jobs 1` 的 68.57 s 不可比，**因此没有倍率可报**。

作者问的是 "find it"，而答案是：**没有异常可找，只有一套从未按同一把尺子量过的数。**

## 自检现在自己报这一行

`test/tools/selftest/run.mjs` 在打印 `runner self-test ok` 之后多打一行（**只打印，不参与任何判定**，`timeout_ms` 一个都没改）：

```
[calibration] child-runs=32; measured=1.2 s cpu; wall=61.8 s
```

**这一行钉死了两件事**：**自检进程自身只烧 1.2 s CPU，却占 61.8 s 墙钟 —— 它 98% 的时间在等子进程**；**CPU 与墙钟并列之后，"68.57 s 里自检自己占多少"不再需要推测**。

**`child-runs=32` 的界限要说清**：它只数这个文件自己起的子运行（`runRunner` / `runTestCli` 两个出口），而之前数到的 **153 个包装进程是它的孙辈**（嵌套 runner 起的），**两层不能混用** —— 这正是"每个数都要带出处"的又一个例子。

## 我方默认

**默认：不再抬上限，也不把 68.57 s 当成"工作变多"。**

先把读数与判定分开 —— 自检自己报出本机校准行（`[calibration]` 那行，只打印不判定），再按"界由读数推出"那条规则重定上限。理由是这条线的四次数值变动里没有一次带着可比的前后读数，靠人改常数已经被证明是病根本身。
