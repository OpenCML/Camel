# `python_binding.session` 的“64x 拷贝成本”护栏：一条宿主相关的读数（H491）

日期 2026-10-25。工作树 `D:\Projects\Camel-wt\h491-bandwidth-flake`，分支 `handover/h491-bandwidth-flake`，
基线 `origin/develop` 1970a695e。本文件只写事实与读数，以及三类处置的代价；**没有改任何断言、`tier`、
`timeout`、`known_fail`**，也不替作者选。

## 1 现象的确切出处

失败来自 `handover/h485-printer-var-read` 的一次全量闸门：revision fd015f555（clean），
2026-10-24T22:34:39Z 开始，targets `smoke` + `feat`，tier fast，jobs 8，218 个 plan。宿主上的运行记录
`/home/zjwei/camel-work/h485-printer-var-read/test/results/log/20261024_183439_3317153/`：
`wall_ms` 300046.5、`avg_concurrency` 7.35/8、`avg_slots_held` 7.69/8、pass 6154 / fail 2 / skip 11 /
xfail 8（另一个红是 `trans.cml` 的 `print_and_recompile`）。

**失败用例自己的墙钟是 2403 ms**（`meta.json` 的 `wall_ms` 2402.99、`timeout_ms` 180000、`timed_out`
false、`exit_code` 1）——300.0 s 是那次**闸门**的墙钟，不是这条用例的；它离自己的 180 s 生还界还有 75 倍。

那一次的读表（该目录 `stdout.txt` 逐字，`pass-through` 一列是绑定的调用，`memmove` 一列是同进程同字节数的拷贝）：

```
size          pass-through (the binding)   memmove (a copy of the same bytes)
    1 MiB           11.3 us                  1074.3 us
    4 MiB           20.5 us                   936.8 us
   16 MiB           12.5 us                  1914.4 us
   64 MiB           14.4 us                  4119.3 us
FAIL 64x the bytes must cost clearly more to copy, or the comparison above proves nothing ...
```

`copying[64 MiB] / copying[1 MiB] = 4119.3 / 1074.3 = 3.83 < 4`。

## 2 用例与 plan 条目

`test/plans/feat/bindings/python_binding.plan.toml`：`tier = "fast"`（第 3 行）、`debug_tier = true`（第 4 行）、
`[defaults]` 里 `timeout_ms = 180000`（第 15 行）、`[[tests]]` 在第 17–27 行。

条目 `python_binding.session`：`kind = "behavior"`，**`tool = "python"`**，case
`test/cases/feat/bindings/python_binding.py`，**`args = ["${case}"]`**——它**没有任何遍**（连一个 pass 词都没有：
它不是 camel CLI 的一次管线运行，而是用 build 出来的解释器直接跑那个 `.py`，`.py` 自己导入 built 的
`out/latest/python/_camel*.so`）；`skip_unless` 探那个扩展模块是否为该解释器构建。

断言：`expect_exit = 0`；`expect_stdout_contains` 13 串（零拷贝地址、memmove 对比行、memory、FaultError 与
DiagnosticError 的位置和码、`python binding: PASS`）；`expect_output_not_contains` 含 `"FAIL"`。
层级：`test/tools/run-tests.mjs` 第 1833–1845 行的 `testTier` 取 test.tier → plan.tier = fast。时间界只有
`timeout_ms` 180 s（生还界）与 runner 的余量规则（第 328 行 `TIMEOUT_MARGIN = 4`；它的判据是**该测试自己的
CPU 时间**，本机 `/proc/self/stat` 可读，所以墙钟不是界）。

## 3 护栏逐字

`test/cases/feat/bindings/python_binding.py`：

- 第 243 行：`sizes = [1 << 20, 4 << 20, 16 << 20, 64 << 20]`；
- 第 255–259 行：`def copy(...): ctypes.memmove(target, source, size)`，
  `pass_through[size] = best_of(call, 20)`，`copying[size] = best_of(copy, 20)`；
- 真正的主张在第 265–269 行：`pass_through[largest] < 2 * copying[smallest]`
  （“handing over 64 MiB must cost less than copying 1 MiB, or the binding moves the bytes”）——它过得很宽
  （11–20 us 对 1074 us）；
- 咬人的那条在第 270–275 行：`copying[largest] > 4 * copying[smallest]`，消息逐字是
  “64x the bytes must cost clearly more to copy, or the comparison above proves nothing (the host's copy bandwidth
  varies, so this guard only proves the sizes were far enough apart for the comparison to mean something; the claim
  itself is the check above)”；
- 取样方式在第 173–182 行 `best_of`：`repeats` 次调用的**均值**，`rounds`（默认 3）轮取最小。

**这条比较里没有一行 Camel 代码**：分子与分母都是本进程的两段 `ctypes.memmove`。它是这条用例里唯一
不测绑定、只测宿主的一段。

## 4 读数（都在 88zjwei 上，`-Jobs 8`）

- 本树单跑 3 次（`-Plan test/plans/feat/bindings/python_binding.plan.toml`）：**PASS 6.78 s / PASS 5.53 s /
  PASS 5.96 s**。三次的 memmove 读数（1 / 4 / 64 MiB，us）与比值：332.1 / 331.6 / 14586.2（43.92）、
  567.9 / 900.1 / 6935.9（12.21）、1015.7 / 881.7 / 10621.7（10.46）。
