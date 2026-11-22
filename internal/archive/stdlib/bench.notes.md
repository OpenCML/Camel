# bench（原 perf 与 sim）

稿件：`bench.cml`。未编译。`stdlib/perf.cml`、`stdlib/sim.cml` 删除，由它取代。`this`（`import this` 打印 Camel 之禅）保留，不在本批范围。

## 判断

两者都不是"用户写程序要依赖的标准库"：
- `perf`（`timeit`、`perfms`）和 `sim`（`pi_hits`、`asian_call_price`）：计时与基准用的工作负载，设计稿（`docs/design/stdlib/README.cn.md`）已把它们归入工具层 `bench`（"原 `perf`、`sim`"）。

处理：`perf` 与 `sim` 合并为一个工具模块 `bench`，仍放在 `stdlib/` 里（测试和示例要导入它），但文档与参考页把它标在"工具"层，不在使用层。

## 改了什么

1. **`timeit` 重写**：原来是递归加 `var result`、每轮打印、模块名竟是 `util`（与文件名不符），用两次 `now()` 相减计时（纯计算会被折叠/挪动，测不准，3.4）。现在建在 `time::timed` 上：`fn->timeit<repeat: n>` 返回 `float[]`（每次的秒数），不打印、没有 `var`；一个函数体。
2. **`perfms` 缩成两行**：均值（毫秒）与 95% 置信半宽，建在 `timeit` 与 `statistics` 上。`repeat` 是 `with` 参数（配置），被计时的函数是数据。
3. **`sim` 的两个内核不再是 C++**：`pi_hits`、`asian_call_price` 用 `random` 的键式随机数写成 Camel（`(0..n)->map<…>->sum`），保持"纯、带种子、可并行"——它们存在的唯一理由就是给调度器测试一个这样的工作负载。**随机数换成 Philox 后原来的金标（`pi_hits(10000, 42) = 7863` 等）本来就要重录**，所以现在换实现没有额外代价。负的样本数/路径数仍是故障（`fail<'InvalidArgument'>`），不再用 `RuntimeError`。
4. 删 `module` 行、`@pure`、`@effect`。

## 函数表

| 项 | 处理 |
|---|---|
| `perf::timeit`、`perf::perfms` | 搬进 `bench`，重写，`timeit` 返回 `float[]` |
| `sim::pi_hits`、`sim::asian_call_price` | 搬进 `bench`，Camel 实现 |

## C++ 侧改动

- 删除 `modules/sim`；`modules/perf` 本来就没有（`perf.cml` 是纯 Camel）。
- 注册表（`stdlib_registry.plan.toml`）里 `sim:` 的算子条目删除。

## 调用点

| 变化 | 位置 |
|---|---|
| `sim` → `bench`（12 个文件） | `test/cases/para/core/{asian_option_mc,monte_carlo_pi,monte_carlo_pi_small,unordered_foreach_smoke}.cml`、`examples/numeric/monte_carlo_pi.cml`、`test/cases/modules/std/sim/basic.cml`、`test/cases/compile/native/sim_{arity,declaration_import,type}.cml`（最后三个随模块删除）；金标重录 |
| `perf::timeit`/`perfms` 的使用（实际调用只有 4 处） | `test/cases/compile/entry_args/timeit_failure.cml`、`test/tools/selftest/benchmark_failure.cml`、`examples/perf/fibs.cml`、`test/cases/linear/jit/perf_fib_smoke.cml`；返回类型变了（`float[]`），打印由调用方负责 |

## 开放问题

1. **`bench` 是否仍放在 `stdlib/`**：推荐放（要能 `import bench`），参考页里分在"工具"层；若要彻底移出，放 `test/lib/` 并让测试用相对导入。
2. **`pi_hits` 的并行度**：用 `map` 求和，`map` 元素之间无序且可并行，这正是用例要测的；返回值与线程数无关（`sum` 对整数按顺序累加，整数加法可交换）。
3. 例子里的 `pi_hits(1000, 42) > 700` 只是个不依赖具体金标的断言（期望 785 左右）；重录金标后可以换成确切值。
