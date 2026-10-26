# time 重写说明

稿件：`docs/handover/stdlib/time.cml`。未编译。

## 发现的缺陷（先说）

`modules/time/operators.cpp` 里的 `strftime` 有一个文档里没有的第三个参数 `as_local`，为真时加上**写死的 8 小时**；`strptime` 把字段按本地时间 `mktime`，再减去**写死的 `8 * 3600`**。也就是说它只在东八区正确，其余时区给出错的值，而文档（"fields are taken as local time"）和资源声明（`strptime`/`strftime` 读 `env`）都在为这个行为圆场。本稿的处理：时间的文本互转**一律 UTC、纯函数**，不依赖时区，两处写死的偏移与 `as_local` 一并删除。

## 改了什么

- 删 `module time`，删 `@effect`，签名写 `uses clock` / `pure`。
- **`strftime`、`strptime` 是纯函数**（`pure`）：同样的输入同样的输出，在任何机器上相同（3.2 "纯计算是确定的"）。原先 `strftime` 的文档是"declared with effects together with the rest of the module"，实际并不读环境。资源声明里的 `env` 读取随之删除，常量折叠和公共子表达式合并因此可以作用在它们上面。
- **数据在前，格式是配置**：`ts->strftime<'%Y-%m-%d'>`、`text->strptime<'%Y-%m-%d'>`；`strftime<'%Y'>` 本身是个"年份格式化器"，可以传给 `map`。
- **`strptime` 返回 `float!`**：解析的是外部文本，格式不符是 `ParseError` 错误值（与 `parse_int` 同一个种类），不再是 `TimeFormatFailed` 故障。格式串本身不可用（程序自己的错）以及 `strftime` 的时刻超出日历范围、格式不产生文本，仍是故障 `TimeFormatFailed`。
- **新增 `timed::<T, R>(f, x): (R, float)`**：设计原则 3.4 已定"计时用 `time::timed(f, x)`"，`bench::timeit` 建在其上。它是原子：必须保证"测的就是这一次 `f(x)`"，不被提前算掉、挪走或合并，设备上还要等计算完成，这些 Camel 体写不出来（同步块里纯计算本来就不排序）。`f` 是被变换的函数，按 4.4 是普通参数，不是 `with`。耗时用单调时钟，单位秒。
- `now()` 保留，秒数（`float`）从 epoch 起，带小数。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `now(): float` | 保留，`uses clock` | |
| `timed::<T, R>(f, x): (R, float)` | **新增** | 3.4 已定；`perf.cml` 里的 `timeit` 目前用两次 `now()` 相减，测不准 |
| `strftime(ts)` with `<format>` | 改：参数顺序、纯、去掉 `as_local` 与写死偏移 | 见上 |
| `strptime(text)` with `<format>` | 改：纯、返回 `float!`、字段按 UTC | 见上 |

## C++ 侧改动（`modules/time`）

- `__time_strftime__`：删 `as_local` 与 `+ 8 * 3600`；参数改为 `norm[0]` = 时刻、`with[0]` = 格式。
- `__time_strptime__`：用 `timegm`（POSIX）/`_mkgmtime`（Windows）取代 `mktime` 加 `- 8 * 3600`；匹配失败改为 `makeError("ParseError", ...)`，结果类型 `float!`。`myStrptime` 本身是自带的解析实现，不动。
- `module.cpp`：`strftime`、`strptime` 在 `declare` 里标纯，删 `declareResources` 里两项 `env`。
- **新增 `timed` 的实现**。它要调用函数值，且不能被图遍当作普通纯节点处理：建议登记为"带函数实参的效应算子"，资源 `clock`，再加上 `f` 的资源（函数类型参数默认对资源泛型，3.3）。这是本稿唯一需要执行器支持"算子调用一个闭包"的地方；`first_of`/`attempt` 已有类似的先例，沿用其机制。若短期做不到，可先在 `perf.cml` 之外不引入，本文件暂时只声明不加载。
- 不需要 `stdlib/signatures/time.cml`。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `strftime(ts, fmt)` → `ts->strftime<fmt>`（约 5 处）、`strptime(text, fmt)` → `text->strptime<fmt>` 并处理 `!`（约 4 处） | `test/cases/modules/std/time/basic.cml`、`test/cases/compile/diag_render/runtime_time_format.cml`（及 `test/plans/feat/compile/golden/diag_render/runtime_time_format.txt`，诊断位置变化）、`test/cases/compile/native/time_{arity,type,declaration_import}.cml`、`test/plans/feat/modules/{resources,stdlib_registry}.plan.toml`、`test/plans/feat/compile/native_declarations.plan.toml` |
| 依赖 `strptime` 为东八区的期望值 | `test/cases/modules/std/time/basic.cml` 里若有按本地时区算出的数，改为 UTC 值 |
| 用 `now()` 相减计时的地方改 `timed`（`now` 约 76 处，大半是计时） | `test/cases/perf/`（8 个）、`test/cases/para/core/`（7）、`test/cases/linear/nvm/`（4）、`test/cases/opt/generic/`（3）、`stdlib/perf.cml`（`timeit` 改建在 `timed` 上，那是 `bench` 的事，不在本稿范围） |
| 清单类 | `docs/language/05_modules.md` 与 `en/`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/technical/41_resources_and_definitions.md`、`docs/design/stdlib/README.cn.md` |

## 开放问题

1. **本地时间不在本模块**：时间的文本互转一律 UTC。
2. **`timed` 的签名**：单个实参 `x`。多实参的函数要打成元组。推荐先这样（`timed(f, x)` 就是 3.4 的原话）；。
3. 日期的结构体与时长类型不在本稿。
