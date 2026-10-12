# 8. 程序与文法

[English](en/08_top_level_and_grammar.md) | 中文

本章描述程序的顶层结构，给出文法概要，并汇总文法接受但尚未实现的写法。精确的文法在 `antlr/OpenCML.g4`、`antlr/OpenCMLLex.g4`。

---

## 8.1 程序

```
program : (decl ';'?)* EOF
decl    : moduleDecl | importDecl | exportDecl | namespaceDecl | usingNamespaceDecl
        | dataDecl | funcDecl | typeDecl
```

模块顶层只有声明，没有可执行语句；执行从 `main` 开始（[4.1](04_execution_model.md#41-程序与入口)）。

| 声明 | 文法 | 章节 |
|---|---|---|
| 模块 | `module 名字` | [5.1](05_modules.md#51-模块声明) |
| 导入 | `import (模块 \| (名字 \| { 名字, ... }) from 模块)` | [5.2](05_modules.md#52-导入) |
| 导出 | `export (dataDecl \| typeDecl \| { 名字, ... })`；函数的 `export` 写在 `func` 前 | [5.3](05_modules.md#53-导出) |
| 命名空间 | `namespace 限定名 { ... }` | [5.4](05_modules.md#54-命名空间) |
| 展开命名空间 | `using namespace 限定名` | [5.2](05_modules.md#52-导入) |
| 数据 | `(let \| var) 名字 (: 类型)? = 表达式` | [7.2](07_statements.md#72-let-与-var) |
| 函数 | `标注* (with <形参>)? export? 修饰符* func 名字(形参) (: 类型)? 块` | [7.4](07_statements.md#74-函数) |
| 类型 | `type 名字 (<参数, ...>)? = 类型` | [7.7](07_statements.md#77-类型声明) |

## 8.2 语句与块

```
stmt      : dataDecl | funcDecl | typeDecl | dataExpr | usingNamespaceDecl | retStmt | blockStmt
retStmt   : (return | throw) dataList
stmtBlock : sync? '{' (stmt ';'?)* '}'
blockExpr : stmtBlock | dataExpr            // 分支臂、lambda 体
```

## 8.3 表达式

从低优先级到高优先级（见 [6.12](06_expressions.md#612-运算符优先级)）：

```
dataExpr   : waitExpr | ctrlExpr
ctrlExpr   : if logicalOr then blockExpr (else blockExpr)?
           | match 名字 '{' (case pattern ('|' pattern)* '=>' blockExpr)+ '}'
waitExpr   : wait? assignExpr
assignExpr : logicalOr (('=' | '+=' | '-=' | '*=' | '/=' | '%=' | '^=' | '@=' | '&=' | '|=') logicalOr)?
logicalOr  : logicalAnd ('||' logicalAnd)*
logicalAnd : equality ('&&' equality)*
equality   : relational (('===' | '!==' | '==' | '!=') relational)*
relational : additive (('<' | '>' | '<=' | '>=') additive)*
additive   : multiplicative (('+' | '-') multiplicative)*
multiplicative : nullable (('*' | '/' | '^' | '@' | '%') nullable)*
nullable   : unary (('??' | '!!') dataExpr)?
unary      : link (as 类型)? | ('!' | '-' | '~') link
link       : comp (('->' | '?->') comp)*
comp       : anno ('..' anno)*
anno       : primary ( '.'字段 | '[' 下标 ']' | '(' 实参 ')' | '<' with 实参 '>' )*
primary    : 名字 | 字面量 | '[' 元素, ... ']' | '{' 键: 值, ... '}' | '(' 表达式, ... ')' | lambda
lambda     : 修饰符* ('<' 形参 '>')? '(' 形参 ')' (':' 类型)? '=>' blockExpr
pattern    : 字面量 | '_' | null | error 名字 | 名字
```

下标、实参与 `with` 实参必须紧贴它前面的表达式。`(e)` 是括号表达式，`(e,)` 是单元素元组。

## 8.4 类型

```
typeExpr : unionType ('?' | '!')*
unionType: arrayType ('|' arrayType)*
arrayType: specType ('[' ']')*
specType : primaryType ('<' (类型 | const 数据), ... '>')?
primaryType : 名字 | '(' 类型 ')' | '(' 类型, ... ')' | '{' 名字: 类型, ... '}'
            | 修饰符* '(' 形参 ')' '=>' 类型 | typeas 名字
```

- `T?`、`T!`：[3.4](03_data_model.md#34-可能为空-t-与可能出错-t)。
- `typeas T`：[7.5](07_statements.md#75-泛型函数)。
- 尖括号：类型函数的实参（[7.7](07_statements.md#77-类型声明)）或模块类型的实参。

## 8.5 文件

| 文件 | 内容 |
|---|---|
| `antlr/OpenCMLLex.g4` | 词法规则 |
| `antlr/OpenCML.g4` | 语法规则 |
| `docs/Camel.asdl` | 抽象语法树的结构定义 |
| `docs/diagnostics.md` | 全部诊断的目录（由脚本生成） |

## 8.6 保留但尚未实现的写法

下列写法文法接受（或关键字已保留），编译器明确拒绝。它们的含义多数尚未决定，不要依赖。

| 写法 | 得到的诊断 | 现在的替代写法 |
|---|---|---|
| 列表推导 `[e for x in xs if c]` | `SemanticDiag::ConstructNotSupported` | `xs->filter<...>->map<...>` |
| 展开 `[...xs, y]`、`{ ...p, x: 1 }`、`f(...args)` | `ConstructNotSupported` | `concat(xs, [y])`；逐个写出字段 |
| 格式化字符串 `` `x = {x}` `` | `ConstructNotSupported` | `'x = {}'->format<x>` |
| `e is T` | `ConstructNotSupported` | 无（运行时没有类型信息） |
| 非空断言 `x!` | `ConstructNotSupported` | `x ?? 默认值`、`match` |
| `f ?.. g` | `ConstructNotSupported` | 无 |
| `f..g` | 组合的结果不能被调用（`ArgumentsMismatch`） | `(x: T) => g(f(x))` |
| `wait { ... }`，以及名字、字面量、lambda 之前的 `wait` | `ConstructNotSupported` | `sync { ... }` |
| `raise e` | `ConstructNotSupported` | `throw e` |
| `try { } catch e: T { } finally { }` | `SemanticDiag::TryCatchNotInLanguage`（不属于语言） | `T!` 与 `!!`、`?->`、`case error e` |
| 元组模式、结构体模式；对普通类型的标识符模式 | `ConstructNotSupported` | `if` 链；先取成员 |
| 结构体解构 `let { x, y } = p`、数组解构 `let [a, b] = xs` | `SemanticDiag::FeatureNotSupported` | `p.x`、`xs[0]`（元组解构可用） |
| 关键字实参 `f(a: 1)` | `FeatureNotSupported` | 按位置传 |
| 形参默认值 `b: int = 5` | `FeatureNotSupported` | 重载；`with` 形参 |
| `use 名字 = 限定名` | `FeatureNotSupported` | `import { 名字 } from 模块`；换名用 `let 新名 = 模块::名字`（[5.5](05_modules.md#55-use)） |
| 切片下标 `xs[a:b]`、`xs[a:b:c]` | `SemanticDiag::NoMatchingFunction`（没有专门的诊断：按“下标运算符没有接受这些实参的重载”报告） | `xs->slice<a, b>` |
| 类型运算 `A & B`、`A \ B`、`A + B`、`A - B`、`A ^ B` | `FeatureNotSupported` | 无 |
| `typeof e`、`T ? U`、`名字 of T` | `FeatureNotSupported` | 无 |
| `keyof`、`enum` | 语法错误 | 无 |
| `for`、`while` 循环 | 语法错误（没有循环语法） | 递归；`map`/`filter`/`reduce`/`foreach` |
| 修饰符 `atomic`、`shared`、`inner`、`outer` | 无诊断；没有语义 | 不写 |

此外有几处“能写出来但目前用不了”：联合类型 `A | B`（成员类型的值不能进入它）、带函数类型的声明 `let f: (x: int) => int = g`。
