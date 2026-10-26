# hash 重写说明

稿件：`docs/handover/stdlib/hash.cml`。未编译。

## 改了什么

- 删 `module hash`，删 `/// @pure` 与两个 `#op(...)`。
- **摘要只在字节上算**：`string` 是文本，它的摘要就是它的 UTF-8 字节的摘要（11.1）。原来 `sha256(string)` 与 `sha256(Bytes)` 是两个算子（`hash:sha256_str`、`hash:sha256_bytes`），现在只剩一个内核 `sha256_bytes`（私有，无 `export`，签名里写 `pure`），公开的两个重载都是一行 Camel：`sha256(Bytes)` 直接调它，`sha256(string)` 先 `encode`。调用方的写法不变：`'abc'->sha256` 与 `encode('abc')->sha256` 仍然都对。
- **`sha256_rounds` 删除**。它是"把 SHA-256 反复套用 N 轮"，唯一用途是给测试造一个耗 CPU 的纯算子（`test/cases/para/core/file_chunk_hash.cml` 用它测并行）；不是任何人会用的哈希接口，也不是标准的 KDF。需要时在用例里用 Camel 循环写：`loop h = s, i = 0 while i < n do (sha256(h), i + 1)`。留在标准库里等于把测试夹具当成范本。
- 子句：私有内核写 `pure`（原生算子必须写）；两个公开重载是 Camel 体，不写子句（推出为纯）。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `sha256(data: Bytes): string` | 改为 Camel 体 | 转到私有内核 |
| `sha256(text: string): string` | 改为 Camel 体 | `sha256(encode(text))` |
| `sha256_bytes`（私有内核） | 由 `hash:sha256_bytes` 保留为私有声明 | 唯一的原子 |
| `hash:sha256_str` 算子 | **删除** | 被上面覆盖 |
| `sha256_rounds` | **删除** | 测试夹具 |

## C++ 侧改动（`modules/hash`）

- 删 `__hash_sha256_str__`、`__hash_sha256_rounds__` 及算子表、`OperatorTable::declare` 项。
- `sha256_bytes` 内核不变。性能：字符串先 `encode` 多一次拷贝；SHA-256 的计算量远大于拷贝，不配 `#impl`。若量到明显差别，再给 `sha256(string)` 配原生内核（10.4）。
- 不需要 `stdlib/signatures/hash.cml`。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `sha256(...)` 写法不变（约 36 处无需改） | — |
| `sha256_rounds(s, n)` 删除（10 处） | `test/cases/para/core/file_chunk_hash.cml`（在用例里就地定义一个 Camel 版本）、`test/cases/modules/std/hash/basic.cml`、`test/cases/modules/rename/hash_{identity,new}.cml`、`test/cases/compile/native/hash_declaration_import.cml`、`test/plans/feat/compile/native_declarations.plan.toml`（相应的期望）、`docs/language/05_modules.md` 与 `en/`、`docs/reference/camel-for-ai-stdlib-signatures.md` |
| 算子 URI `hash:sha256_str` 的引用 | `modules/hash/*`、`test/cases/compile/native/hash_*` 与 `stdlib_registry.plan.toml` 里对算子名的断言 |

## 开放问题

1. **摘要的类型**。现在返回 64 位小写十六进制 `string`。摘要的本质是 32 个字节，十六进制是它的文本形式；推荐保持 `string`（最常用，且 `bytes` 模块目前没有 hex 转换），将来 `bytes` 有了 `to_hex` 之后，是否再提供返回 `Bytes` 的形式，由 `bytes` 的重写者定。
2. **其他算法**（MD5、SHA-1、SHA-512、HMAC）与**增量哈希**（大文件边读边算，对应 `fs::read_chunk`/流）：本稿不加。流落地后，增量哈希应该是对 `Bytes..` 的折叠，不需要新的原子之外的东西；等有需求再设计。
3. **依赖 `bytes` 的 `encode`**：另一位在重写 `bytes`/`str` 的同事若改了 `encode`/`decode` 的名字，`hash.cml` 与 `fs.cml` 的 `import { encode, decode } from bytes` 要同步。
