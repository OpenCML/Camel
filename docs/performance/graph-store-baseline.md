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

- `--timeit` 在 `alloc_small` 上偶发以访问冲突（`0xC0000005`，退出码 `3221225477`）结束：本次测量中 `std::nvm/5` 一次、`std::jit/5` 一次，约三十次 `--timeit` 调用中的两次；同样的命令重跑即通过。脚本对非零退出重试（最多三次）并在标准错误上记一行；这不是数字的一部分，是待查的崩溃，已记在 [H8 报告](../handover/reports/H8.md) 第 6 节。
- 机器不是空闲的：并行工作会抬高所有时间列。**在同一次调用内**比较各行是安全的，跨调用只应比较字节/节点这类与时间无关的列。