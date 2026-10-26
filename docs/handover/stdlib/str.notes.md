# str 重写说明

文件：`str.cml`。依据：设计原则 4.4、5.5、10.4、11.1，`stdlib-conventions` 6.5 与 7。

## 函数表

| 名字 | 处理 | 理由 |
|---|---|---|
| `substr` | 删除 | 切割改用核心的字符串下标 `s[a:b]`（码点下标，规则同数组：终点不含、起点在终点后得空、越界是故障）。`str` 不提供函数形式，也不留别名；所有体都写 `s[i:j]` |
| `codepoints`、`from_codepoints` | 保留（原子） | 码点当数字，只有 C++ 能给；`from_codepoints` 的 `string!`（EncodingError）不变 |
| `to_upper`、`to_lower` | 保留（原子），文档改写 | 大小写靠 Unicode 数据，属于外部数据。已裁定做完整 Unicode 简单（一对一）大小写映射 |
| `find` | 改名 `index_of`，改为 Camel 体 + `#impl` | 与 `array::find<pred>`（返回元素）重名异义；"第一次出现的下标，没有则 null"正是 `array::index_of`。体是 `loop` + 下标切片比较，`#impl(index_of_kernel)` 指向原 `str:find` 内核（热路径） |
| `split` | 保留，改为 Camel 体 + `#impl(split_kernel)` | 体是 `if n == 0 then 按码点切 else cut(s, sep)` 一个表达式，`cut` 是私有的 `loop`。**语义变化**：空分隔符不再是故障，而是切成码点（Go 的 `Split(s, "")` 同），已裁定保持全函数 |
| `replace` | 保留，改为一行 `s->split<old>->join<new>`；with 参数 `from_`、`to` 改名 `old`、`new` | `from_` 是为避开关键字 `from` 的凑合写法；原内核删除。空 `old` 不再是故障，`new` 插在码点之间 |
| `trim`、`trim_start`、`trim_end` | 保留，改为 Camel 体，内核删除 | 一个 `loop` 找边界 + 下标切片；私有 `is_space` 用 `contains` 判断 ASCII 空白。仍只修剪 ASCII 空白（原裁定），见问题 3 |
| `starts_with`、`ends_with` | 保留，改为一行体，内核删除 | `len` 比较 + 下标切片比较，靠 `&&` 的短路 |
| `repeat` | 保留，改为 `[s for i in 0..count]->join<''>`，内核删除 | **语义变化**：count 不为正时得空串，不再是故障（与 `array::take` 对 n 不为正的处理一致）。结果过长仍由字符串长度上限在连接时报故障 |
| `strip_prefix`、`strip_suffix` | 新增 | 去前缀/后缀，不存在则 `null`（`T?`，不用哨兵）。解析协议头、扩展名的常用写法，体各一行 |
| `lines` | 新增 | 按 `\n`、`\r\n` 切行，末尾换行不多出空行。`examples/text/log_filter.cml` 一类代码的常用起手式 |
| `is_space`（私有） | 新增 | 只供 trim 用，不导出 |

未新增：`chars`（`s->split<''>` 即是）、`count`、`pad`（`format` 的 `{:<5}` 已覆盖）、`contains`/`join`/`len`/`format`/`parse_*`（核心内建）。

## `pure`、`@pure` 与 `#impl`

- 全文删除 `/// @pure`。没有写任何 `pure`：没有需要对外承诺的函数，也没有要求回调为纯的参数（str 里没有回调）。原子的纯度仍在 C++ 的 `OperatorTable::declare` 里（见下）。
- 两处 `#impl`（`index_of`、`split`）：搜索与切分是文本处理最热的两处，Camel 体每次探测都要分配一个切片，内核是 O(n)。依据 10.4"常用而热的定义带原生内核"。其余内核（trim 三个、`starts_with`、`ends_with`、`repeat`、`replace`）按"测量表明更慢再配"不保留。若决策者倾向"先全部不配"，这两个内核也可删，体不变。

## C++ 侧改动（modules/str）

