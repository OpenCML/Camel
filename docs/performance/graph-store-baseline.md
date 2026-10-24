# 图存储的性能与内存基线（2026-10-20）

这一页只**测量**，不优化：给图存储重构（[总纲](../design/blueprint/README.cn.md)）留一份当前的数字，重构之后用同一个脚本、同一组程序再跑一次，比较同一台机器上的两次结果。

## 机器与构建

| 项 | 值 |
|---|---|
| 机器 | Windows 11 (AMD64)，Intel64 Family 6 Model 183 Stepping 1（i7-14700K），28 个逻辑核，64 GiB 内存 |
| 工具 | Python 3.12.1；`python3 test/tools/perfbaseline/baseline.py` |
| 构建 | `npm run build`（Release，`out/latest`），提交 `e8abfc30` |
| 测量时间 | 2026-10-20 01:28（本机时区） |
| 负载 | 开发机，测量前后各有其他工作；数字只与同一次调用内的其他行可比 |

## 方法

每个程序，八次独立的 `camel` 进程：

- **编译**：`camel <程序> std::stats std::null`。`std::stats` 打印 `GRAPH_STATS`（图数、节点、边、图字节），`std::null` 消费掉程序所以不执行。记录整个进程的墙钟时间（含进程启动、模块加载、解析、建图）、峰值工作集，以及 `std::stats` 报的节点数与字节数。
- **优化**：`camel <程序> std::opt::inline std::null` 的峰值工作集；时间取 `camel --timeit std::opt::inline/<轮数> <程序> --output-format json` 的 `mean_ms`。`--timeit` 每次迭代都重新编译，计时窗口只包住被计时的那个 pass。
- **执行**：`std::nvm`、`std::fvm`、`std::jit` 各一次完整运行取峰值工作集；时间取 `--timeit std::<后端>/<轮数>` 的 `mean_ms`（同样只包住那个 pass，不含编译）。
- **图字节**：`std::stats` 的 `bytes` 字段，按可达图累加：每个节点一条 `GCNode` 记录（16 字节）+ 每个输入引用（`gc_node_ref_t`）+ 节点载荷（`nodePayloadBytes`，按字对齐）。这是**图存储本身的账**，不是进程堆。字节/节点是它与节点数之商。
- **峰值工作集**：进程结束后读 `GetProcessMemoryInfo` 的 `PeakWorkingSetSize`。句柄在进程启动时打开、结束后才关闭，Windows 因此仍保留进程对象，数字是精确峰值而不是采样。非 Windows 上这一列是 `n/a`。
- 表中轮数为 5；机械同一次运行内的抖动约 ±5%（`--timeit` 的 95% 置信区间另见 `--json`）。

复现：

```powershell
python3 test/tools/perfbaseline/baseline.py --rounds 5 --json tmp/perf-baseline.json
```

一条命令就能跑完并打印表格（`--rounds 3` 是 CI 用的短版）。输入不在检出中的程序（`train.mlp` 的训练产物在 `benchmarks/artifacts/` 下，被 gitignore）在表下记一行"未测量"并跳过，其余照常运行；只有 `camel` 重试后仍失败或没有构建产物才让脚本以非零退出。

## CI 可选作业

`.github/workflows/perf-baseline.yml`（名称 **Performance baseline**）只在手动触发时运行（Run workflow → Performance baseline；`rounds` 默认 3），不随 push、tag 或 PR 触发，因此不进入主流水线。它用与 CI 相同的 Linux x64 工具链构建 Release，然后执行：

```bash
python test/tools/perfbaseline/baseline.py --rounds "$ROUNDS" --markdown perf-baseline.md --json perf-baseline.json
```

表格打印在作业日志里，同时写进该次运行的 Summary，并作为 `perf-baseline` 产物保留 90 天。**没有阈值判断**：数字变化只是这次运行的结果，不会让作业失败；只有构建失败、脚本异常或某个 `camel` 运行三次都失败才会失败（脚本对偶发访问冲突的重试见"已知的测量噪声"）。

