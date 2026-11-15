# 计数读数在负载下：`ref.boxes_made_per_call_ten` 的 prl 红，是并行负载造成的，还是计数在并发下不对？

- 日期：2026-11-15
- 车道：`lane-countdet`（工作树 `D:\Projects\Camel-wt\countdet`，基线 `b55283c43`，`origin/develop`）
- 被测二进制：本工作树 `npm run build` 的产物，`out/latest/build-info.json` 记 `b55283c43c (clean)`
- 机器：i7-14700K（20 核 / 28 逻辑核），64 GB；批次进行时机器上另有别的车道在跑，空闲时总 CPU 约 13–40%
- 本车道未改 `src/`、未改 `test/`、未改期望值、未加 `known_fail`

## 一句话结论

**不是并行负载造成的。** 串行的 `--jobs 1` 一次负载都不加，这个用例 20 遍里就红了 5 遍；读数 `created=11` 恒定，变的是 `live`（在 0、3、4、5、6 之间跳），而 `fvm`/`jit` 在同一用例、同一旗标下各 100/100 次读同一个 `11/9/2`，`nvm` 20/20 次读同一个 `11/11/0`。红的原因是 **prl 后端在进程内部多线程调度 + 压测 GC 的停机点时机，让退出时「还没被回收看见的块」这个剩余量随调度变化**；`live` 是依赖调度的读数，按原则 10.5 与作者 2026-11-15 的裁定（`docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md`），这本身就是缺陷，不是噪声。

## 条件

- 计划级：`node scripts/test.js test/plans/feat/compile/ref.plan.toml --jobs N`（47 个用例），红 = 该轮出现 `[FAIL] ref.boxes_made_per_call_ten.<后端>`。
- 用例级（对照组）：直接跑计划里那条命令，旗标逐字相同
  `camel --gc-stress alloc=7,safepoint=3,kind=both --gc-check foreign,verify=both --gc generational <case> <std::nvm|std::fvm|std::jit|std::prl>`。
  列表里的 `${backend}` 在 `test/vars.toml` 里是 `run_prl = ["std::prl"]` 等，直接调用时写的也是 `std::prl`。