- `operators.cpp`、`executor.cpp`、`module.cpp` 的 `declare` 表：
  - 删除：`trim`、`trim_start`、`trim_end`、`replace`、`starts_with`、`ends_with`、`repeat` 的内核与登记。
  - 删除：`substr`（改由核心下标提供）。
  - `to_upper`、`to_lower` 内核换成完整 Unicode 简单大小写映射（带表；`ß` 之类一对多不做）。
  - **核心**（不在 `modules/str`）：字符串上的范围下标 `s[a:b]`（含省略端 `s[:b]`、`s[a:]`），按码点；规则同数组下标（终点不含，起点在终点后得空串，`a`、`b` 不在 `0..len(s)` 是故障 `IndexOutOfRange`，不支持负数下标）。
  - 保留：`find`（私有内核 `index_of_kernel`，URI 仍是 `str:find`）、`split`（私有内核 `split_kernel`，URI `str:split`）、`codepoints`、`from_codepoints`、`to_upper`、`to_lower`。
  - `split` 内核改语义：空分隔符不再 `InvalidArgument`，按码点切（空串得空数组），与体一致，差分测试才会过。
  - `#op('str:find')`、`#op('str:split')` 写在私有内核声明上（名字与 Camel 名不同）；`codepoints` 等导出声明的默认 URI 就是 `str:<名字>`。
- 故障种类 `InvalidArgument` 在 str 里不再使用；`docs/diagnostics.md` 的引用处不用动（种类仍被别的模块使用）。

## 需要迁移的调用点

| 变化 | 位置 |
|---|---|
| `substr<a, n>` → `s[a:a + n]` | `test/cases/modules/stdlib/{fault_substr_length,fault_substr_negative,fault_substr_start,str_chars,str_unicode,utf8_string}.cml`（6）、`examples/text/log_filter.cml`、`docs/language/03_data_model.md` 与 `docs/language/en/03_data_model.md`、`test/plans/feat/modules/{stdlib_faults,stdlib_values}.plan.toml` 与 `golden/stdlib/str_unicode.txt`。`fault_substr_negative`（长度为负）不再有对应故障，改成 `s[a:b]` 的越界用例；`fault_substr_length`、`fault_substr_start` 随新边界重写。`docs/stdlib-reference.md`、`docs/reference/camel-for-ai-stdlib-signatures.md` 由生成器重生成 |
| `find` → `index_of` | `test/cases/compile/native/str_declaration_import.cml`、`test/cases/modules/std/fs/remove.cml`、`test/cases/modules/stdlib/{str_unicode,utf8_string}.cml`、`test/cases/parse/rulings/{import_rename,import_rename_hides}.cml`（用 `find as str_find` 测改名导入，改成 `index_of as str_find` 即可）、`examples/text/log_filter.cml`、`docs/tutorial/examples/12_stdlib.cml`，以及 `tutorial.cn.md/en.md`、`docs/language/05_modules.md`（+`en/`）、设计原则里提到 `str::find` 的几处（grep `str::find`） |
| `split<''>`、`replace<'', …>`、`repeat<负数>` 的故障用例 | `test/cases/modules/stdlib/{fault_split_empty,fault_replace_empty,fault_repeat_negative}.cml` 及其在两个 plan 里的期望：改为值用例（`fault_repeat_huge` 保留，仍是长度上限的故障） |
| `replace` 的具名实参 `from_:`/`to:` | grep 没有调用点用具名写法，无需迁移 |
| `docs/language/03_data_model.md` 的 str 函数清单 | `substr` 删除（改写为 `s[a:b]`），增列 `index_of`、`strip_prefix`、`strip_suffix`、`lines`；`en/` 同 |

## 已裁定（决策者）

字符串切片进核心下标；大小写做完整 Unicode；空分隔符与非正 `repeat` 保持全函数；`trim` 仍只修剪 ASCII 空白（沿用原裁定，只改私有 `is_space` 即可放宽）。

## 仍需注意

- `docs/stdlib-reference.md` 里 `len(x: string)` 的说明写的是 "number of bytes"，与 11.1（按码点）不符，是核心文档缺陷，不在本模块，只提醒。

## 草稿里没有验证的写法（没有工具链，请审阅时留意）

- `[s for i in 0..count]` 中 `i` 未使用，若有"未使用变量"警告需要换写法。
- `loop … do if … then (…) else (…)` 的换行写法（`then`/`else` 在行首续行）。
