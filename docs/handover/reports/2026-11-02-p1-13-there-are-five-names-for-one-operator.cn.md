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

## 现状

- `origin/lane-gs6` = `7f74bdb71`，全量 **`8389/8374/2`**（`cml_var_holding_a_lambda`、`stream.interval_loop_cml`）；
- `inspect --ops` 648、`--verify-ops` 552、三栏 480/22/146；
- 我的符号尝试已撤回，工作树干净；日志在 `D:\Temp\gs6-b17.log`。

**规矩（今天第二次踩）**：**每题推完立刻 `git branch -r --contains <sha>` 确认；"我提交了" ≠ "它可被合并"。**

`等点名`。