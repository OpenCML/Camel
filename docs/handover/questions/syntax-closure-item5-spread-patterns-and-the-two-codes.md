# 语法层收口第 5 条的另一半：`...xs`／解构／元组与结构体模式今天落不了地

## 是什么

`docs/handover/README.cn.md:212`（第 5 条）一句话里有三件事：

1. `is`、`of`、`keyof`、`try`、`catch`、`finally` 六个词不再保留；
2. 「展开 `...xs`、结构体和数组解构同样处理」；
3. 「删掉只为指路而存在的诊断：`ConstructNotSupported`、`FeatureNotSupported`、`TryCatchNotInLanguage`」。

第 1 件已落地（本车道本批提交：`token_kinds.def` 里 `Reserved` 分组整个消失，`enum`/`typeof` 归入
`Type`；解析器、CST/AST 节点、GCT、codegen、格式化器、词法表页面、TextMate 语法一起改）。
第 3 件只落下了一半：`TryCatchNotInLanguage` 整个退役（编码 0x000074 保留不复用），
`is` 与 `of` 两个产出点删掉；**`ConstructNotSupported` 与 `FeatureNotSupported` 两个诊断码本身不能删**。
第 2 件没有落地，需要裁定与独立提交链。下面只写这两处缺口。

## 逐字证据

`docs/handover/README.cn.md:212`：

> **清理保留字**：`is`、`of`、`keyof`、`try`、`catch`、`finally` 这些不进语言的词不再保留，写了就是普通的
> 名字或语法错误；展开 `...xs`、结构体和数组解构同样处理。删掉只为指路而存在的诊断：
> `ConstructNotSupported`、`FeatureNotSupported`、`TryCatchNotInLanguage`（负责人 2026-10-25：
> "try catch 不保留，应该是历史债"；设计原则 3.9 已改）。`throw`、`namespace`、`using`、`typeof`、
> `constas` 都是现行语法，保留。

`docs/handover/reviews/questions-2026-10-24-evening.md:833`：

> **`...xs`、`e is T`、元组和结构体模式、解构**：语法收口第 5 条把它们变成普通的语法错误，专用诊断随之
> 删除。语言参考里"写了会报某某诊断"的那些句子一并删掉。

**同仓的 `docs/design/io-concurrency-streams/plan.cn.md:60` 已经写下相反的一半**，它说的是"删除 `?..` 及其
诊断"这件事有多大，并点名这个码要留：

> `ConstructNotSupported` 这个**诊断码本身要留**——还有 6 条别的用例在用它（`is_operator.cml`、
> `non_null_assert.cml`、`spread_array.cml`、`spread_struct.cml`、`of_type.cml`、`wait_ignored.cml`、
> `strict_equal.cml`）。

**`...` 不只是展开**，它在两处现行语法里是别的记号：

- `stdlib/signatures/globals.cml:263`：`export func print(x: T, ...rest: U...): T`（变参参数，另有
  `:272` `println`、`:281` `eprintln`、`:310` `with <...args: T...>`、`:480` `attempt`）；
- `src/parse/syntax/parser.cpp:567`：`/// moduleName : ('.' | '..' | '...')? IDENTIFIER ...`。

## 今天的读数

本批改动后 `grep` 到的产出点数（`src/**`，不含 `semantic.cpp` 的消息表）：

| 诊断 | 编码 | 改动前 | 改动后 | 剩下的产出点 |
|---|---|---|---|---|
| `ConstructNotSupported` | `0x0300001F` | 16 | **15** | `parse/ast/builder.cpp` 9、`compile/gct/builder.cpp` 2、`compile/gir/*` 4 |
| `FeatureNotSupported` | `0x03000040` | 17 | **15** | 全在 `compile/gct/builder.cpp` |
| `TryCatchNotInLanguage` | `0x03000074` | 1 | **0**（退役） | — |

