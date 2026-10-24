# Camel 示例

中文简体 | [English](README.md)

一组短程序，每个完成一件事。每个文件开头的注释说明它展示什么，并给出运行命令。命令都从仓库根目录执行，并假定 `camel` 已在 `PATH` 中。

```sh
camel examples/text/word_frequency.cml
```

命令行先写程序文件，后面可以再写作用于其计算图的遍。不写遍时，程序在默认后端上运行；`camel x.cml std::prl` 用并行调度器运行同一个程序。每个示例在 `std::nvm`、`std::fvm`、`std::jit` 和 `std::prl` 上的输出都相同。

想循序渐进地学习这门语言，请读[教程](../docs/tutorial/tutorial.cn.md)；想查某个语法结构的细节，请看[语言参考](../docs/language/README.md)。示例调用的库函数（`str`、`array`、`math`、`statistics` 等）及其签名和例子，见[标准库参考](../docs/stdlib-reference.md)。

## 按任务分类的示例

### 文本: `text/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`word_frequency.cml`](text/word_frequency.cml) | 用 `split`、`trim`、`to_lower` 把一段文本切成词，用 `Map<string, int>` 统计词频；用 `sort_by` 和 `take` 取出现最多的词 | `camel examples/text/word_frequency.cml` |
| [`table_report.cml`](text/table_report.cml) | 对齐的表格：列宽取最长的条目（`max`、`repeat`），表头用 `to_upper`，合计用 `sum` | `camel examples/text/table_report.cml` |
| [`csv_parse.cml`](text/csv_parse.cml) | 用 `split` 和 `trim` 把 CSV 行切成字段，用 `parse_int` 读出数字；不合格的行成为错误值，连同行号一起报告 | `camel examples/text/csv_parse.cml` |
| [`log_filter.cml`](text/log_filter.cml) | 读入日志文件，切成行和字段；按子串（`find`）或模式（`re.test`）筛选行，并按级别计数 | `camel examples/text/log_filter.cml` |

### 数据: `data/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`records.cml`](data/records.cml) | 结构体记录数组：筛选、派生新记录、`sort_by` 与 `sum`、用 `Map` 分组求和 | `camel examples/data/records.cml` |
| [`json_roundtrip.cml`](data/json_roundtrip.cml) | JSON 编码，以及解码为带类型的值；格式不符的文本得到错误值 | `camel examples/data/json_roundtrip.cml` |
| [`validate_input.cml`](data/validate_input.cml) | 可能失败的函数返回 `T!`；`?->`、`!!` 与 `match ... case error e` | `camel examples/data/validate_input.cml` |
| [`nullable_lookup.cml`](data/nullable_lookup.cml) | 可能查不到的查找返回 `T?`（`array` 的 `find` 与 `index_of`、`Map` 查找）；`??` 与 `match ... case null` | `camel examples/data/nullable_lookup.cml` |
| [`dedup_sets.cml`](data/dedup_sets.cml) | 用 `unique` 或 `Set<T>` 去重；并集、交集、差集 | `camel examples/data/dedup_sets.cml` |

### 文件: `files/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`read_write.cml`](files/read_write.cml) | 写入文本文件，读回并切成行；用 `fs.try_read_text` 返回的 `string!` 处理文件不存在的情况 | `camel examples/files/read_write.cml` |
| [`json_config.cml`](files/json_config.cml) | 把 JSON 文件中的配置读入结构体；文件缺失或结构不符时使用默认值 | `camel examples/files/json_config.cml` |

### 排序与查找: `algorithms/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`merge_sort.cml`](algorithms/merge_sort.cml) | 用递归写的归并排序，并与 `array.sort` 和 `array.sort_by` 的结果比较；程序中需要排序时调用的是这两个库函数 | `camel examples/algorithms/merge_sort.cml` |
| [`binary_search.cml`](algorithms/binary_search.cml) | 尾递归的二分查找；找不到时返回 `null`（`int?`）。任意顺序的数组可用 `array.index_of` | `camel examples/algorithms/binary_search.cml` |