Linux 上"峰值"列是 `n/a`（精确峰值工作集用的是 Windows 的 `GetProcessMemoryInfo`）；跨平台只应比较与时间无关的列，例如字节/节点。

程序清单（`test/tools/perfbaseline/baseline.py` 里的 `PROGRAMS`，改清单就是改基线口径）：

| 程序 | 是什么 |
|---|---|
| `fib.deep` | `test/cases/perf/fib_recursive.cml`：深递归 `fib(30)`，图很小、执行很重 |
| `alloc_small` | `test/cases/perf/gc/alloc_small.cml --arg reps=200000`：大量小对象分配 |
| `map_chain` | `test/cases/perf/gc/map_chain.cml --arg n=2000 --arg reps=20`：映射链的构造与回收 |
| `build_tree` | `test/cases/perf/gc/build_tree.cml --arg depth=12 --arg reps=5`：大参数树的构造 |
| `jit_const` | `test/cases/perf/gc/jit_const.cml --arg n=24`：JIT 下的对象常量 |
| `train.mlp` | `benchmarks/mlp/model.cml`（`CAMEL_BENCH_WORKLOAD=train`，一步、一线程、不预热）：真实训练程序，59 个图 |

## 结果

机器：Windows 11 (AMD64)，Intel64 Family 6 Model 183 Stepping 1, GenuineIntel × 28，Python 3.12.1。

| 程序 | 图 | 节点 | 边 | 图字节 | 字节/节点 | 编译 | 执行 nvm | 执行 fvm | 执行 jit | 峰值（编译 / 优化 / nvm / fvm / jit） |
|---|---|---|---|---|---|---|---|---|---|---|
| fib.deep | 5 | 31 | 36 | 808 | 26.1 | 32.2 ms | 219.0 ms | 55.8 ms | 84.7 ms | 11.4 MB / 11.6 MB / 11.6 MB / 11.7 MB / 11.9 MB |
| alloc_small | 7 | 69 | 78 | 1748 | 25.3 | 21.7 ms | 156.0 ms | 106.1 ms | 107.7 ms | 11.5 MB / 11.6 MB / 15.3 MB / 15.4 MB / 15.6 MB |
| map_chain | 11 | 75 | 77 | 1818 | 24.2 | 23.1 ms | 14.8 ms | 10.4 ms | 10.4 ms | 11.5 MB / 11.5 MB / 15.9 MB / 15.6 MB / 15.9 MB |
| build_tree | 11 | 80 | 89 | 1938 | 24.2 | 23.9 ms | 12.8 ms | 6.8 ms | 7.3 ms | 11.4 MB / 11.5 MB / 16.5 MB / 15.3 MB / 15.6 MB |
| jit_const | 5 | 26 | 27 | 598 | 23.0 | 22.9 ms | 10.7 ms | 4.5 ms | 5.5 ms | 11.0 MB / 11.1 MB / 11.1 MB / 11.2 MB / 11.5 MB |
| train.mlp | 59 | 495 | 597 | 12450 | 25.2 | 55.3 ms | 8.8 ms | 8.8 ms | 9.3 ms | 16.3 MB / 16.9 MB / 24.6 MB / 24.9 MB / 25.0 MB |

## 从这些数字能读出的（以及不能读出的）

- **图存储 23–26 字节/节点**，六程序都落在同一区间：节点记录 16 字节 + 输入引用（每边 4 字节）+ 载荷。这是重构要改善的那个数；表格给的是重构前的口径。
- **编译** 21–24 ms 基本是固定开销（进程启动、模块与标准库加载、解析），`train.mlp` 的 55 ms 才包含较多建图；因此“编译时间”在小程序上不是图存储的指标，**字节/节点**才是。
- **峰值工作集**的 11 MB 底噪是运行时与标准库的装载；执行期的 15–25 MB 里，`alloc_small` 的 15.3 MB 与 `train.mlp` 的 24.6 MB 才是程序的分配。峰值为整数 MB 量级，只能看趋势。
- **执行**：`fib.deep` 的 nvm 219 ms 与 fvm 56 ms、jit 85 ms 的差距是调度器的执行模型，不是图存储的；与图存储相关的是节点/边与字节。
- **不能读出**：这些数字不区分图存储的哪一列（输入列、载荷列、权限/属性列）占了多少；`std::stats` 的 `bytes` 只累加节点记录、输入引用与载荷。重构后若要按列比较，应先扩展 `std::stats`。

