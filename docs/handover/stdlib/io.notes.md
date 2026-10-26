# io 重写说明

稿件：`docs/handover/stdlib/io.cml`。未编译。

## 改了什么

- 删 `module io`；`@effect` 标签删除，资源写进签名：全部 `uses stdin`（与 C++ `declareResources` 一致：`stdin` 读/写）。
- 模块说明改为"终端当键盘用"。写终端与整行读取仍是前奏的 `print`/`println`/`eprintln`/`input`，不在这里；`Stream` 与 `stdin` 值留给流的后续工作（见开放问题 2）。
- **没有哨兵值**：`get_char` 原先没有按键时返回 `''`。改为 `string?`，写成 Camel 体：`get_chars(1)` 为空则 `null`。
- **`get_chars(max)`**：原先负数 `max` 在 C++ 里意为"不限"（隐含的哨兵）；改为负数是故障（`InvalidArgument`）。"取走全部等待中的输入"就是 `clear_input_buffer` 做的事，不另开一条。`''` 在这里是正常的文本（"没有字符"），不是哨兵，所以返回类型仍是 `string`。
- **`set_terminal_raw_mode` 返回 `void!`**：stdin 不是终端（管道、文件）是环境造成的失败，原先抛 `RuntimeError` 故障。新增种类 `NotATerminal`（见 fs 说明里的"错误种类没有集中目录"一条）。关闭 raw 模式永远成功。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `set_terminal_raw_mode(enabled): void!` | 改（原 `void`，故障） | 环境失败是 `!` |
| `has_input(): bool` | 保留 | 原子 |
| `get_chars(max): string` | 改：负数是故障，不再表示不限 | 去掉哨兵 |
| `get_char(): string?` | 改：Camel 体，`string?` | 去掉空串哨兵；少一个内核 |
| `clear_input_buffer(): void` | 保留 | 原子 |

## C++ 侧改动（`modules/io`）

- 删 `__io_get_char__` 与算子表项。
- `__io_set_terminal_raw_mode__`：失败改 `makeError("NotATerminal"/"IOError", ...)`，返回 `void!`（对照 `fsRemoveImpl` 的 `makeOk(NullSlot, resultType)`）。
- `__io_get_chars__`：`maxChars < 0` 抛 `InvalidArgument` 故障；内部 `readInput(-1)` 保留给 `clearInputBuffer`。
- Windows 与 POSIX 两套 `Terminal` 实现不动。raw 模式在程序结束时恢复（原则 3.9"终止之前保证清理"）：现在的实现靠 `rawModeEnabled` 静态量，没有在退出路径上恢复；文档里写了这条承诺，需要核对或补上。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `get_char()` 返回 `string?`（3 处，原来拿 `''` 判断） | `examples/tty/snake.cml`、`test/cases/modules/io/`（`ordering.cml`、`roundtrip.cml`、`effect_graph.cml`） |
| `set_terminal_raw_mode` 返回 `void!`（4 处） | `examples/tty/snake.cml`、`test/cases/modules/io/raw_mode.cml`（该用例现在用 `attempt` 接故障，改为匹配错误值 `NotATerminal`）、`test/cases/compile/native/io_{arity,type}.cml` |
| 去掉 `module io` 的清单类引用 | `test/plans/feat/modules/io.plan.toml`、`test/plans/feat/modules/resources.plan.toml`、`test/plans/feat/compile/native_declarations.plan.toml`、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/technical/41_resources_and_definitions.md` |

## 开放问题

1. **模块名**。`io` 现在只剩键盘，叫 `terminal` 更准；改名要动 13 个文件与算子 URI 前缀（`io:`）。推荐暂不改：等流落地、`io` 要容纳 stdin 流时再看（那时它又名副其实）。
2. **标准输入的流**（`stdin_lines(): string!..` 之类）是设计稿 G6 的来源之一，本稿不加，等流来源机制落地后与 `fs::read_lines` 同批。
