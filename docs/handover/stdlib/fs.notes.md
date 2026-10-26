# fs 重写说明

稿件：`docs/handover/stdlib/fs.cml`。未编译（本机没有工具链）；语法按 `stdlib/stream.cml`、`stdlib/types.cml`、`test/cases/effects/native_clause.cml` 的既有写法对齐。

## 总思路

1. **文本与二进制两套**（11.1），两者只在 `bytes::encode`/`decode` 相遇。文本函数用 Camel 写在二进制函数之上，所以 C++ 内核从"文本一套、二进制一套"收成**只剩二进制一套**（原子集合最小且正交，10.4）：`read_text` = `read_bytes ?-> decode`，`write_text` = `encode` 后 `write_bytes`。各平台写出的字节相同（11.5）由此自然成立。
2. **错误不是故障**（3.9、conventions 5）：文件不存在、无权限、磁盘满都是环境造成的失败，一律 `T!`；原先的 `read_text`（故障）与 `try_read_text`（错误值）是同一种失败的两套表示，合并成一个返回 `string!` 的 `read_text`。传了负的 offset/size 仍是故障（程序自己的错）。
3. **`with` 的判据**（4.4）：读函数里路径就是流过的数据（`'a.txt'->read_text`）；写函数里流过的是内容，路径是"决定写到哪个文件"的配置，所以 `content->write_text<'out/log.txt'>`，`write_text<'out/log.txt'>` 本身就是"写这个文件的函数"，可以部分应用后当回调传。`read_chunk` 的 `offset`、`size` 是配置，路径是数据。
4. **`uses`**：无函数体的声明全部写 `uses fs`（10.x：原生算子在签名里写资源，没写的按"全局"资源，会无谓地与一切效应排序）。Camel 函数体的导出函数（`read_text`、`write_text`、`append_text`）**不手写** `uses`：资源由编译器推出，手写只是重复；它们的资源正是 `fs`，悬停可见。
5. 原来的 `@effect ...` 文档标签删除（资源现在写在签名里，文档标签是第二份事实）。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `read_text(path): string!` | 改（原 `: string` 故障）；改为 Camel 体 | 与 `try_read_text` 合并；`EncodingError` 随 `decode` |
| `try_read_text` | **删除** | 并入 `read_text`，同一种失败只一种表示 |
| `read_bytes(path): Bytes!` | 改（原 `: Bytes`，失败是故障） | 同上 |
| `read_chunk` | 改：`path->read_chunk<offset, size>`，返回 `Bytes!` | offset/size 是配置，路径是数据；文件失败是错误值，负参数仍是故障 |
| `read_lines(path): string!..` | **新增** | 设计稿 `io-concurrency-streams` 3.1 的流来源；读取只在被拉取时发生 |
| `write_text(content)` with `<path, atomic = false>` | 改（原 `write_text(path, content): void` 故障） | 内容是数据、路径是配置；失败是 `void!`；Camel 体 |
| `write_bytes(content)` with `<path, atomic = false>` | 改 | 同上；仍是 C++ 内核 |
| `replace(path, content)` | **删除**，并入 `atomic: true` | "原子地换掉"是写的一种方式（开关），不是另一个函数；`replace` 还与 `str::replace` 撞名 |
| `append_text`、`append_bytes` | **新增** | 写日志不用先读后写；`append_bytes` 是内核，`append_text` 是 Camel 体 |
| `exists`、`is_file`、`is_dir` | 保留，加 `uses fs` | 回答 bool，不能访问的路径按"不存在"，文档写明 |
| `file_size(path): int!` | 改（原故障 `FileSystemError`） | 文件不存在是环境失败 |
| `list_dir(path): string[]!` | **新增** | 没有它读不了目录；按名字的字节序给出，各平台一致 |
| `mkdir(path): void!` | 改（原 `bool`，"已存在返回 false"） | 失败用 `!`；已存在是 `AlreadyExists` 错误 |
| `mkdirs(path): void!` | 改（原 `bool`） | 幂等：已有目录不是错误，所以可以重复调用；同路径是文件才是 `AlreadyExists` |
| `remove`、`remove_dir` | 保留，加 `uses fs`，`@effect` 删除 | 本来就是 `void!`，种类已是 `NotFound`/`PermissionDenied`/`IOError` |

## C++ 侧改动（`modules/fs`）

- 内核只剩：`read_bytes`、`read_chunk`、`write_bytes`（带 `atomic` 开关，吸收 `replace` 的实现）、`append_bytes`（新）、`exists`/`is_file`/`is_dir`/`file_size`、`list_dir`（新）、`mkdir`/`mkdirs`/`remove`/`remove_dir`、`read_lines`（新，流来源）。删除 `__fs_read_text__`、`__fs_try_read_text__`、`__fs_write_text__`、`replace` 与对应的算子表项。
- 读、写、`file_size`、`mkdir*` 的失败由抛故障改为 `makeError`；`errorKind` 已有，补 `AlreadyExists`；`openFailureReason` 之类按错误种类映射。
- 资源声明（`declareResources`）不变，与签名里的 `uses fs` 必须一致（native_clause 用例检查）；`append_bytes`、`list_dir` 补登记（RMW / Read）。
- `write_bytes` 带 `with <path, atomic>`，C++ 读 `with` 视图（`str` 模块已有先例）。
- 可选的性能项：`read_text` 多一次拷贝（字节 → 字符串）。需要时用 `#impl(read_text_kernel)` 配原生内核（10.4），先量再说。
- `read_lines` 依赖流来源机制（设计稿 G6，`stream.cml` 里"`#op` 来源可以交出一个流"）。该机制未落地前这一条声明不能加载，可先不并入。

