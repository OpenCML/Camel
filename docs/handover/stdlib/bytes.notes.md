# bytes 重写说明

文件：`bytes.cml`。依据：设计原则 10.4、10.5（数据块）、11.1。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `encode` | 保留（原子） | `string` → `Bytes` 的唯一入口（语言文档与十多个用例在用） |
| `decode` | 保留（原子） | 非法 UTF-8 是错误值 `string!`（来自外部世界），不是故障 |
| `from_array`、`to_array` | 保留（原子） | 数据块与数组之间的逐字节转换；元素越界 0..255 是程序错误，故障 |
| `size` | 删除 | `len(Bytes)` 并入核心的 `len` 组（URI 由核心定，不再有 `bytes:size`/`bytes:len`） |
| `concat` | 删除 | 由核心的 `a + b` 取代（`Bytes + Bytes`，同字符串） |
| `slice` | 不加 | 由核心的范围下标 `b[a:b]` 取代（同字符串） |

剩下的四个都是原子（转换）：`Bytes` 是数据块，要靠 C++ 才能读写，符合 10.4。没有写 `pure`：纯度在 C++ 登记，也没有回调参数。


## C++ 侧改动

- `modules/bytes/module.cpp`：删除 `sizeKernel`、`concatKernel` 及执行器表与 `declare` 表里的 `size`、`concat`。
- **核心**：`len` 组加 `len(x: Bytes): int`；`+` 加 `Bytes + Bytes`；范围下标 `b[a:b]` 加到 `Bytes`（与字符串、数组同规则，字节下标，越界是 `IndexOutOfRange`）。
- 其余内核不动。

## 需要迁移的调用点（`bytes::size` / `import { size } from bytes`，共 20 个文件，改为核心的 `len`，并删去对应的 import 项；`concat` 改为 `+`）

`test/cases/compile/native/{bytes_arity,bytes_type,foreign_operator}.cml`（另有 `bytes_tag`、`entry_uri`、`unselected_case_declaration` 三个里自带 `size` 的内联声明，改成 `len`）、`test/cases/modules/bytes_values.cml`、`test/cases/modules/optim/{dtype_exact,native_dtype_exact,written_dtype_exact}.cml`、`test/cases/modules/stdlib/{reduced_reduction_bytes,str_chars,str_unicode,utf8_string}.cml`、`test/cases/modules/tensor/{binary_bytes,binary_roundtrip,generic_signature_element_type}.cml`、`test/cases/modules/tensor/dtype/{bf16_modulo,bf16_storage,f64_exact,i32_exact,mask_written_conversion}.cml`、`docs/diagnostics.md`（引用处）。`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md`（生成）、`docs/design/training/bytes.cn.md` 里写 `size` 的地方改为 `len`。新增 `slice` 的值用例与越界故障用例。

## 已裁定（决策者）

`len(Bytes)` 进核心；`Bytes` 有 `+` 与 `b[a:b]`。

## 仍待确认

- **十六进制**：`to_hex`/`from_hex` 经常要用（`hash` 返回十六进制文本）。能用 `to_array` 加 `format` 写出，但我没有核实 `format` 是否支持 `{:02x}`，所以没加。推荐：确认后在本模块用 Camel 加上。