## 已知的测量噪声

- `--timeit` 在 `alloc_small` 上偶发以访问冲突（`0xC0000005`，退出码 `3221225477`）结束：本次测量中 `std::nvm/5` 一次、`std::jit/5` 一次，约三十次 `--timeit` 调用中的两次；同样的命令重跑即通过。脚本对非零退出重试（最多三次）并在标准错误上记一行；这不是数字的一部分，是待查的崩溃，已记在 handover note ⟨89307a8⟩ (H8) 第 6 节。
- 机器不是空闲的：并行工作会抬高所有时间列。**在同一次调用内**比较各行是安全的，跨调用只应比较字节/节点这类与时间无关的列。

## 2026-10-21 复核（H17c）

H17b 用上表的 `map_chain` 数字（14.8 ms nvm）与它自己的构建比较，得出 "nvm +19%"，并怀疑是 H17 的代价。H17c 追这条线时重新构建了 `e8abfc30` 本身（构建脚本与原表相同，产物保留在工作树外的临时目录），在同一段机器空闲的时间里交错测量，只复核执行三列（编译与峰值没有重测）：

| 程序 | nvm：原表 → 本次重建 `e8abfc30` | fvm | jit |
|---|---|---|---|
| fib.deep | 219.0 → 227.2 ms（+3.7%） | 55.8 → 57.0（+2.2%） | 84.7 → 86.1（+1.7%） |
| alloc_small | 156.0 → 157.5（+1.0%） | 106.1 → 105.9（−0.2%） | 107.7 → 108.7（+0.9%） |
| map_chain | 14.8 → 14.9（+0.7%） | 10.4 → 10.5（+1.0%） | 10.4 → 10.8（+3.8%） |

也就是说原表的数字本身是可复现的（同一个提交、同一台机器，差 ≤4%）。但 H17c 同时确认：**执行时间在同类构建之间会有 4%–20% 的布局噪声**——一个不改变任何被执行的代码的提交（`fb8f6e4e`，rtdata 打印/相等/类型文本重构）让 `map_chain` 的 nvm 从 15.05 ms 变成 16.22 ms（+7.8%），微基准里数据相关分支的用例 +20% 以上；反过来，H17c 自己的无害改动又让同一程序从 18.06 ms 变成 17.26 ms（−4.4%）。fvm/jit 也受影响，只是幅度小些。细节与证据见 handover note ⟨13d8441⟩ (H17c) 第 1 节。

因此这张表的使用方式要收紧：

- **执行**三列（nvm/fvm/jit）只在**同一个构建内**比较（例如同一行里三个后端的差距），或者比较"同一提交重新构建、同时段交错测量"的两组数字；**不要**拿不同提交/不同构建的执行时间直接相减——4%–20% 的差异可能完全来自代码/数据布局。
- 与时间无关的列（图、节点、边、图字节、字节/节点）不受影响，仍可跨构建比较；这也是本文开头"重构之后用同一个脚本再跑一次"仍然成立的部分。
- 若以后要让执行时间也能跨构建比较，需要先让代码布局稳定（函数分段 + 确定性排序，或固定加载地址），那属于构建系统的改动，未在本次进行。

## 规模扩展（size scaling，2026-10-20）

上面六个程序最大 495 个节点，只能看到"每个节点占多少字节"，看不到**代价随程序大小增长**的部分：整个程序可达闭包上的扫描、每个重写 pass 对每张图的解码、内联器每次内联之后的重新收集。本节补上三个由脚本生成的、按大小递增的程序，记录 fsh-dev 现在的数字，并记录删掉步骤 3 里不必要的扫描之后的对比（同一组程序、同一个脚本）。

### 程序

