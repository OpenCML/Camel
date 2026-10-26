# json 重写说明

稿件：`docs/handover/stdlib/json.cml`（声明）与 `docs/handover/stdlib/json.signatures.cml`（对应 `stdlib/signatures/json.cml`，只作参考）。未编译。

## 改了什么

1. **删 `module json`**（文件即模块）；删全部 `/// @pure`，原生声明改在签名里写 `pure`（原生算子必须写资源子句，否则按全局资源处理）。Camel 体的 `is_null` 不写子句（推出）。
2. **例子改成能编译的**：原先的例子用没有定义的 `j`（"In the examples below, `j` is a Json decoded so"），违反 conventions 7。现在每个例子自己 `decode` 出值，用 `!!`/`??` 收尾。
3. **`is_null` 改为 Camel 体**：`kind(j) == 'null'`，原子集合不留能由别的原子低成本组合出来的内核。
4. **访问器的四种形态保留**（conventions 2：`Json`、`Json?`、`Json!`、`Json!?` 各一条声明，共用 URI），这是已裁定写法；`get` 的 `key` 同名两种类型也保留。调用写 `get<key: 'a'>`，具名形式与 conventions 4 一致。
5. **`encode`/`decode_as` 仍在签名文件**（结果类型取自类型实参/值的类型，声明写不出，conventions 8），但改用新写法：
   - `decode_as<shape>(text)`（用值作形状）→ `decode_as::<T>(source)`（5.9 的显式类型实参；设计原则 11.2 与 5.9 都以 `decode_as::<Config>(text)` 为例），`text->decode_as::<Config>`、`let cfg: Config! = decode_as(text)` 都成立；
   - `encode(value)` 与 `encode(value, indent)` 两个元数 → 一个 `encode::<T>(value)` 加 `with <indent: int? = null>`：默认值替代按元数重载（4.4），`null` 表示一行，取代"负数表示一行"的哨兵值。`x->encode<indent: 2>`。
   - 两个 `decode_as` 的第一个参数统一叫 `source`（原来 `text` 却可以传 `Json`，名字不符）。
6. **种类不变**：`JsonParseError`、`JsonTooDeep`、`JsonTypeError`。

## 函数表

| 函数 | 处理 | 理由 |
|---|---|---|
| `decode` | 保留，`pure` | 坏文本是 `Json!`，深度限额 10000 不变（11.2） |
| `kind` | 保留 | 返回字符串。若以后 `enum` 能由原生算子返回，改成 `JsonKind` 枚举更合适（见开放问题 2） |
| `is_null` | 改为 Camel 体 | 见上 |
| `get`（`key: string`/`key: int`，各 4 形态） | 保留 | 已裁定 |
| `as_bool/int/float/string/array/object`（各 4 形态） | 保留 | 已裁定；`as_float` 接受整数 |
| `encode` | 改（签名文件）：`encode::<T>` + `indent: int? = null` | 见上 |
| `decode_as` | 改（签名文件）：`decode_as::<T>(source)` | 见上 |

## C++ 侧改动（`modules/json`）

- `is_null` 的内核与算子表项删除（保留则要加 `#impl`，不必）。
- 各访问器：纯度仍在 `OperatorTable::declare`，与签名里的 `pure` 一致。
- `encode`：注册的规则从 `(value)`、`(value, indent)` 两条改为一条带 `with indent: int?`；`indent` 为 null 时单行。`--verify-ops` 的参数名比较随之更新。
- `decode_as`：规则签名从 `<shape: T> (text: string | Json) => T!` 改为 `::<T>`；T 取自显式类型实参或期望类型（`from_bytes`/`load_npy` 那条路，2026-10-24 晚裁定里已有同类改法），不再读 `shape` 实参的值；`DecodeAsResolver` 里读形状值的部分删除。
- 原则 5.5 的远期方向：`encode`/`decode_as` 本该是 Camel 的"按类型展开"（`match type`/`static for`）加上 `Json` 的几个原子，内核不再读运行时的静态类型。数组元素与映射值的类型函数写法（取元素类型）还没有先例，所以本稿不写；待泛型按结构展开落地后再并入，签名文件随之删除。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `decode_as(text, shape)` / `decode_as<shape>(text)` → `decode_as::<T>(text)`（约 68 处提及） | `test/cases/modules/std/json/`（9 个文件）、`test/cases/compile/any_typing/`（7）、`test/cases/compile/param_defaults/`（2）、`test/cases/compile/native/`（3）、`test/plans/feat/modules/`（3）与 `golden/`（2）、`examples/files/json_config.cml`、`examples/data/json_roundtrip.cml`、`docs/language/`（3）、`docs/tutorial/`、`docs/technical/`、`docs/stdlib-reference.md`（生成） |
| `encode(v, n)` → `v->encode<indent: n>` | `examples/files/json_config.cml`、`test/cases/modules/std/json/decode_as.cml`、`test/cases/compile/native/one_module.mjs` |
| 访问器的 `get<'a'>` 写法不变；例子里 `is_null` 的使用 | `test/cases/modules/std/json/json_value.cml`（2 处） |
| 删除 `module json` 行 | `stdlib/json.cml`、`stdlib/signatures/json.cml` 本身 |

## 开放问题

1. **`encode` 对 NaN/±Inf**。现行行为是写成 `null`（库的默认）。JSON 没有这些值。推荐保持不动、文档写一句；若要严格，改为 `string!` 加 `JsonEncodeError`，会让每个 `encode` 调用都要处理，代价大于收益。
2. **`kind` 的返回类型**。推荐保持 `string`（六个固定字面量，`match` 可直接匹配）；换成 `enum JsonKind` 需要原生算子能返回 `enum`，现在没有先例。
3. **`encode`/`decode_as` 的最终形态**见上"C++ 侧"最后一条，不是现在要决定的事，只是登记方向。
