# 8. 顶层组成与文法概要

本章描述 Camel 的**顶层组成**（程序与文件）、**文件输入与交互**，以及与 **ANTLR** 文法文件的**对应关系**（文法概要）。精确语法以 `antlr/OpenCML.g4`、`antlr/OpenCMLLex.g4` 为准。

---

## 8.1 程序与文件

### 8.1.1 程序结构

- **program**：**`SEP? (decl SEP?)* EOF`**。
  - 即：可选前导分号、若干声明（声明之间可选分号）、文件结束。
- **decl**：**moduleDecl | importDecl | exportDecl | dataDecl | funcDecl | typeDecl | useDecl**。
- 完整程序由若干**声明**组成；执行从入口（如 **main**）开始，由调度器（线性或并行）执行图或字节码。

### 8.1.2 顶层声明类型

| 声明 | 文法规则 | 说明 |
|------|----------|------|
| 模块 | **moduleDecl** | `MODULE identDef` |
| 导入 | **importDecl** | `IMPORT (moduleName \| (identDef \| bracedIdents) FROM moduleName)` |
| 导出 | **exportDecl** | `EXPORT (dataDecl \| typeDecl \| bracedIdents)` |
| 数据 | **dataDecl** | `(LET \| VAR) carrier (':' typeList)? '=' dataList` |
| 函数 | **funcDecl** | `(WITH angledParams)? EXPORT? implMark? modifiers? FUNC identDef parentParams (':' typeExpr)? stmtBlock` |
| 类型 | **typeDecl** | `implMark? TYPE identDef '=' (typeExpr \| STRING)` |
| use | **useDecl** | `USE (identDef '=')? identRef` |

### 8.1.3 语句（用于块内与顶层）

- **stmt**：**dataDecl | funcDecl | typeDecl | dataExpr | useDecl | retStmt | blockStmt**。
- **stmtList**：**stmt (SEP? stmt)* SEP?**。

---

## 8.2 块与表达式层次

### 8.2.1 块

- **stmtBlock**：**`SYNC? '{' stmtList? '}'`**。
- **blockStmt**：**`WAIT? stmtBlock`**。
- **blockExpr**：**stmtBlock | dataExpr**（用于 then/else、match case、Lambda 体等）。

### 8.2.2 表达式层次（从高到低）

与 ANTLR 规则对应，从**最低优先级**到**最高优先级**：

| 规则名 | 说明 |
|--------|------|
| **waitExpr** | `WAIT? assignExpr` |
| **assignExpr** | logicalOrExpr + 可选赋值 |
| **logicalOrExpr** | logicalAndExpr **\|\|** logicalAndExpr * |
| **logicalAndExpr** | equalityExpr **&&** equalityExpr * |
| **equalityExpr** | relationalExpr (**=== \| !== \| == \| !=**) relationalExpr * |
| **relationalExpr** | additiveExpr (**< \| > \| <= \| >=**) additiveExpr * |
| **additiveExpr** | multiplicativeExpr (**+ \| -**) multiplicativeExpr * |
| **multiplicativeExpr** | nullableExpr (**\* \| / \| ^ \| @ \| %**) nullableExpr * |
| **nullableExpr** | unaryExpr (**?? \| !!** dataExpr)? |
| **unaryExpr** | linkExpr (**AS \| IS** typeExpr)? \| (**! \| - \| ~**) linkExpr |
| **linkExpr** | compExpr (**-> \| ?->** compExpr)* |
| **compExpr** | annoExpr (**.. \| ?..** annoExpr)* |
| **annoExpr** | accessExpr (相邻时：indices \| parentArgues \| angledValues \| **!**)* |
| **accessExpr** | primaryData (**.** (IDENTIFIER \| INTEGER))* |
| **primaryData** | identRef \| literal \| arrayData \| structData \| **(** dataExpr **)** \| tupleData \| funcData |

**ctrlExpr**（控制表达式，在 dataExpr 中）：

- **IF** logicalOrExpr **THEN** blockExpr (**ELSE** blockExpr)?
- **MATCH** identRef **'{'** matchCase+ **'}'**
- **TRY** stmtBlock catchClause+ (**FINALLY** stmtBlock)?

---

## 8.3 类型与模式

### 8.3.1 类型表达式

- **typeExpr**：**unionType ('?' unionType?)?**（可空等）。
- **unionType**：**interType ('|' interType)***。
- **interType**：**diffType ('&' diffType)***。
- **diffType**：**keyUnionDiffType ('\\' keyUnionDiffType)***。
- **keyUnionDiffType**：**keyInterType (('+' \| '-') keyInterType)***。
- **keyInterType**：**typeUnit ('^' typeUnit)***。
- **typeUnit**：**(identDef OF)? arrayType**。
- **arrayType**：**specType ('[' ']')***。
- **specType**：**primaryType ('<' typeOrData (',' typeOrData)* '>')?**。
- **primaryType**：**structType \| identRef \| '(' typeExpr ')' \| tupleType \| funcType \| TYPEOF dataExpr \| TYPEAS identDef**。
- **structType**：**'{' (keyTypePair (',' keyTypePair)*)? ','? '}'**。
- **tupleType**：**'(' typeList? ','? ')'**。
- **funcType**：**modifiers? angledParams? parentParams '=>' typeExpr**。

### 8.3.2 模式（match）

- **pattern**：**identRef \| literal \| '(' (dataList \| identList)? ','? ')' \| '{' (pairedValues \| identList)? ','? '}' \| '_'**。
- **matchCase**：**CASE** pattern (**'|'** pattern)* **'=>'** blockExpr。

---

## 8.4 文件输入与交互

- 实现可支持从**文件**读入源码（如 **`camel run <file>.cml`**）、或**交互式**输入。
- 具体由 CLI 与运行时约定；参见 [CLI 文档](../cli.cn.md)、[../cli.md](../cli.md)。

---

## 8.5 文法文件索引

| 文件 | 内容 |
|------|------|
| **antlr/OpenCMLLex.g4** | 词法规则：分隔符、关键字、标识符、字面量、注释等 |
| **antlr/OpenCML.g4** | 语法规则：program、decl、stmt、表达式层次、类型、模式等 |
| **docs/Camel.asdl** | AST 结构定义（ASDL），供实现使用 |

关键字与字面量详见 [02_lexical.md](02_lexical.md)；运算符与优先级详见 [06_expressions.md](06_expressions.md#612-运算符优先级从低到高)。
