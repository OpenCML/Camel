# lambda 形参括号：本 lane 落地的范围与三处没定住的

裁决（`docs/handover/reviews/questions-2026-10-24-evening.md`，§`composition-expected-type-edges`
第 26 行，2026-10-24 夜）：**lambda 的形参要写括号**，`x => e` 不是 Camel 的写法，`(x) => e` 才是；
规则体里 `x => x` 会和规则的 `=>` 撞上，所以不加无括号的写法（写法面见
`docs/design-principles.cn.md` §5.9，第 408 行）。

本 lane 落地的是：名字直接站在一个落不下去的 `=>` 之前时，报新码
`SyntaxDiag::LambdaParametersNeedParentheses`（`0x02000012`，注册表 `diagnostics/registry.tsv`，
判定点在 `src/parse/syntax/report.cpp` 的修复分支里），消息逐字：

```
A lambda's parameters need their parentheses: write '(x) => e', not 'x => e'.
```

实测接管的位置（都报在 `=>` 上）：`let f = x => x + 1`（4:15）、`return x => x + 1`（3:14）、
裸语句（3:7）、`let f: (x: int) => int = x => x + 1`（3:32）、match 臂体（4:21）、
`[x => 1]`（3:23）、`{ f: x => 1 }`（3:20）、`[1, 2]->map<x => x + 1>`（3:27）、
`return x => x`（3:14）。只有 `x, y => x + y`（3:18）虽然报同一个码，**不给 fix**：`x, y`
与调用实参（`f(a, x => e)`）在箭头前同形，包错范围就是替作者改语义。

## 一、带类型的无括号形参 `x: int => e`：仍然不是这个码（实测）

`q10.cml:3:14`：

```
error[SyntaxDiag::UnexpectedToken]: Unexpected ':'; expected '}'.
  --> q10.cml:3:14
   |
 3 |     let f = x: int => x + 1
   |              ^
```

裁决说的是「形参要写括号」，这一形是「名字 + `:` + 类型 + `=>`」；判定点上的落点 token 是 `:`，
不是 `=>`，所以新分支接不住它。要接住就得在 `:` 之后再往前看一个类型和一个 `=>`，本 lane 没有做
（只做到裁决逐字的那一形）。**问题**：这一形要不要也归到同一个码下？

## 二、无括号形参表 `x, y => e`：报码但不给 fix

`q8.cml:3:18` 报 `SyntaxDiag::LambdaParametersNeedParentheses`，但没有 `data.fixes`
（见上面「与调用实参同形」）。**问题**：需不需要在「箭头前那一串名字不在调用括号里」的上下文
（例如 `let f = x, y => e`）里也给一个 fix？本 lane 的做法是宁可只给句子。

## 三、`sync x => e`：报的是另一个拼写问题（实测，未改动）

`q11.cml:4:18`：

```
error[SyntaxDiag::UnexpectedToken]: Unexpected 'x'; expected '('.
  --> q11.cml:4:18
   |
 4 |     let f = sync x => x + 1
   |                  ^
```

`sync` 后面本来就要求 `(`，这一形在到达箭头之前就被拒了。本 lane 没有动它。

## 四、顺便记下的两条读数（不是问题，是回归）

* 规则体 `x => x` 本来就正确：`rule r(x: int) { x => x }` 编过（`RUN_EXIT=0`，只有
  `SemanticDiag::EffectNotExecuted` 警告），本 lane 一个字没动规则那一侧。
* 括号形与组合等价：`ident..ident` 与 `(x) => ident(ident(x))` 在两个位置（声明值、函数实参）
  上的四个读数一致（`test/cases/compile/stream/comp_lambda_equivalence.cml`，h428/h429 的锁）。