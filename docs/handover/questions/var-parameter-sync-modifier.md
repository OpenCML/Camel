# `var` 参数要哪个 `sync`：声明的修饰符，还是函数体

Author: 车道 `handover/h435-learnability`（`D:\Projects\Camel-wt\h435-learnability`），2026-10-24。Status: open — 需要一句裁定：放行 `var` 形参的条件是"声明带 `sync` 修饰符"还是"函数体是 sync 块"；若是前者，`VarParamInAsyncFunction` 的名字与消息是否跟着改。测量在 Linux 主机（`88zjwei`）上、基线 `handover/h425-ai-reference` 的构建（`build-info.json` 的 revision 为 `2e6438492`，dirty=false）上完成。

## 数字先

四种写法，全部实测（`camel <file>`，读 `--output-format json` 之外的文本诊断）：

| 声明 | 函数体 | 诊断 |
|---|---|---|
| `sync func bump(var x: int): int sync { wait x += 1; return x }` | sync 块 | **无**（exit 0，`camel check` exit 0，运行输出 `4 4`） |
| `func bump(var x: int): int sync { wait x += 1; return x }` | sync 块 | `SemanticDiag::VarParamInAsyncFunction`（W） |
| `sync func bump(var x: int): int { return x + 1 }` | 非 sync | **无** |
| `func bump(var x: int): int { x = x + 1; return x }` | 非 sync | `SemanticDiag::VarParamInAsyncFunction`（W）+ `SemanticDiag::IgnoredSideEffect` |

- 关键读数在第 2、3 行：**两行的函数体都是 sync 块，只有第 2 行报警告**；第 3 行函数体不是 sync 块，却因为带了 `func` 前的 `sync` 修饰符而**没有**警告。也就是说这条检查读的是**修饰符**，不是函数体。
- 诊断模板（`diagnostics/registry.tsv`，逐字）：`Variable parameters are only allowed in sync functions.` 名字里没有 "modifier" 的字样，字面意思指向函数体——与读数相反。
- 参考文档 §7 已经写明"`sync` 写在 `func` 之前只是修饰符，不排序任何东西"，读者据此会以为两种 `sync` 等价；这条实测正好落在两者的差别上。

## 我这一轮的做法（照实测写，不照设计猜）

`docs/reference/camel-for-ai.md` §12.13 原来教的是：

```camel
func bump(var x: int): int {
    x = x + 1
    return x
}
```

这不是笔误而是**会警告的写法**（上表第 4 行），而且 `x = x + 1` 不在 sync 块里还带 `IgnoredSideEffect`。现在改成先写清规则、再给无警告的例子：

> The `var` is allowed only where the declaration carries the **`sync` modifier**, the `sync` written before `func`. A sync body does not count: `func bump(var x: int): int sync { ... }` still warns `SemanticDiag::VarParamInAsyncFunction`.

例子逐字为上表第 1 行（`sync func bump(var x: int): int sync`），实测 exit 0、`camel check` exit 0、无任何诊断。

我没有把这个判断写进 `docs/language/**`，也没有改 `diagnostics/registry.tsv`；参考文档只是把可复现的读数写出来。

## 两条路（都需要一句裁定）

- **(a) 只认修饰符**（今天的事实）：把 `VarParamInAsyncFunction` 的消息改成"带 `sync` 修饰符的声明"，或至少补一句说明。一行消息，参考文档不用再改。
- **(b) 函数体也算数**：`func bump(var x: int): int sync` 不应报警告。那是编译器的改动，超出本车道范围（本车道不改编译器），需要另开车道。

建议 (a)：消息与实测一致，代价最小；但这条是语义裁定，按 AGENTS.md"未决项问作者"，本车道只记录读数。

## 我没有做

没改编译器、`diagnostics/registry.tsv`、`docs/language/**`；没给这条诊断加检查（加检查会锁死一个还没裁定的行为）；没有在参考文档里写"函数体 sync 应当也算数"这种猜测。