### 函数: `functions/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`closures.cml`](functions/closures.cml) | 函数作为值：闭包、函数组合、`with` 参数、带自身状态的计数器 | `camel examples/functions/closures.cml` |
| [`generic_helpers.cml`](functions/generic_helpers.cml) | 在头部声明类型参数（`::<T>`）的泛型辅助函数，建立在 `array` 的泛型函数之上 | `camel examples/functions/generic_helpers.cml` |

### 命令行工具: `cli/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`temperature.cml`](cli/temperature.cml) | `main` 的参数就是程序的输入，由 `--arg name=value` 给出；单位用 `trim` 和 `to_upper` 规范化 | `camel examples/cli/temperature.cml --arg value=21.5 --arg unit=C --arg steps=3` |
| [`exit_status.cml`](cli/exit_status.cml) | `main` 返回的 `int` 是进程退出码；`exit(n)` 立即结束程序 | `camel examples/cli/exit_status.cml --arg used=91 --arg limit=80`<br>`camel examples/cli/exit_status.cml --arg used=40 --arg limit=80` |

### 并行执行: `parallel/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`parallel_map.cml`](parallel/parallel_map.cml) | 同一个 `map`，由命令行上的遍决定顺序执行还是并行执行 | `camel examples/parallel/parallel_map.cml`<br>`camel examples/parallel/parallel_map.cml std::prl` |

### 数值模拟: `numeric/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`monte_carlo_pi.cml`](numeric/monte_carlo_pi.cml) | 用固定种子的蒙特卡洛方法估计圆周率，并用 `statistics`、`min`/`max` 和 `math` 汇总 | `camel examples/numeric/monte_carlo_pi.cml` |
| [`random_walk.cml`](numeric/random_walk.cml) | 用 `random` 模块做固定种子的随机游走。各步写成尾递归：依赖状态的调用在 `map` 内没有先后顺序 | `camel examples/numeric/random_walk.cml` |
| [`ode_euler.cml`](numeric/ode_euler.cml) | 用尾递归对冷却方程做欧拉积分，并与解析解比较 | `camel examples/numeric/ode_euler.cml` |

### 张量与训练: `ml/`

| 文件 | 展示的内容 | 运行命令 |
|------|------|------|
| [`train_linear.cml`](ml/train_linear.cml) | 最小的训练循环：`autodiff.grad` 求梯度，`nn.sgd` 更新参数 | `camel examples/ml/train_linear.cml` |
| [`export_onnx.cml`](ml/export_onnx.cml) | 用 `onnx.export_model` 把一个两层小模型导出为 ONNX 文件 | `camel examples/ml/export_onnx.cml`<br>`camel examples/ml/export_onnx.cml std::macro std::opt onnx::export` |

`files/` 和 `ml/` 下的程序以及 `text/log_filter.cml` 把文件写到 `tmp/examples/`（相对于执行命令时所在的目录）；目录由程序自己创建，不受 git 跟踪。

## 其他目录

- [`python/`](python/)：从 Python 里用 Camel。`python/rules/` 是一个 Python 项目，把定价规则写成 Camel 模块（`pip install camel-lang`，然后 `python examples/python/rules/basket.py`）；见 [`python/rules/README.cn.md`](python/rules/README.cn.md) 与 [技术文档 57](../docs/technical/57_python_bindings.md)。
- [`perf/`](perf/)：`fibs.cml` 用 `perf.perfms` 给递归的斐波那契函数计时（`camel examples/perf/fibs.cml std::fvm`）；输出是测量结果，每次运行都不同。
- [`tty/`](tty/)：`snake.cml` 是交互式的终端游戏，供手动体验（`camel examples/tty/snake.cml`）；见 [`tty/README.md`](tty/README.md)。

## 检查示例

测试计划 `test/plans/feat/examples.plan.toml` 在四个后端上运行上面表格中的每个程序，并把输出与 `test/plans/feat/golden/examples/` 下记录的文本比较：

```sh
node scripts/test.js test/plans/feat/examples.plan.toml
```

该计划包含在 `npm run test` 运行的默认集合中。有意改变某个示例的输出后，重新记录：

```sh
node test/tools/run-tests.mjs --update-golden test/plans/feat/examples.plan.toml
```
