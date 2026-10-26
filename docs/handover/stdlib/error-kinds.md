# 标准库的错误种类目录（草稿）

错误值（`T!`）的种类名（`e.kind`）。来源：对 `stdlib/`、`modules/`、`src/` 里 `makeError(...)`、`@fails` 的搜索，加上本轮重写新增的种类。故障（`fault`）不在此表：它们是 `RuntimeDiag` 的诊断码。种类名是 PascalCase，一个种类只有一个意思，不同模块的同类失败共用同一个种类。

## 通用（环境与输入）

| 种类 | 含义 | 返回它的函数 |
|---|---|---|
| `NotFound` | 路径指向的东西不存在；或系统里没有所问的对象 | `fs`：`read_text`、`read_bytes`、`read_chunk`、`read_lines`（元素）、`file_size`、`list_dir`、`mkdir`（父目录不存在）、`remove`、`remove_dir`；`env::get_cwd`（目录已删除）；`process::user_name` |
| `PermissionDenied` | 系统拒绝了这次访问（含只读文件系统） | 同上 `fs` 各函数与 `write_*`、`append_*`、`mkdirs`；`env::get_cwd` |
| `AlreadyExists`（新增） | 要创建的东西已经在了，或同路径是另一种东西 | `fs::mkdir`；`fs::mkdirs`（路径上是文件而非目录） |
| `IOError` | 其余输入输出失败：路径是目录而要读文件、写失败（含磁盘满）、非空目录、读到一半失败 | `fs` 的读、写、`append_*`、`list_dir`（不是目录）、`remove`、`remove_dir`、`file_size`、`mkdir*`；`io::set_terminal_raw_mode`（终端拒绝） |
| `NotATerminal`（新增） | 标准输入不是终端，无法进入 raw 模式 | `io::set_terminal_raw_mode` |
| `EncodingError` | 字节不是合法的 UTF-8，或文本超过字符串的上限 | `bytes::decode`；`fs::read_text`、`fs::read_lines`（元素） |
| `ParseError` | 外部文本不符合所期望的写法 | `parse_int`、`parse_float`（前奏）；`time::strptime`（本轮由故障改为此种类） |

## json

| 种类 | 含义 | 函数 |
|---|---|---|
| `JsonParseError` | 文本不是 JSON（消息给出位置） | `json::decode`、`json::decode_as` |
| `JsonTooDeep` | 文档嵌套超过深度限额（默认 10000） | `json::decode`、`json::decode_as` |
| `JsonTypeError` | 文本是 JSON，但与要解码成的类型不符（消息给出路径） | `json::decode_as` |

## 其他模块（现状，非本轮范围，列出以求完整）

| 种类 | 含义 | 函数 |
|---|---|---|
| `IndexError` | 索引越界而调用声明为可失败 | `stdlib/rewrite.cml` 中 7 处 `@fails`、`stdlib/signatures/graph.cml` |
| `RuleInvalid` | 规则不能编译 | `rewrite`（`seq`、`first`、`repeat`、`until`、`alternate` 等） |
| `RuleTwoWay` | 规则被按它不允许的方向使用 | 同上 |
| `GraphInvalid`、`GraphConflict`、`TxnUsed`、`NoChain`、`SignatureMismatch`、`ClosureNotEmpty` | 图事务与图改写的各类失败 | `modules/graph/txn.cpp`、`modules/rewrite/module.cpp` |
| `NoAttribution`、`NoCostModel` | 自动微分的归因/代价模型缺失 | `modules/autodiff/pullback.cpp`、`stdlib/signatures/autodiff.cml` |
| `CheckpointError` | 检查点读写失败 | `ckpt` |
| `RangeError` | 前奏文档里 `throw error('RangeError', ...)` 的示例用名，不是库返回的种类 | `stdlib/signatures/globals.cml`（示例） |

## 说明

- `IOError` 是兜底；能归入 `NotFound`、`PermissionDenied`、`AlreadyExists` 的不要用 `IOError`（`fs` 的 `errorKind()` 已按此映射）。
- `JsonParseError`/`ParseError` 的区别：前者是 JSON 文本，后者是其他按固定格式读的文本；`strptime` 用 `ParseError`。
- `IndexError`、图与自动微分各条的归并（例如是否并入通用种类）不在本轮，由对应模块的重写者决定。
- 新增 `AlreadyExists`、`NotATerminal` 需要在 C++ 侧 `errorKind()`（`modules/fs/operators.cpp`）与 `modules/io` 中实际产生。