`test/tools/perfbaseline/scale_programs.py` 生成（同一个单元数得到同一份文本，不提交任何生成物）。程序由 `units` 个函数 `f<i>(x: int): int` 加一个 `main` 组成；函数每 8 个连成一条调用链（后一个调用前一个，链首不调用任何函数），四种形状轮换：

| 形状 | 函数体 |
|---|---|
| arith | `g(x) * k + c` |
| branch | `if x > k then g(x - k) else x + c`（两个分支臂） |
| loop | `loop acc = x for j in 0..3 do acc + g(j)`（一个 LOOP 节点：条件图与循环体图） |
| generic | `pick(g(x), x + c, x > k)` 与 `len(pick('s<i>', 'abc', x > k)) + g(x)`：一个泛型函数 `pick` 的 int 与 string 两个实例 |

每个函数恰有一处对前一个函数的调用，所以内联一条链的代价线性于程序大小，运行代价是每个单元几次调用。`main` 通过每个含 40 次调用的辅助函数求和并打印，便于在改动编译器前后比较输出。

| 名称 | 单元数 | 图 | 节点 | 边 | 图字节 | 字节/节点 |
|---|---|---|---|---|---|---|
| scale.1k | 90 | 169 | 1130 | 1126 | 28060 | 24.8 |
| scale.10k | 900 | 1606 | 11009 | 10952 | 273792 | 24.9 |
| scale.50k | 4000 | 7108 | 48829 | 48662 | 1214724 | 24.9 |

`baseline.py` 默认把这三个程序和前面六个一起测（`--no-scale` 不测它们，`--only scale.10k` 只测一个）；大程序的轮数最多取 3。

### 机器与方法

| 项 | 值 |
|---|---|
| 机器 | Docker Desktop 上的 Linux 容器（WSL2 6.18，Ubuntu 24.04），Intel Core Ultra 5 125H，18 个逻辑核，15 GB 内存 |
| 构建 | clang 19，Release；fsh-dev `79060dd7` 加一行修复（`loop_driver.h` 的 `template` 关键字，没有它这个提交在 clang 19 下编不过） |
| 测量时间 | 2026-10-20 |
| 负载 | 同一容器里别的构建在并行：负载 1 到 18。**时间只在同一次调用内可比**；"之后"的对比用交错 A/B（`ab.py`），噪声底写在表里 |

这一节的机器不是上面 Windows 主机，数字**不能**和上面的表直接比。峰值内存取 `wait4` 报告的该子进程最大驻留集（POSIX 上 `baseline.py` 现在也给峰值，不再是 n/a）。

```bash
python3 test/tools/perfbaseline/baseline.py --rounds 5 --only fib.deep,scale.1k,scale.10k,scale.50k
python3 test/tools/perfbaseline/ab.py --a OUT_A --b OUT_B --pairs 5   # OUT 是 out/latest 目录
```

### fsh-dev 现在

`baseline.py --home <fsh-dev 的 out/latest> --rounds 5`，机器负载较低的时段（没有保证）。`std::opt` 是整个优化器（内联、特化、折叠、公共子表达式、代数化简、死代码，迭代到不动点）。

| 程序 | 图 | 节点 | 编译 | std::opt::inline | std::opt | 峰值：编译 / inline / opt |
|---|---|---|---|---|---|---|
| fib.deep | 5 | 31 | 5.9 ms | 0.1 ms | 115.9 ms | 14.7 / 14.7 / 14.7 MB |
| alloc_small | 7 | 69 | 5.2 ms | 0.2 ms | 20.6 ms | 14.7 / 14.7 / 14.7 MB |
| map_chain | 11 | 75 | 8.8 ms | 0.2 ms | 64.0 ms | 14.7 / 14.7 / 61.4 MB |
| build_tree | 11 | 80 | 5.8 ms | 0.3 ms | 49.8 ms | 14.7 / 14.7 / 16.2 MB |
| jit_const | 5 | 26 | 5.0 ms | 0.1 ms | 357.5 ms | 14.7 / 14.7 / 14.7 MB |
| train.mlp | 59 | 495 | 12.1 ms | 9.6 ms | 23.0 ms | 18.6 / 19.0 / 19.0 MB |
| scale.1k | 169 | 1130 | 15.4 ms | 134.1 ms | 222.0 ms | 19.9 / 21.2 / 22.6 MB |
| scale.10k | 1606 | 11009 | 118.7 ms | 1.97 s | 1.74 s | 84.6 / 88.8 / 90.3 MB |
| scale.50k | 7108 | 48829 | 572.3 ms | 11.31 s | 24.91 s | 332.5 / 347.4 / 351.0 MB |

