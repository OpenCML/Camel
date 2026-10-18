# 二进制值与文件接口实现

2026-10-18。语言作者已确认设计稿 T5：`read_chunk` 改为返回 `Bytes`，同步迁移调用处。

`Bytes` 是核心拥有的不可变名义类型。数据内联存放于 GC 对象中，保存完整长度，没有以 NUL 结尾的约定，也没有指向外部缓冲的悬垂引用。`bytes` 模块导出类型以及以下纯函数：

- `encode(string): Bytes`：验证并复制 UTF-8 文本。
- `decode(Bytes): string!`：严格检查 UTF-8；拒绝过长编码、代理码点、截断、错误续字节与超出 Unicode 范围的值，失败返回 `EncodingError`。
- `from_array(int[]): Bytes` / `to_array(Bytes): int[]`：逐字节显式转换，整数须在 0..255。
- `size(Bytes): int` / `concat(Bytes, Bytes): Bytes`：不解码、不修改输入。

文件接口分开处理文本和二进制：

- `read_bytes` / `write_bytes` 处理 `Bytes`；`read_chunk` 现在返回 `Bytes`。
- `read_text` / `write_text` 检查 UTF-8，错误提示改用二进制接口；`try_read_text` 在解码失败时返回 `EncodingError`。合法的 UTF-8 仍可含 NUL，按长度读写。
- `replace(path, bytes)` 使用同目录独占创建的临时文件。Linux 写完后 `fsync` 文件、关闭、改名并 `fsync` 目录；失败清理本次临时文件。Windows 使用 `FlushFileBuffers` 与带替换和写穿标志的 `MoveFileExW`，该平台分支本轮未执行。
- `crypto::sha256` 新增 `Bytes` 重载。块哈希调用先计算一次二进制 SHA-256，再对十六进制文本执行剩余轮次，保持原来的总轮数。

已迁移仓库中的分块文件哈希、文件示例、二进制回归用例，以及 DDPM 的 IDX/RGB 输入和 PGM/PPM 输出。文本签名和生成的标准库参考文档同时更新。

服务器手动验证：8 字节含 NUL/高位字节的往返与分块读取保持原值；SHA-256 为 `26aa5dd8eb311cb23d596c9ecb8cd1d959e3215033223177ee68a40f3ac4e2f0`；中英文与含 NUL 的合法 UTF-8 能显式往返；非法 UTF-8 解码返回错误；文本读取器拒绝二进制；原子替换与空文件替换成功。FVM 上启用分代 GC 校验和 minor safepoint stress 后，相同二进制用例也通过。

尚待第一阶段其余工作：张量与 Bytes 的小端互转、原生 `save_tree` / `load_tree`，以及旧 `str::from_bytes` 等可构造非 UTF-8 字符串的接口的全面收口。本轮没有将这些剩余能力标为完成。
