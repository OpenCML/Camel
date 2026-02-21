# 各种树与中间结构详解

本文档说明编译过程中出现的**树形与图形中间结构**：CST、AST、GCT，以及 GIR 的文本/拓扑视图。这些属于**技术实现**，与《语言规范》分离。文中示例通过对 **test/** 下代码执行 `camel inspect` 得到。

---

## 1. 如何查看各阶段产出

在项目根目录执行（将 `<file>` 换为实际 `.cml` 路径，如 `test/run/linear/hello.cml`）：

| 选项 | 说明 |
|------|------|
| `camel inspect --tok` / `--token-stream` | 词法 token 流 |
| `camel inspect --cst` / `--concrete-syntax-tree` | 具象语法树（CST） |
| `camel inspect --ast` / `--abstract-syntax-tree` | 抽象语法树（AST） |
| `camel inspect --gct` / `--graph-construct-tree` | 图构造树（GCT） |
| `camel inspect --gir` / `--graph-ir` | 图中间表示（GIR，Dot 格式） |
| `camel inspect --topo-node-seq` | 拓扑排序后的节点序列（线性调度视图） |

示例：`camel inspect --ast test/run/linear/hello.cml`

---

## 2. 具象语法树（CST）

### 2.1 角色与来源

- **CST** 由语法分析器（ANTLR4）根据 **antlr/OpenCML.g4**、**OpenCMLLex.g4** 直接产生。
- 保留**语法规则与词法单元**：括号、关键字、运算符的层次与顺序，与源码一一对应，便于错误定位和格式化。

### 2.2 顶层结构

- 根节点为 **Program**，其子节点为 **Decl** 序列，最后为 **\<EOF\>**。
- 每个 **Decl** 可为：
  - **ModuleDecl**：`module` + **IdentDef**（模块名）
  - **ImportDecl**：`import` + **BracedIdents** / 列表 + `from` + **ModuleName**
  - **FuncDecl**：`func` + **IdentDef** + **ParentParams** + `:` + **TypeExpr** + **StmtBlock**

### 2.3 函数体与表达式层次（hello.cml 片段）

**StmtBlock** 含 `sync`、`{`、**StmtList**、`}`。**StmtList** 中每个 **Stmt** 可以是：

- **DataExpr**（表达式语句）：从 **WaitExpr** → **AssignExpr** → **LogicalOrExpr** → … → **LinkExpr** → **CompExpr** → **AnnoExpr** → **AccessExpr**。
  - **AccessExpr** 可包含 **PrimaryData**（如 **IdentRef**：`println`）及 **ParentArgues**（`(`、**ArgumentList**、`)`）。
  - **ArgumentList** 内为 **IndexValues** / **IndexValue**，其 **DataExpr** 再递归同一表达式层次，最终到 **Literal**（如 `'Hello, Camel!'`）。
- **RetStmt**：`return` + **DataList**，**DataList** 中同样是 **DataExpr** 的递归下降直到 **Literal**（如 `0`）。

表达式在 CST 中按**优先级**展开为长链：**WaitExpr** → **AssignExpr** → **LogicalOrExpr** → **LogicalAndExpr** → **EqualityExpr** → **RelationalExpr** → **AdditiveExpr** → **MultiplicativeExpr** → **NullableExpr** → **UnaryExpr** → **LinkExpr** → **CompExpr** → **AnnoExpr** → **AccessExpr** → **PrimaryData** / **Literal** 等。每一层对应文法中的一个规则，便于解析但不直接用于语义分析。

### 2.4 类型在 CST 中的形式

- **TypeExpr** 常呈现为 **UnionType** → **InterType** → **DiffType** → **KeyUnionDiffType** → **KeyInterType** → **TypeUnit** → **ArrayType** → **SpecType** → **PrimaryType** → **IdentRef**（如 `int`）。
- 括号、冒号、关键字都会作为节点或叶子出现。

### 2.5 示例（hello.cml 的 CST 缩略）

```
Program
|-Decl -> ModuleDecl (module, main)
|-Decl -> FuncDecl
|     func, IdentDef(main), ParentParams( ), :, TypeExpr(..., int),
|     StmtBlock
|        sync, {, StmtList
|           Stmt -> DataExpr -> ... -> AccessExpr(PrimaryData(println), ParentArgues( 'Hello, Camel!' ))
|           Stmt -> RetStmt(return, DataList(0))
|        }
\- <EOF>
```

---

## 3. 抽象语法树（AST）

### 3.1 角色与来源

- **AST** 由 **AST Builder** 从 CST 构建，丢弃括号、优先级链等语法细节，只保留**声明、类型、表达式语义**，便于后续 GCT 与类型检查。
- 结构在代码中对应 **parse/ast/** 与 **docs/Camel.asdl**（ASDL 为内部定义，语言规范不依赖 ASDL）。

### 3.2 顶层结构

- 根为 **Module**，包含 **Import\***、**Export?**、**Stmts\***。
- **Stmts** 下为顶层语句：**FuncDecl**、**DataDecl**、**TypeDecl** 等。

### 3.3 函数声明

- **FuncDecl** 子节点为 **FuncData**，包含：
  - **FuncType**：**NamedPair\***（with 参数）、**NamedPair\***（普通参数）、**Type?**（返回类型，如 **RefType: int**）。
  - **StmtBlock**：是否 sync、**Stmt\***。
- **Stmt** 可为 **ExprStmt**、**ExitStmt**（return）、**DataDecl**（let/var）等。

### 3.4 表达式在 AST 中的形式

- **ExprStmt** 内为 **Data**（表达式），如 **ReservedExpr**（Call）：**RefData**（如 `println`）、**Data\***（实参）、**NamedData\***。
- **ExitStmt** 为 **return**，后跟 **Data\***（返回值）。
- **DataDecl**：**Data\*** 为初值，可为 **ReservedExpr**（如 `now()`、`format<...>`）、**BinaryExpr**（如 `-`、`+`）、**IfExpr**、**RefData** 等。
- **IfExpr** 含条件、then 块、else 块；**BinaryExpr** 含运算符与左右操作数；**ReservedExpr** 表示调用、泛型等保留语义。

### 3.5 示例（hello.cml 的 AST）

```
Module: main
\-Stmts*
   \-FuncDecl: main
      \-FuncData: main
         FuncType: NamedPair*, NamedPair*, RefType(int)
         StmtBlock: sync
            \-Stmt*
               ExprStmt -> ReservedExpr(Call): RefData(println), Data*('Hello, Camel!')
               ExitStmt: return -> Data*(0)
```

### 3.6 示例（fib.cml 的 AST 片段）

- **FuncDecl: fib**：FuncType 含 **NamedPair: n, RefType: int**，返回 **RefType: int**；StmtBlock 内 **ExitStmt: return** 后跟 **IfExpr**。
- **IfExpr**：**BinaryExpr(<=)**（RefData(n), 1）→ then 为 **RefData(n)**，else 为 **BinaryExpr(+)**（两次 **ReservedExpr** 调用 fib(n-1)、fib(n-2)）。
- **FuncDecl: main**：含 **DataDecl: idx/start/res/duration**、多个 **ExprStmt**（println、format、now、fib 等）、**ExitStmt: return 0**。

---

## 4. 图构造树（GCT）

### 4.1 角色与来源

- **GCT** 由 **GCT Builder** 从 AST 生成，是 AST 到 **GIR（图）** 的中间形态；仍为**树形**，但节点类型已按「图构造」语义划分（声明、链接、分支、退出等）。

### 4.2 顶层结构

- 根为 **EXEC**，其子节点包括：
  - **DECL**：对应函数/类型等声明，如 **DECL: func main**，下挂 **TYPE: () => i64**。
  - **FUNC**：对应一个函数的图构造，如 **FUNC: main**，下挂 **TYPE** 与 **SYNC**（或 **EXEC**）块。

### 4.3 节点类型（LoadType）含义

| 节点 | 含义 |
|------|------|
| **DECL** | 声明（函数名、类型签名） |
| **TYPE** | 类型（如 `() => i64`、`(n: i64) => i64`） |
| **FUNC** | 函数体入口 |
| **SYNC** | 同步块（顺序执行） |
| **EXEC** | 可执行块 |
| **LINK** | 普通调用/数据流链接（argcnt=…） |
| **WITH** | with 参数/泛型参数链接 |
| **NREF** | 对变量的引用（绑定名） |
| **DREF** | 对符号的解引用（如 println、fib、__add__、format） |
| **DATA** | 编译期常量（地址、类型、字面量） |
| **EXIT** | 返回（return） |
| **BRCH** | 条件分支 |
| **CASE** | 分支分支（(true)/(else)/(value)） |
| **ACCS** | 下标/字段访问，子节点 0 为被访问对象 |
| **CAST** | 运行时类型转换，子节点 0 为待转换的值 |

### 4.4 示例（hello.cml 的 GCT）

```
EXEC
|-DECL: func main
|  \-TYPE: () => i64
\-FUNC: main
   |-TYPE: () => i64
   \-SYNC
      |-LINK: argcnt=1
      |  |-DREF: println
      |  \-DATA: 0x..., string, "Hello, Camel!"
      \-EXIT: return
         \-DATA: 0x..., i64, 0L
```

### 4.5 示例（fib.cml 的 GCT 片段）

- **FUNC: fib** 下 **EXEC** → **EXIT: return** → **BRCH**：
  - **LINK**（argcnt=2）：**DREF: __le__**，**DREF: n**，**DATA: 1L**（条件 n <= 1）。
  - **CASE: (true)**：**EXEC** → **DREF: n**（then 分支）。
  - **CASE: (else)**：**EXEC** → **LINK**（**DREF: __add__**，两次 **LINK** 调用 fib(n-1)、fib(n-2)）。
- **FUNC: main** 下 **SYNC**：多个 **NREF**（idx, start, res, duration）各自绑定 **DATA** 或 **LINK**（now、fib、format、println），最后 **EXIT: return** → **DATA: 0L**。

GCT 中的 **DREF** 对应算子或函数符号，**LINK/WITH** 表示参数传递与调用关系，**BRCH/CASE** 对应 if-then-else 或 match，为后续转为 GIR 的节点与边做准备。

### 4.6 特殊语义：`as` 关键字与类型转换

`as` 表达式的处理分两类：

1. **静态字面量可转换**：左操作数为 `DATA`（编译期常量）且 `convertTo(targetType)` 成功时，直接产生新的 `DataLoad`，无需 GCT 节点。
2. **需运行时转换**：左操作数为变量、调用结果等非 `DATA` 时，用 `CastLoad(targetType)` 包装，子节点 0 为原始值节点。若静态字面量转换失败，则报语义错误。

GIR Builder 在 `visitCastNode` 中递归访问子节点 0、创建 `CastNode`，并以 `Norm` 边将值节点连接到 `CastNode`。

---

## 5. GIR 与拓扑序视图（补充）

- **GIR** 是图而非树，详见《图中间表示（GIR）详解》。用 `camel inspect --gir` 得到的是 **Dot** 格式：**subgraph** 对应函数图，**节点** 为 DATA/OPER/CALL/EXIT 等，**边** 为数据依赖或控制依赖。
- **拓扑序**：`camel inspect --topo-node-seq` 输出线性调度器使用的节点顺序，例如：
  - **FUNC: main** 下列出 `[N00] DATA(-1, "Hello, Camel!")`、`[N01] CALL: <println>, N00`、`[N02] DATA(-2, 0L)`、**RETN: N03**，以及 **CALL: __root__**（入口）。

这些视图用于实现与调试，不改变语言规范；语言规范仅描述源码与可见行为。
