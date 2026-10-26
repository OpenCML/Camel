# process 重写说明

稿件：`docs/handover/stdlib/process.cml`。未编译。

## 改了什么

- 删 `module process`，删 `@effect`，签名写 `uses`（`sleep` → `clock`、`exit`/`thread_count` → `process`、`user_name`/`cpu_count` → `env`，与现有 `declareResources` 一致）。
- **`sleep` 与 `exit` 收进本模块**。原先 `process.cml` 注释写着"`sleep` 与 `exit` 也是内建，不用导入"，而 `stdlib/signatures/globals.cml` 里另有 `#op('process:sleep')`、`#op('process:exit')` 的声明：同一个算子两处声明，违反"一个事实只写一处"。`exit` 在 `process.cml` 原来根本没有声明。本稿把两者都声明在这里；前奏里的那两条要删（见开放问题 1）。
- **`sleep` 的单位改为秒（`float`）**：与 `time::now`（秒，`float`）、3.9 的超时参数（`timeout: 5.0`）、`perf` 的计时一致；原来是毫秒 `int`，一个库里两种时间单位。负数或 NaN 仍是故障。
- **`whoami` → `user_name`，返回 `string!`**：取值器用名词（conventions 3），不用 shell 命令的名字；系统没有用户名（容器里常见）是环境造成的失败，原先抛 `RuntimeError` 故障，改为 `NotFound` 错误值。
- `cpu_count`（至少 1，不会失败）、`thread_count` 保留。`thread_count` 是运行时自省，只有测试在用（`test/cases/modules/tensor/{thread_count,gemm_threads,thread_pools}.cml`），留在这里因为"进程有几个线程"本来就是进程的事实。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `sleep(seconds: float)` | 改单位；`uses clock` | 与全库的时间单位一致 |
| `exit(code: int)` | **新增到本模块**（从前奏搬来） | 一个算子一处声明 |
| `whoami` | **改名** `user_name`，返回 `string!` | 名词取值器；环境失败用 `!` |
| `cpu_count` | 保留 | |
| `thread_count` | 保留 | |

## C++ 侧改动（`modules/process`）

- `__process_sleep__`：参数改读 `Float64` 秒，转成 `std::chrono::duration<double>`；检查 `seconds < 0 || isnan`。
- `__process_whoami__`：算子名改为 `user_name`（算子表项、`OperatorTable::declare`、`declareResources`、执行器注册表都改）；失败改 `makeError("NotFound", ...)`，返回 `string!`。
- `exit` 的算子不动，只是它的签名声明从 `globals.cml` 搬到 `process.cml`。
- 不需要 `stdlib/signatures/process.cml`。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `sleep(ms)` → `sleep(秒)`（约 15 处，其中 5 处没有导入、用的是前奏） | `test/cases/modules/std/process/basic.cml`、`test/cases/para/core/`、`test/cases/compile/native/process_*`、`test/cases/modules/rename/process_{old,new,identity}.cml`、`examples/`（含 `delay_ms` 变量的几处：改为秒并改名 `delay`）、`stdlib/signatures/globals.cml` |
| `exit` 由前奏改为 `import { exit } from process`（13 个文件没有导入） | `test/cases/consistency/attempt/{not_isolated_exit,resource_release}.cml`、`test/cases/consistency/cleanup/{exit_in_map,exit_zero,file_written_before_exit,python_output_before_exit,unflushed_print_exit}.cml`、`test/cases/consistency/faults/exit_code.cml`、`test/cases/modules/tiles/{batch_matmul,layout_grad,mean_grad,mixed_matmul}.cml`、`examples/cli/exit_status.cml` |
| `sleep` 由前奏改为导入（3 个文件） | `test/cases/consistency/attempt/siblings.cml`、`test/cases/consistency/faults/sleep_global.cml`、`examples/tty/snake.cml` |
| `whoami` → `user_name`（6 处） | `test/cases/modules/std/process/basic.cml`、`test/cases/modules/rename/process_*.cml`、`test/cases/compile/native/process_declaration_import.cml`、`test/cases/modules/nn/checkpoint_verify.mjs`、`test/cases/modules/std/fs/remove_permissions.mjs`、`docs/language/05_modules.md` 与 `en/`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/technical/41_resources_and_definitions.md`、`test/plans/feat/modules/{resources,stdlib_registry}.plan.toml`、`test/plans/feat/compile/native_declarations.plan.toml` |

## 开放问题

1. 已裁定：`sleep`、`exit` 只在 `process`，前奏里的两条（`stdlib/signatures/globals.cml`）删除。
2. **`sleep` 的单位**我定了秒（理由见上）；毫秒整数的好处是没有浮点，但全库其余部分都是秒。
3. **子进程**（`run(cmd, args)`，捕获输出、退出码、超时）是 `process` 最明显的缺口，设计稿提到"子进程"作为等待的来源，但没有定接口。本稿不加；需要的话建议另开设计稿（输入输出用 `Bytes`/流，超时是参数，失败是 `!`）。