（执行三列在 `--json` 里，随这个脚本一起生成；这里只列与图存储改动相关的。`scale.50k` 的 `std::opt` 在不同调用里相差一倍以上，11.98 到 24.91 s，原因是别的构建占用机器；读它的量级。）

读法：

- **编译**随节点数近似线性，约 12 µs/节点（572 ms / 48.8k）；这一列在删扫描前后相同，因为编译路径没有被改。
- **`std::opt::inline` 在删扫描之前是超线性的**：每个节点 0.12 ms（1.1k）、0.18 ms（11k）、0.23 ms（49k）。每一次成功的内联都让引擎重新收集整个闭包，所以一次 pass 的代价是 内联数 × 程序大小。
- **峰值内存**约 6.5 MB/千节点，加 14.7 MB 的运行时固定部分（与前面 Windows 的 11 MB 底噪同类）。`map_chain`（75 个节点）的 `std::opt` 峰值 61.4 MB 与程序大小无关。
- `std::opt` 在很小的程序上也要几十到几百毫秒（`fib.deep`、`jit_const`）：它们是递归加常量参数，特化展开受增长预算限制才停下。

### 删掉步骤 3 里不必要的扫描之后

对比同一台机器、同一时段、交错测量（`ab.py`，每对 A1 B A2）。A 是 fsh-dev，B 是 `gs/scans`。"pass 时间"是 `--timeit` 的单次均值（pass 本身，不含编译）；"噪声"是同一构建两次测量的差 |A1−A2| 与 A 之比。

| 程序 | 模式 | A (ms) | B (ms) | B/A | 噪声 |
|---|---|---|---|---|---|
| scale.1k | std::opt::inline | 140.5 | 34.9 | 0.25 | 1.9% |
| scale.1k | std::opt | 226.3 | 100.4 | 0.44 | 1.0% |
| scale.10k | std::opt::inline | 1982.4 | 202.1 | 0.10 | 1.4% |
| scale.10k | std::opt | 1809.1 | 305.7 | 0.17 | 4.0% |
| scale.50k | std::opt::inline | 12311.4 | 1257.2 | 0.10 | 5.4% |
| scale.50k | std::opt | 15450.9 | 1905.9 | 0.12 | 28.4% |

小程序（`fib.deep` 到 `jit_const`，31 到 80 个节点）：编译（进程 CPU 时间）的 B/A 在 0.76 到 0.99 之间，噪声 11% 到 18%，没有变慢的迹象；`std::opt` 的 B/A 在 0.52 到 0.94 之间（不再为没有可做之事的图打开草稿）；`std::opt::inline` 在 0.2 到 0.8 ms，在噪声内持平。峰值内存各程序相差不超过 1.5%。

`std::opt::inline` 在 `scale.10k` 与 `scale.50k` 上都快了约 10 倍，之后每节点 0.018 到 0.031 ms，随大小**线性**：不再是 内联数 × 程序大小。`std::opt` 快 2 到 8 倍（越大越多）；它剩下的代价主要是每次提交重新编码整个闭包（一轮 `std::opt` 要提交数次）和引擎每次重启仍要遍历一次闭包，这两项要等 `GraphId` 与增量提交。

输出不变的证据：1452 个测试程序和两个生成程序（`test/cases` 下 opt、linear、compile、consistency、loops、macro、modules、facts、trans、definitions、resources、para、perf，加上 `scale.1k`、`scale.10k`）在 `std::opt`、`std::opt::inline`、`std::opt::{fold,cse,algebra,dce,ctrl}`、`std::opt::{specialize,devirtualize,inline,fold}` 四组 pass 下，`std::gir` 的输出与 fsh-dev 逐字节相同。