15 个 `ConstructNotSupported` 产出点里，属于第 5 条点名写法的只有 4 个（`parse/ast/builder.cpp:2403`
展开、`:2452` 结构体展开、`:2802` 标识符模式、`:2832` 元组/结构体模式）；其余是 `rule<...>`（`:879`）、
`fact`（`:1434`）、后缀 `!`（`:4186`）、格式化字符串当模式（`:4763`）、嵌套模式（`:4807`），
以及 GCT/GIR 侧的 `..` 组合、`indices`、`wait`、严格相等、关键字实参等。`FeatureNotSupported` 的 15 个
产出点里，`Struct Unpacking`（`compile/gct/builder.cpp:985`）与 `Array Unpacking`（`:993`）是第 5 条点名的，
其余是 `TypeOp::Inter/Diff/KeyUnion/KeyInter/KeyDiff/Specialize`（`:5115-5150`）、`UnitType`/`InferType`/
`DataType`（`:5610,5621,5632`）、`constas`/rest 参数写错位置（`:5464,5491`）、自指类型（`:1660,1686`）。

第 2 件今天钉在 8 条用例上（`test/plans/feat/compile/not_supported.plan.toml`）：`spread_array`、
`spread_struct`、`struct_unpacking`、`pattern_tuple`、`pattern_struct`、`pattern_identifier`（第 5 条点名的
6 条），以及 `non_null_assert`（后缀 `!`）、`list_comprehension`（不是第 5 条点名的写法）。
全仓 `.cml` 文本扫描：`try {`／`catch e:`／`finally {`／`x is T`／`tag of T` 在代码里**一处真实使用都没有**
（命中全是注释与字符串），所以删掉这六个词没有弄红别处；`stdlib` 里 `let {`／`let [` 0 处，
`...rest` 3 处是参数写法而不是展开。

## 第 7 条（编辑器文法与快照）的同一批裁定

两条与本页不同题、但同批的读数（`docs/handover/reviews/questions-2026-10-24-evening.md` 的 2026-10-26
一节，落在本车道基线 `e7a2dc646` 之后的 `9303b92ec`）：负责人裁定语法快照测试不能"永远跳过"，
`editors/vscode` 的开发依赖进仓库安装步、裸 `Tensor` 改成 `f32[..]` 后重生成 golden，并对
`editors/vscode/**` 放开车道。本车道照此做了（见本批提交与 lane 报告）。

## 选项与代价

- **A（本批已做）**：六个词不再保留；`TryCatchNotInLanguage` 退役；删掉 `is`、`of` 两个产出点；
  词法表、语言参考、TextMate 语法、AI 参考一起同步。代价：第 5 条"同样处理"那半句没落地，
  `ConstructNotSupported`／`FeatureNotSupported` 仍在服务 15+15 个产出点。
- **B（建议下一步，需要开口子）**：把展开、结构体/数组解构、元组/结构体/标识符模式这 4 个产出点删掉，
  `...` 记号本身保留（变参与 `moduleName` 在用）。代价：删或改 6 条用例及其计划条目，语言参考
  `docs/language/06_expressions.md:28,386`／`07_statements.md:54`、`docs/language/03_data_model.md:307`
  的对应句子一并改，`ast_hashes.txt` 金标要重录；数组解构今天没有自己的用例，删它要**新增**一条反例。
  另外 `test/cases/tools/grammar/operators.cml:57-59`（`[...xs, 1]`、`{ ...base, x: 1 }`、`spread(...xs)`）
  钉着 `...` 的 TextMate scope，它那份 `test/plans/feat/tools/golden/grammar/operators.scopes` 要跟着重录——
  这份快照测试从此**真的会跑**（不再 SKIP，见下），漏掉就会红。
- **C（不建议）**：连 `...` 记号一起删。代价：`print(x: T, ...rest: U...)` 这类现行语法一起消失，
  等于改语言表面而不是清理历史债，需要设计原则先裁。