# 一个 URI 有好几个名字时，必须问"哪一个是程序能写出来的那个"

## 五处，一行

| # | 一件事 | 被当成事实源的名字 | 程序能写出来的名字 |
|---|---|---|---|
| 1 | `string(x)` 的转换 | 每个类型的 kernel（`:op/dtos`、`:op/itos`…） | `string(x)` |
| 2 | 那 8 条 kernel 的声明 | 通配 `#op(':op/*tos')`（只在前奏这一支是模式） | 一个声明、一条泛型规则 |
| 3 | 一个算子有几个 kernel | URI 的**条数**（打印器按条数认得它） | 算子的身份 |
| 4 | `printRuleUris` | 每个 URI 各占一行 | 一条规则 |
| 5 | **`+` 这类可重载算子** | **组名 `__add__`（声明的名字）** | **符号 `+`** |

第 5 处是今晚最后发现的一条，形状与前四条相同：**符号名（`+`）与组名（`__add__`）是同一件事的两个名字，打印器拿了后者。**

## 它是怎么暴露的

我按裁定并了 `origin/develop`，把打印器取上游的 `callFormOf(uri)` 版（那张表把三处"顺藤摸瓜认名字"合成了表项，答的是"这条调用有没有可写的形状、形状叫什么"）。构建后两条红：

```
[FAIL] cml_var_holding_a_lambda   output does not contain 'h = (x: int): int => x + 100'
[FAIL] stream.interval_loop_cml
[stdout] return (x: int): int => __add__(x, n)
[stdout] let g = (x: int): int => __mul__(x, 3)
```

**不是上游的错，是我的声明暴露了它**：P1-13 给 `+`/`-`/`*`/`/` 等写的那批声明让每条规则有了调用形状，而这些形状的名字是组名，打印器就把 `x + n` 印成了 `__add__(x, n)`——**一段程序读不回来的文本**。

## 试过的一条修法，按判据停手

在 `writable` 之前加一条：**有符号就先印符号**，`operatorSymbolOf(groupOf(uri))` 非空就不走 `callForm`：

```
const char *symbol = camel::core::operatorSymbolOf(
    camel::core::OperatorTable::instance().groupOf(uri));
const bool writable = symbol == nullptr && callForm.has_value() && …;
```

全量 **`8389/8371/5`**（原本 2 条红，变成 5 条）。**按"② 靠声明名区分的情形若坏了就停手写页"的判据，已撤回**，树停在 `7f74bdb71`（2 条红）。

**为什么"符号优先"不够**：`operatorSymbolOf(group)` 对**每个**可重载组都回一个符号，而打印器需要的是"**这一段程序里能写出来的那个名字**"——有的调用点只能写声明名（模块导出的调用形状），有的必须写符号，**判据不在组上，在调用点上**。所以这不是"加一条 if"，是"**印哪个名字**"要做成一条规则的一部分：**符号是语言的，声明名是模块的，二者不能用一个判据覆盖。** 这一句就是作者要裁的：

> **当一条算子同时有符号名与声明名时，打印器按什么选？** 默认建议：**能写符号就写符号**（符号是语言的一部分，任何程序都认），**符号不存在时才用声明名**——但"能写符号"要按**调用位置**判定，不是按组。

## 试过的第三条规则：名字与组名同名 ⇒ 印符号

按"名字是派生的还是写下来的"再试一次：

```
const bool derivedFromGroup = callForm.has_value() && callForm->name == group &&
                              operatorSymbolOf(group) != nullptr;
const bool writable = … && !derivedFromGroup;      // 派生名让位给符号
```

全量 **`8389/8371/5`**——红的从 2 条变 5 条（`interval_bounds_constant.opt_cml`、`stream.interval_loop_cml`、`canonical_document_and_equivalence`、`print_and_recompile`、`cml_var_holding_a_lambda`）。**这条区分不了，第四版不再试。**

**结论**：`name == groupOf(uri)` 与 `operatorSymbolOf(group) != nullptr` **不足以判断"该印哪个名字"**。需要的是**第三种事实**：不是组，也不是名字的来源，而是**这个调用点所在的这一段程序，在它可见的名字空间里能不能写出那个符号**（`+` 在语言每一处都写得出；模块导出的调用形状只在自己作用域里写得出）。这一条请作者裁。

**表格（能拿的两列是实测，另两列是读代码，别当实测）**

| 用例 | uri | groupOf | operatorSymbolOf | callFormOf 的 name | 源程序实际写的 |
|---|---|---|---|---|---|
| `cml_var_holding_a_lambda` | `:op/add_*` | `__add__` | `+` | `__add__` | `x + n` |
| `stream.interval_loop_cml` | `:op/mul_*` | `__mul__` | `*` | `__mul__` | `x * 3` |

前两行来自红里的 stdout（`__add__(x, n)`、`__mul__(x, 3)`），后两列按代码读出。`declaredByModule` 一列要一次专门的探针程序才拿得到，**没做**。

## 三次失败各一句

1. **在组上判**（有符号就印符号）：`operatorSymbolOf(group)` 对每个可重载组都非空，**组这个事实太宽**，连该印声明名的调用点一起翻了过去（2 红 → 5 红）；
2. **判名字的来源**（名字与组名同名 ⇒ 印符号）：同名只说明名字是派生的，**不说明这个调用点写不写得出来**（2 红 → 5 红）；
3. **判返回值**（`name == group` 且符号非空）：同上，只是换了个写法。

## 第四版：按**所有权**判，成立

> **`:op/` 是语言自己的算子命名空间**：那里的每个组的符号由语言固定，程序在任何地方都写得出来 ⇒ **印符号**。
> **别的命名空间（模块、用户）**：名字是那个模块**导出**的拼写 ⇒ **印声明名**。

```
const bool languageOwned = uri.starts_with(":op/") &&
    operatorSymbolOf(OperatorTable::instance().groupOf(uri)) != nullptr;
const bool writable = !languageOwned && callForm.has_value() && … ;
…
} else if (callForm.has_value() && callForm->kernelDeclaration && !languageOwned) {
```

第二处 `&& !languageOwned` 是修法的一半：语言拥有的名字**不能**落进"body-less kernel 声明 ⇒ 拒绝"那一支（我 P1-13 写的声明正是 body-less），要让位给下面的符号分支。第一版只改 `writable`，五条红全变成"拒绝打印"。

**读数**：全量 **`8389/8376/0`**，五条红全绿、无新红；`print_and_recompile` 与新增的 `string_conversion` 三层仍过。

**问句与代价**（作者若另有裁，按此回看）：

- **A 按所有权判**（已落）：改法两行、判据可读；**代价**是"语言自己的命名空间"这个边界将来若扩大（例如某个 `:xxx/` 前缀也变成语言拥有），要回来看这一行；
- **B 按调用点可见性判**：更普遍，但**打印器今天不掌握作用域信息**，要加机制。

## 现状

- `origin/lane-gs6` 尖端：全量 **`8389/8376/0`**；
- `inspect --ops` 648、`--verify-ops` 552、三栏 480/22/146。

**规矩（今天第二次踩）**：**每题推完立刻 `git branch -r --contains <sha>` 确认；"我提交了" ≠ "它可被合并"。**

`等点名`。