## 需要迁移的调用点

（计数来自对导入了 `fs` 的文件的粗略搜索；`docs/stdlib-reference.md` 是生成物不手改。）

| 变化 | 位置 |
|---|---|
| `read_text` 由故障改为 `string!`（约 20 处） | `test/cases/modules/std/`（5）、`test/cases/consistency/cleanup/`（2）、`test/cases/modules/bytes_text_reject.cml`、`test/cases/compile/diag_render/`、`docs/tutorial/examples/08_*.cml`、`docs/tutorial/08_modules_and_stdlib.{cn,en}.md`、`docs/reference/camel-for-ai.md` |
| `try_read_text` → `read_text`（约 10 处） | `examples/files/read_write.cml`、`examples/files/json_config.cml`、`examples/text/log_filter.cml`、`test/cases/compile/{entry_args,native,tagged}/`、`docs/technical/29_nullable_and_fallible_values.md` |
| `write_text(p, c)` → `c->write_text<p>`（约 25 处） | `test/cases/modules/std/`（7）、`test/cases/consistency/cleanup/`（5）、`test/cases/modules/import_audit/`（2）、`examples/files/*`、`examples/text/log_filter.cml`、`test/cases/compile/{entry_args,native}/`、`docs/cli.md`、`docs/cli.cn.md`、两份 tutorial 08 |
| `write_bytes(p, b)` → `b->write_bytes<p>`（约 6 处）、`read_chunk(p, o, s)` → `p->read_chunk<o, s>`（约 8 处）、`read_bytes`（约 7 处，要处理 `!`） | `test/cases/modules/{bytes_values,bytes_text_reject,fs_binary_roundtrip}.cml`、`test/cases/para/core/file_chunk_hash.cml`、`docs/design/training/README.cn.md` |
| `replace(p, b)` → `b->write_bytes<p, atomic: true>` | 同上批文件中的 `fs::replace` 调用（搜索时与 `str::replace` 混杂，需人工区分；`test/cases/modules/rename/`、`test/cases/compile/native/` 里也有） |
| `mkdirs`/`mkdir` 返回值由 bool 改为 `void!` | `examples/ml/export_onnx.cml`、`examples/files/*`、`examples/text/log_filter.cml`、`test/cases/modules/std/`（5）、`test/cases/consistency/cleanup/`（2） |
| `file_size` 变 `int!`（约 14 处） | `test/cases/modules/{bytes_values,fs_binary_roundtrip}.cml`、`test/cases/para/core/`、`examples/ml/export_onnx.cml`、`examples/files/read_write.cml` |
| 模块清单与说明 | `docs/language/05_modules.md`、`docs/language/en/05_modules.md`（第 57 行的函数列表）、`docs/reference/camel-for-ai-stdlib-signatures.md`、`docs/technical/41_resources_and_definitions.md`、`test/plans/feat/compile/native_declarations.plan.toml`、`test/cases/compile/native/fs_*`、`test/cases/effects/native_clause.cml`（它自己声明 `module fs`，也要去掉 `module` 行） |

## 开放问题（原则没有定的）

1. **`read_lines` 的元素类型**。推荐 `string!..`（元素是 `string!`，失败给一个错误元素后流结束）。不用 `string..!`，因为那样打开文件的失败要在调用时就出现，违背"没被拉取的流不执行"（设计稿片 D 的验收：不存在的路径不拉取就不报错）。代价是 `for line in read_lines(p)` 里每行要 `!!`/`match`；若以后允许"流中途失败即终止循环"的写法再重议。
2. **写函数里路径作 `with`、内容作数据**。这是照 4.4 判据的结论；若负责人认为"路径是对等的目标"而要 `write_text(path, content)`，则改回两个普通参数（`merge(m1, m2)` 先例），C++ 内核不受影响。推荐保持 `with`。
3. **写时自动建父目录**。保留现行行为（`write_*`、`append_*` 都建），文档写明；更严格的做法是不建、要求先 `mkdirs`。推荐保留，理由：现有 25+ 处调用依赖它，且 `mkdirs` 幂等所以显式写也便宜。
4. **后续才加、本稿未加**：`rename`/`copy`、递归删除（`remove_all`，危险，需要单独裁定）、文件时间与权限、`read_chunks` 流（可以用 `unfold` 加 `read_chunk` 写成 Camel，用到再加）。
5. **错误种类没有集中目录**：`NotFound`/`PermissionDenied`/`IOError` 已在用，本稿新增 `AlreadyExists`。conventions 5 说"写在目录里"，但仓库里没有找到这个目录（`EncodingError` 只散见于 `modules/*`）；建议建一份并把 fs/json/time/io 用到的种类登记进去。