- 全量 `-Plan smoke,test/plans/feat -Tier fast -Jobs 8` 2 次：两次都 **fail 0**（total 6175、pass 6156、
  skip 11、known failures 8）。第 1 次 wall 219.5 s、concurrency 7.3/8、该用例 **PASS 4.27 s**；
  第 2 次 wall 244.8 s、concurrency 7.32/8、该用例 **PASS 5.68 s**，读数 205.0 / 299.0 / 10229.2（49.90）。
  **没有复现。**
- 宿主上还留着的 23 份该用例读数（20 个其他检出的闸门 + h485 的单跑 + 本树 3 次）：比值 **8.07 … 99.31**，
  唯一的 3.83 就是那次红；**1 MiB 那一格在 23 份里读 159.7–1199.3 us**，没有一次落在“安静机器”的量级。
- 同一台机的负载：`uptime` 在两次测量之间读到 load average **97.3 / 112.1 / 127.5（32 核）**，3–4 倍超订。

## 5 敏感性来自哪里（逐字依据与一致机制）

- 读数与带宽不符，而且**不自洽**：1 MiB 的每字节成本比 64 MiB 还高（1074.3 us / 1 MiB = 0.98 GB/s，
  对 4119.3 us / 64 MiB = 16.2 GB/s），任何存储层次都不会这样；五份完整读表里有**四份**的第 1 MiB 一格 ≥
  第 4 MiB 一格（h485 红 1074.3 > 936.8；h485 单跑 833.3 > 98.4；本树第 1 次 332.1 ≈ 331.6；第 3 次 1015.7 > 881.7）。
- 与负载一致的机制（推断，未单独做实验）：`best_of` 取“20 次调用的均值、3 轮取最小”。1 MiB 的一轮只有
  20 × ~30 us ≈ 0.6 ms 的工作，64 MiB 的一轮是 20 × 4–18 ms ≈ 0.08–0.37 s。在 32 核上跑着 100–127 个可运行
  任务时，一轮里被调度让出一次（一个 CFS 时间片，几毫秒到十几毫秒）会把 1 MiB 的均值抬高约 30 倍，而同一次
  让出在 64 MiB 的一轮里只抬高约 1.2 倍。这个比值因此约等于 `(64 MiB 拷贝 + 让出/20) / (让出/20)`——
  **分母就是宿主的调度延迟**，负载越高越小，阈值 4 正好落在它的波动范围里。
- 结论：这条断言**本质上是宿主相关的**，在满载并发下必然可能失败。而且那次红之前阈值已经被放宽过一次
  （commit b3d8a8f4f 把 `> 10 *` 改成 `> 4 *`，并把“the host's copy bandwidth varies”写进消息）；放宽没有让它
  稳定，因为被放宽的是宿主读数的裕量——那次红的失败文本逐字就是放宽后的版本。

## 6 三类可选处置与代价（不替作者选）

**(a) 把这条比较换成不随宿主调度漂移的量**（用例仍留 fast 层）。候选：读数改成“更多次单次调用里最小的那
一次”（min 不受单次抢占影响）；或把最小尺寸从 1 MiB 抬到与 64 MiB 同量级的另一档；或改比同一字节数下的每字节
成本。代价：这是断言改写，要给出新的、可复核的阈值依据（第 4 节那 23 份分布现在有了）；min-of-N 要论证它与均值
测的是同一件事；若改用更大的尺寸，用例墙钟（现在 4.3–7.5 s）会涨，而 64 MiB 一格自己就在 4.1–18.3 ms 之间漂。
与既有裁定同向：`docs/handover/reviews/questions-2026-10-23.md` 的 declaration-cost-gate (a) 已裁定
“改量 CPU 时间，不量墙钟……共享主机上的墙钟随负载漂移”。

**(b) 把该用例移出 fast 层**（test 级 `tier = "slow"`，或整条 plan 改 slow）。代价：合并闸门从此不再覆盖 H253 的
零拷贝契约（plan 的 `desc` 与 `tags` 写的就是 zero-copy / DLPack），只有 `--tier slow` 或发布前才跑；而且它
**不消除假红**，只把读数挪到另一次运行——那次也在负载下就照样可能低于 4。

**(c) 让 runner 只在独占时跑它**（test 级 `parallel = false`，runner 的 `Slots` 会给它全部槽位）。代价：每次到它
闸门要空等 4–7 s（闸门 219–245 s 的约 2–3%；失败那次闸门的 `serial_exclusive_ms_total` 已是 24.0 s）。关键限制：
它只保证**本次运行内**没有邻居，**不保证宿主空闲**——同一台机上同时跑着别的车道的闸门（h485 那次前后一小时内，
h480/h483/h484/h486/h487/h488/h490 各有自己的 `-Jobs 8` 运行）。所以这一类**可能不足以**消除假红，除非同时规定
闸门限流。

## 7 批次点怎么判它

看到 `python_binding.session` 红，先看红的是哪一条：红在“64x the bytes must cost clearly more to copy”
（纯宿主两段 memmove 之比）就先单跑一次 `-Plan test/plans/feat/bindings/python_binding.plan.toml`（4–7 s），
绿了就是宿主读数、不是回归；只有单跑也红，或者红的是“handing over 64 MiB must cost less than copying 1 MiB”
（第 265–269 行那条才在说绑定真的拷了字节）时，才按真缺陷处理。