- 「同时并发」= 同一轮里同时起 4 个 `camel` 跑同一个用例；「另起负载」= 另起 4 个 `camel` 死循环跑同一用例，整批一直跑着。
- 原始表（每轮一行，含该轮 CPU 读数与全量日志文件路径）：`docs/handover/reports/2026-11-15-counted-boxes-under-load/*.tsv`（与本页同目录，随本页提交）；单轮日志在 `$env:TEMP\countdet\logs\`（脚本自己建的临时目录，不随仓库提交）。计划级红轮的全量输出另有 `test/results/log/<stamp>/fail/...` 一份。

### 表一 计划，仅这一个计划，`--jobs 1`（20 遍）

| 组 | 次数 | 红 | 红轮读数（created/released/live） | 绿轮 |
|---|---|---|---|---|
| `--jobs 1` | 20 | **5**（第 5、7、10、11、14 轮） | `11/6/5` ×4，`11/5/6` ×1 | 15 轮 47/47 |

红的一律只是 `.prl` 那一格，`nvm`/`fvm`/`jit` 从未红。轮内 CPU 读数 13–99%（别的车道同时在跑），最短 4.4 s、最长 8.5 s。

### 表二 同一个计划，`--jobs 4`（20 遍）

| 组 | 次数 | 红 | 红轮读数（created/released/live） | 绿轮 |
|---|---|---|---|---|
| `--jobs 4` | 20 | **6**（第 4、10、11、13、17、20 轮） | `11/5/6` ×4，`11/7/4` ×2 | 14 轮 47/47 |

轮内 CPU 读数 29–90%。红率 30% 与表一的 25% 没有可分辨的差别：**加上测试运行器的并行，红率不升**。

### 表三 `--jobs 4` 的同时，另起 4 个 `camel` 跑同一用例（20 遍）

| 组 | 次数 | 红 | 红轮读数（created/released/live） | 说明 |
|---|---|---|---|---|
| `--jobs 4` + 4 个负载进程 | 20 | **2**（第 6、16 轮） | `11/5/6`、`11/6/5` | 第 17 轮另有 2 个**非计数**红：`ref.boxes_loop_ten.fvm`/`.jit`，原因是 `verification timed out`（CPU 被压到 100%，单轮耗时从 3 s 涨到 16–24 s），不是计数断言 |

红率 10%，比表一、表二还**低**。把 28 个逻辑核压满会让每个用例整体变慢、prl 的那条尾巴在不同时刻收尾，红既没有变得更频繁，也没有消失。

### 表四 对照组：直接调用同一用例、同一旗标（`fvm` / `jit` / `nvm`）

| 后端 | 条件 | 次数 | 红（live>3） | 读数分布（created/released/live） |
|---|---|---|---|---|
| `prl` | 串行 | 20 | 4 | `11/11/0` ×16，`11/7/4` ×2，`11/6/5` ×1，`11/5/6` ×1 |
| `prl` | 4 个同时跑 | 80 | 19 | `11/11/0` ×61，`11/5/6` ×9，`11/6/5` ×7，`11/7/4` ×2，`11/8/3` ×1 |
| `fvm` | 串行 | 20 | **0** | `11/9/2` ×20 |
| `fvm` | 4 个同时跑 | 80 | **0** | `11/9/2` ×80 |
| `jit` | 串行 | 20 | **0** | `11/9/2` ×20 |
| `jit` | 4 个同时跑 | 80 | **0** | `11/9/2` ×80 |
| `nvm` | 串行 | 20 | **0** | `11/11/0` ×20 |

`created` 在四个后端、所有条件下都是 11（thousand 那对是 1001）；只有 prl 的 `live` 会在 0、3、4、5、6 之间跳。fvm/jit 各 100 次、两组合计 200 次读同一个 `11/9/2`，nvm 20 次读同一个 `11/11/0`：**抖的只有 `prl`**。

用例自己的注释说这个程序「2 blocks on fvm, jit and prl, three on nvm」，而基线 nvm 读 0、fvm/jit 读 2：**注释过期，未改，留给修的人** —— 改注释会让下一个人以为行为变了；行为没变，过期的只是那句注释。

### 附加读数（不是被要求的组，用来分清楚抖从哪来）

| 条件 | 次数 | 读数 |
|---|---|---|
| `prl`，去掉 `--gc-stress`，只留 `--gc-check foreign` | 20 | 20 次全 `11/0/11`（`live=11`） |
| `prl`，`ref_boxes_made_per_call_thousand` 同一套压测旗标 | 20 | 20 次全 `1001/1001/0` |

**不压测时 prl 的读数是确定的，但它是 `live=11`**：没有回收跑过，一块都没还回来（这正是 `docs/handover/reviews/2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md` 里量到的「每一块的计数还是到不了零」）。所以 `live <= 3` 这条断言只有在压测 GC 真的跑过时才有机会成立，而压测 GC 在哪停，取决于线程调度。

> **更正（2026-11-15 晚，车道 `lane-prlcount`）：上表那一行与它下面这段话把「不压测时 `live=11`」写成了 prl 的特征。它不是。**
>
> 同一组旗标（只 `--gc-check foreign,verify=both --gc generational`，不带 `--gc-stress`）下，`std::nvm`、
> `std::fvm`、`std::jit`、`std::prl` 四个后端各跑 3 次，**12 次全是
> `created=11 released=0 live=11 disposed=11 finalized=0`**
> （`docs/handover/reports/2026-11-15-prl-counted-boxes-return/D-nostress-all.tsv`）。原读数（prl 20 次全
> `11/0/11`）一个字没错，错的是由它推出的那一句「prl 不归还」——四个后端在没有收集的时候一样一块都不还。
>
> 理由：一次运行里唯一会把 `live` 减一的入口是
> `src/core/rtdata/foreign.cpp#ForeignResourceObject::finalize`，它只由收集器在发现 wrapper 已死时调用；
> `Frame::set` 与 `src/passes/sched/taskflow/taskflow_frame_pool.cpp#TaskflowFramePool::release` 只清槽、不减计数
> （`clearGcSlots`），退出时的 `disposeAllForeignResources` 是 `retain → dispose → release` 净零，也不减 `live`。
> 于是 `live` 是「退出时还没有被任何一次收集看见的死箱子」，`--gc-stress` 只是让收集真的跑起来。
> **所以被放弃的是「prl 不归还」这条推断，不是那一行读数**；取代它的是与后端无关的一句：没有收集就没有归还。
> prl 多出来的只是「哪一次收集落在哪个程序点」由调度决定。
>
> 下面两节的结论不受影响：`live <= 3` 只有在压测 GC 真的跑过时才有机会成立，而 prl 的收集点落在哪取决于调度。
> 详见 `docs/handover/reports/2026-11-15-prl-counted-boxes-return.cn.md`。

### thousand 那对：作者那个问题的正面回答

`ref.boxes_made_per_call_thousand`（同一个程序、范围长一百倍）在 `prl` 上跑了 20 遍，**20 遍全是 `created=1001 released=1001 live=0`**；ten 那一档 `created=11`，`live` 有时 0、有时 4/5/6。所以：

