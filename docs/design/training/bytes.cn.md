# 二进制值与文件接口实现

2026-10-18。语言作者已确认设计稿 T5：`read_chunk` 改为返回 `Bytes`，同步迁移调用处。

> 2026-10-22 注：第 29 行记的 `str::from_bytes` 已删除；字符串与 UTF-8 字节之间现在只用
> `bytes::encode`/`bytes::decode`（非法 UTF-8 是错误值）。本文其余内容是 2026-10-18/19 的记录。

`Bytes` 是核心拥有的不可变名义类型。数据内联存放于 GC 对象中，保存完整长度，没有以 NUL 结尾的约定，也没有指向外部缓冲的悬垂引用。`bytes` 模块导出类型以及以下纯函数：

- `encode(string): Bytes`：验证并复制 UTF-8 文本。
- `decode(Bytes): string!`：严格检查 UTF-8；拒绝过长编码、代理码点、截断、错误续字节与超出 Unicode 范围的值，失败返回 `EncodingError`。
- `from_array(int[]): Bytes` / `to_array(Bytes): int[]`：逐字节显式转换，整数须在 0..255。
- `size(Bytes): int` / `concat(Bytes, Bytes): Bytes`：不解码、不修改输入。

文件接口分开处理文本和二进制：

- `read_bytes` / `write_bytes` 处理 `Bytes`；`read_chunk` 现在返回 `Bytes`。
- `read_text` / `write_text` 检查 UTF-8，错误提示改用二进制接口；`try_read_text` 在解码失败时返回 `EncodingError`。合法的 UTF-8 仍可含 NUL，按长度读写。
- `replace(path, bytes)` 使用同目录独占创建的临时文件。Linux 写完后 `fsync` 文件、关闭、改名并 `fsync` 目录；失败清理本次临时文件。Windows 使用 `FlushFileBuffers` 与带替换和写穿标志的 `MoveFileExW`，该平台分支本轮未执行。
- `hash::sha256` 新增 `Bytes` 重载。块哈希调用先计算一次二进制 SHA-256，再对十六进制文本执行剩余轮次，保持原来的总轮数。

已迁移仓库中的分块文件哈希、文件示例、二进制回归用例，以及 DDPM 的 IDX/RGB 输入和 PGM/PPM 输出。文本签名和生成的标准库参考文档同时更新。

服务器手动验证：8 字节含 NUL/高位字节的往返与分块读取保持原值；SHA-256 为 `26aa5dd8eb311cb23d596c9ecb8cd1d959e3215033223177ee68a40f3ac4e2f0`；中英文与含 NUL 的合法 UTF-8 能显式往返；非法 UTF-8 解码返回错误；文本读取器拒绝二进制；原子替换与空文件替换成功。FVM 上启用分代 GC 校验和 minor safepoint stress 后，相同二进制用例也通过。

2026-10-19 补充：`tensor::to_bytes(t)` 提供纯的、按行优先的小端 payload 导出，不包含形状或文件头。直接复制存储位，不经过 double 转换，保留 f32 负零与 int64 的完整精度，支持 bool、空张量及共享视图。反向转换 `from_bytes::<f32/int/bool>(b, shape)` 已接入显式类型调用，并检查长度、负维度、乘积溢出及 bool 规范编码。也可在已知的 Tensor 期望类型下省略类型实参：`let t: Tensor<f32> = from_bytes(b, [4])`；不新增 dtype 字符串或参照树作为替代接口。

服务器独立运行时 `.camel-training-stage1-v1` 上手动执行 `binary_bytes.cml std::fvm`：`1.0/-2.5/+0/-0` 输出精确小端字节，`9007199254740993/-2` 保留 int64 全部位，bool 为 `[1, 0, 1]`，空张量产生 0 字节，reshape 视图与原值字节一致。未替换正在用于 COCO 基线的项目默认运行时。

最终 `.camel-explicit-types-v6` 同时包含编译期类型调用与二进制解码。手动用例覆盖声明、普通形参和返回位置的 dtype 推断，以及带声明类型的解码器函数值。SNaN/QNaN payload、无穷、最小次正规数、负零和超出 double 精确范围的 int64 都逐字节保持；rank-0、空张量、bool 也通过。NVM/FVM/JIT 的结果相同；FVM 开启分代回收、minor safepoint stress 与收集前后校验仍保持结果。短 payload、bool 非规范值及尺寸乘积溢出给出明确运行期诊断。

尚待第一阶段其余工作：（2026-10-19 更新：`save_tree` / `load_tree` 已在 `tree` 模块；`string` 保证是合法的 UTF-8，`str::from_bytes([255])` 在调用处就是故障，`Bytes` 是不必导入的内建类型名，见设计原则 11.1。）第一阶段尚未验收完成。
