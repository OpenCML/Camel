# 两处"文档片段不过编译"的读数（h465 集成 lane，2026-10-25）

> 本文只报选题与读数，不定语义。作者页面（`docs/handover/README.cn.md`、`reviews/**`、
> `docs/design-principles*`、`docs/design/**`、`docs/language/**`）一个字未动。

整层（`-Plan .`，`h465-flip-integration`）这两条红：`docs.links.citations` 与
`docs.snippets_compile`。后者是本文的题目；它在**翻转前就是红的**（见第二节），本文要报的是
"哪些片段在翻转线上不过编译、以及它们各属于谁"。

## 一、检查器的口径（读数，不是意见）

`scripts/check-doc-snippets.mjs`：

- `ALLOWED_FILES` 只有三个（`docs/stdlib-reference.md`、`docs/diagnostics.md`、
  `docs/design-principles-log.cn.md`）；
- `EXCERPTS` 只有两个目录：`docs/design/`、`docs/technical/`（这两处"只有含 `func main` 的围栏
  才算程序"，其余算摘录）；
- `docs/design-principles.cn.md`（及 `.en.md`）与 `docs/language/**` **都不在这两张表里**，所以
  它们里面每一个 `camel`/`cml` 围栏都会被编译；
- "故意不是程序"的围栏靠标记，不靠猜：`<!-- camel-snippet: fragment : 理由 -->`
  （`MARKED_KINDS` = `fragment`、`counter-example`、`graph-dump`、`compile-only`；**没有理由的
  标记本身是错误**）。

## 二、`docs/design-principles.cn.md:283`：`...` 占位体（翻转前就红，与翻转无关）

围栏逐字（正文行 283-290，缩进是文档里的两格）：

```camel
  func softmax::<T>(x: T[n, m], axis: int = -1): T[n, m]
      when is_float::<T>()
  {
      ...
  }
  rule add_zero::<T>(x: T) when is_integer::<T>() { x + 0 => x }
```

读数：`docs/design-principles.cn.md:283: does not compile: error[SyntaxDiag::UnexpectedToken]:
Unexpected '...'; expected '}'.`，`.en.md:283` 同一条。

**它不因翻转而红**：一份翻转前的 develop 线跑法（`h437-splice-verbatim` 基座，
6108 / 6087 / **fail 2** / skip 11 / known 8）里 `docs.snippets_compile` 的报文与本树**逐字相同**
（`:283` 与 `.en.md:283` 各一条 `Unexpected '...'`）。机理是围栏体写的是省略号占位符，语法当然
不收；这与 P4 的类型身份、拼写、元素类型任何一条规则都无关。

两种终态（作者页，我不动）：

- (a) 给围栏加标记 `<!-- camel-snippet: fragment : ... -->` —— 机制已存在，其它页面就是这么标
  "这是摘录/示意"的；正文一字不改。
- (b) 把体写成一个真程序（例如 `return x - max(x)` 之类），围栏继续被编译。

## 三、翻转线新增的 6 条：`static loop` 序列形式（`docs/language/**`）

| 文档 | 行 | 报文 |
| --- | --- | --- |
| `docs/language/06_expressions.md` | 458 | `does not compile: Unexpected '('; expected a name.` |
| `docs/language/06_expressions.md` | 505 | 同上 |
| `docs/language/07_statements.md` | 339 | 同上 |
| `docs/language/en/06_expressions.md` | 458 / 505 | 同上 |
| `docs/language/en/07_statements.md` | 339 | 同上 |

三处片段（中英同文）都把循环写成**序列形式**：

```cml
func sum(t: (int, int, int)): int {
    return static loop acc = 0 for (j) in indices t do acc + t.[j]
}

func count::<T>(t: T): int {
    return static loop n = 0 for (name, F) in fields T do n + 1
}

func sum(..ts): int {
    return static loop acc = 0 for (j) in indices ts do acc + ts.[j]
}
```

读数：本树的解析器在 `for (` 处要求一个名字（`Unexpected '('; expected a name.`）；本树实装的
是**推导式形式**（`test/cases/compile/constas/constas_column.cml:11`：
`return ( static for (j) in indices ts => ts.[j].[n] )`）。序列形式（`static loop … for (…) in … do …`）
是 h475 的题目（develop 基座的一条 lane，按协调者说明**不进本树**），所以这 6 条在本分支上属于
"文档写到了实装之前"。

两种终态：

- (a) h475 落到 develop 时它们自然变绿（本树不需要、也不应该动它们，动了会与 h475 打架）。
- (b) 若最终落地的写法与 `for (…) in …` 不同，这 6 处要按落地的拼写改写。

## 四、我没有做的事

- 没有改任何作者页（`docs/design-principles.cn.md`、`.en.md`、`docs/language/**` 一字未动）。
- 因此这两条红在本分支上**仍然红**；`docs.links.citations` 那一条见
  `docs/handover/questions/` 内的相关记录与集成报告：`plan.cn.md:399` 的死锚已按授权做最小引用
  改写，剩下的 `docs/design/public-repo/README.cn.md:66: 'docs/design.md': no such file` 是**翻转前
  就有**、且修在 develop（`docs/design.md` 由 h471 那批在 develop 上创建）的一条，等 develop
  同步过来才会绿。

## 五、随 h474 到位的答复（第三节那 6 条已被判给改语法的车道）

作者的答复随 `handover/h474-in-place-planning` 到位：
`docs/handover/reviews/questions-2026-10-24-evening.md` §`h462-static-loop-sequence-boundaries` 第 3 条
——"设计原则与设计稿里的旧写法我已改（`static loop … for n, F in fields T`）；`docs/language/**` 按
'文档随代码走'由改语法的车道一起改——**这 6 个代码块请本 lane 补上，不必等我**"。

所以第三节的 6 条**不是新问题**：它们归 h462/h475（改语法的车道），不必等作者，也不由集成车道改。
作者已改的是设计原则与设计稿；本次合并**没有**带来 `docs/design-principles.cn.md:283` 的改动，
`:283` 的 `...` 占位体（第二节）仍是未决的那一条。