# 两张词表按位置对应，加词必须两处同改

**本页只说明一件事：资源修饰词为什么是"两处"而不是"一处"，以及为什么没加那道编译期断言。**

## 现在的形状

```
src/parse/include/camel/parse/ast/resource_modifiers.h
inline constexpr auto kResourceModifierWords = std::to_array<std::string_view>({
    "unordered", "idempotent", "overwriting", "readonly", "observed",
});

src/kernel/core/resource_semantics.cpp
constexpr auto kModifierRows = std::to_array<ModifierRow>({
    {"unordered", &ResourceModifiers::unordered},
    ...
});
```

解析层这张表是"程序里能写哪些词"的唯一清单；内核那张表是"语义记录按哪个名字读哪个字段"的唯一清单。两者的**位置**一一对应：`ResourceDeclLoad` 的第 i 个位、两个打印器的第 i 个词、`ResourceModifiers::hasModifier(i)`，以及 `gct/builder.cpp` 里那条把树填进记录的单行循环，全都只用下标 `i`，谁也不拼名字、谁也不再排一次序。

## 为什么没加 `static_assert`

想过一行搞定的写法：`static_assert(std::size(kModifierRows) == std::size(ast::kResourceModifierWords));`。加不了，原因是依赖方向：

- 解析层依赖内核，不是反过来 —— `parse/ast/stmt.h` 已经 `#include "camel/core/operator_semantics.h"`；内核源码里没有任何一处 include 解析层的头，内核目标也不带解析层的源。
- 要让内核看见 `kResourceModifierWords`（或反过来让解析层看见 `kModifierRows`），就得在这条边上新开一个方向，或者把内核那张表搬进公共头。为了防一次"加词忘一边"，引入一条真依赖，不划算。

所以护栏改成两条便宜且不需要新依赖的：

1. `hasModifier(index)` / `setModifier(index, written)` 都带越界判断（越界读为 `false`、写为无操作）——只在一侧加词时不会读到垃圾位、不会 UB；
2. 本页把"加词要改哪三行"写成动作。

## 动作：加一个修饰词

① `parse/.../resource_modifiers.h` 表里加一行词；② `kernel/.../resource_semantics.h` 的记录加一个 `bool` 字段；③ `kernel/core/resource_semantics.cpp` 的行表加一行 `{词, &ResourceModifiers::字段}`。**没有第四处**：parser 的接受循环、CST→AST 映射、`toString`、`geneCode`、AST→内核映射、内核的 `any()` 与 `toString` 都是遍历表。语法产生式那条注释要跟着写一行（规格必须点名它接受什么），`scripts/context-keywords.mjs` 的 evidence 指向的是循环那一行，不用改。

## 忘了会怎样

只改解析侧：程序能写这个新词，树的位也置上了，但内核记录那一列不存在 ⇒ `hasModifier(i)` 越界返回 `false`，语义上这个词**静默消失**：资源身份文本与 C++ 侧注册值不一致时，报告说的还是老样子，而打印器（读解析表）却会印出这个词 —— 两处就矛盾了。只改内核侧：词写不进程序（解析器不接受），内核那一行是死行。

读数：`camel inspect --ast` 的五词用例（`test/cases/effects/resource_modifier_words.cml`）与 `parse.constructs.resource_modifier_in_ast` 只看打印，能抓"打印与记录不一致"的一半；抓不到的是"两边都少一个词"——那只能靠这页的动作。