**箱数不随调用次数增长** —— 十次调用与一千次调用留下的 `live` 不是「一百倍」，一千次反而每次都收干净到 0。这一条是稳定、可重复的读数，作者关于「计数是否随调用次数增长」的问题在这一档上已经有了答案。

红的不是这一条。红的是 ten 那一档退出时的那条尾巴（最后几次调用的块，回收还没看见），**它的读数抽签**：同一个二进制、同一个后端、同一份程序，0、3、4、5、6 都读到过。所以「这一档红了」既不能推翻上面那条正面回答，也不能反过来替它作证。

## 判据

**「`--jobs 1` 一次也不红」这条前提不成立。** 20 遍里红 5 遍，读数 `11/6/5` 与 `11/5/6`；这时整个运行器是串行的，一个用例一个进程，没有任何测试级并发。红出现的条件不是测试并发，而是 prl 自己的多工作线程（`src/passes/sched/taskflow/`）与 `--gc-stress alloc=7,safepoint=3,kind=both` 的停机点交错。

**读数能不能依赖调度：不能。** 原则 10.5 要求计数等于持有者的数目；作者 2026-11-15 对「产出处那一个减一由谁发」的答复把这件事钉在**执行计划上的一次「最后使用」分析**上，四个后端只照着执行、不各自推导（那一页的「决策者答复」一节）。按这条，同一个程序在同一个后端上跑两次必须读出同一个数。事实是 prl 读出 0、3、4、5、6 五个值，fvm/jit 读 2，nvm 读 0：**读数是调度与回收时机的函数，不是持有者数目的函数**，是计数没落到计划上的缺陷，不是噪声。

**这条红对作者的问题意味着什么。** 作者加这个用例是量「计数是否随调用次数增长」；它红一次，就说明这一档的读数是抽签抽出来的 —— 一次绿不能替它作证，一次红也不能说清是哪个数目错了（`created`/`disposed` 都对，错的是 `live` 与 `released` 的分配）。

## 能不能重跑结案

**不能。** 理由按测量本身：

1. `--jobs 1`、零附加负载、20 遍红 5 遍 —— 重跑只是在这个分布上再抽一次样本，绿的是 75% 那一侧。
2. 读数本身是多值的（`live` ∈ {0,3,4,5,6}），不是「同一个正确值偶尔被读错」。重跑一万遍也量不出「持有者有几个」。
3. 反过来，加负载也没把它变成稳定红或稳定绿（10% / 25% / 30%），所以「用负载复现」也不是结案的路。

**能结案的条件**（都可量，本车道不改）：`prl` 在 `--jobs 1` 下 20 遍读出同一个 `live`，而且这个数与 `fvm`/`jit` 一致；那时这个用例的断言可以从「`live <= 3` 的上界」收紧成「`live` 等于计划给的持有者数」。在此之前，这条红应当记成已知缺陷（计数依附于回收时机），不能记成偶发噪声。

## 复现

```powershell
$env:CAMEL_HOME = "<工作树>\out\latest"
$env:PATH = "$env:CAMEL_HOME\bin;$env:PATH"
cd <工作树>

# 计划级：串行 20 遍
powershell -NoProfile -File docs\handover\reports\2026-11-15-counted-boxes-under-load\run-plan.ps1 -Tag A-plan-jobs1 -Runs 20 -Jobs 1
# 计划级：并行 20 遍
powershell -NoProfile -File docs\handover\reports\2026-11-15-counted-boxes-under-load\run-plan.ps1 -Tag B-plan-jobs4 -Runs 20 -Jobs 4
# 计划级：并行 + 4 个负载进程
powershell -NoProfile -File docs\handover\reports\2026-11-15-counted-boxes-under-load\run-plan.ps1 -Tag C-plan-jobs4-load4 -Runs 20 -Jobs 4 -Load 4
# 用例级对照组
powershell -NoProfile -File docs\handover\reports\2026-11-15-counted-boxes-under-load\run-case.ps1 -Tag D-prl-serial -Backend "std::prl" -Rounds 20 -Width 1
powershell -NoProfile -File docs\handover\reports\2026-11-15-counted-boxes-under-load\run-case.ps1 -Tag E-prl-width4 -Backend "std::prl" -Rounds 20 -Width 4
```

## 没有回答的

- 为什么只有 prl 抖、fvm/jit 稳：本车道只测量，没有读调度器与回收器的实现去定性，只指出阅读入口是 `src\passes\sched\taskflow\`（任务域与停机点握手）与 `src\core\rtdata\foreign.cpp`（`created`/`released`/`live` 的原子计数）。
- `live` 该等于几：那要等计划上的「最后使用」分析落地；本车道给的只是今天的读数分布，以及「它依赖调度」这一条判据。