# `match` 守卫：设计原则 4.4 与 2026-10-24 的裁定都写 `case p when c =>`，但解析器今天没有守卫

## 事实（同一命令 `camel <file> std::gir`，同一主机，`origin/develop` = `a6f4cfbcf` 的构建）

小程序的 `match` 里写 `case 0 when n == 0 => 1`，逐字：

```camel
func guard(n: int): int {
    return match n {
        case 0 when n == 0 => 1
        case _ => 2
    }
}
```

```
error[SyntaxDiag::MissingToken]: Expected '=>' before 'when'.
   --> func_and_match.cml:17:16
    |
 17 |         case 0 when n == 0 => 1
    |                ^^^^
EXIT=1
```

同一窗口、同一命令下，规则与函数的 `when` 位置都是能过的（`rule f(x: int) { … } when c` 今天 EXIT=0；函数把 `when` 单独一行写在签名与体之间也 EXIT=0），所以这一条不是"位置"的问题：

- 产生式只有 `matchCase : 'case' pattern ('|' pattern)* '=>' blockExpr`（`src/parse/syntax/parser_expr.cpp`，`parseMatchCase`）。
- CST（`node_kinds.def`）、AST、GIR 里都没有守卫节点，运行时也没有"守卫为假则试下一条"的语义。

位置只是这个功能的一部分：定位置等于同时定下"守卫假 → 试下一个 case"、"守卫在哪个作用域求值"、"`case null` 与守卫的先后"。本车道（H413 `when` 位置）不替它定这些。

## 本车道落了什么、没落什么

- 落：规则的尾随 `when` 换成了具名诊断 `SyntaxDiag::WhenAfterBody`（新码 `0x020010`，`registry.tsv` 单一来源）；规则的新位置（`rule name::<T>(params) when cond { lhs => rhs }`）解析；`camel-format` 按新位置排版；`stdlib/rules.cml` 与仓里全部 `.cml` 的旧位置逐条机械改写（61 处）。
- 没落：`case p when c =>`。它缺的是功能，不是位置。

## 旁证：不与位置规则冲突的两处

- `#impl<f>` 是**声明之前的标签**（`#requires<…>` 同一条通道），没有"体后子句"这种写法，位置规则不碰它。
- `fact` 的尾随子句（`join`/`meet`/`widen`/…）是定义性子句，裁定已明确与适用条件性质不同（`docs/handover/reviews/questions-2026-10-23.md` 第 444 行）；而且 `fact` 今天在解析器里还没有产生式，无冲突可谈。

## 请作者裁定

1. `match` 守卫单开一条车道吗？（守卫假 → 试下一个 case；守卫的求值作用域；`case null` 与守卫的先后；守卫能不能读 `|` 备选模式绑定的名字。）
2. 在它落地之前，设计原则 4.4 与 2026-10-24 的裁定要不要加一句"`match` 守卫尚未实现"？现在是**文档说了、代码没有**，读者照文档写 `case p when c =>` 会得到 `Expected '=>' before 'when'`。

## 出处

- 设计原则 4.4；裁定：`docs/handover/reviews/questions-2026-10-23.md` 第 444 行（`cf3a8c0ce` 那一批）。
- 解析器：`src/parse/syntax/parser_expr.cpp`（`matchCase`）、`src/parse/syntax/parser_rule.cpp`（`ruleDecl`）。
- 本车道落地的具名诊断：`diagnostics/registry.tsv`、`docs/diagnostics.md`（`SyntaxDiag::WhenAfterBody`）。