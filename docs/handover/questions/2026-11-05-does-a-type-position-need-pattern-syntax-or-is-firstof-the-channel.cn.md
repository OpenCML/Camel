# 类型位置要模式语法，还是就用 `FirstOf<P>` 这条既有通道？

2026-11-05，`lane-gradtype`（`06cdadd45`）。裁定要的写法是类型位置上的 `(W, ...Rest)`，今天的类型语法表达不出来，这一笔改用 `FirstOf<P>` 顶上，并在提交信息里记成 recorded deviation。**绕路记进提交信息不等于交代清楚**：换写法这件事落成了这一页，交给作者定。

## 1. 问句

**`(W, ...Rest)` 这种「在类型位置拆一个包」的写法，是要作为语言的一部分落地，还是今天就用 `FirstOf<P>` 这条既有通道？**
**若是后者，`FirstOf` 的语义边界要不要写进语言参考**——它今天只活在 `stdlib/types.cml#FirstOf` 与 `src/core/type/typecall.cpp#TypeCallType::evaluate` 里，`docs/language/` 一个字没有。

## 2. 让它是问句的读数（逐条抄，不概括）

读数取自 `lane-gradtype` 的在建树（`06cdadd45`；二进制自报 `Camel v0.2.44 (Build 261104_163543_688a07164_dirty)`，即提交前那份带改动的工作树。`06cdadd45` 还没进 `develop`，`FirstOf` 与 `Gradient` 今天只在 lane-gradtype 上）。探针文件在 `/tmp/pat-probe/` 下，一条一个文件。

### 2.1 `FirstOf` 的签名，与它对空包的处理：拒，但原话不点名 `FirstOf`

- **签名**：`stdlib/types.cml#FirstOf` 声明的是 `export type FirstOf<P>`，实现在 `src/core/type/typecall.cpp#TypeCallType::evaluate` 里的一支，注册在 `kTypeFunctions`。**只收包**：`FirstOf<(int, string)>` 报 `(int, string) is not a type pack, and a pack is what has a first member`，exit 1。**收包就有答案**：`is_same::<FirstOf<WithOf<Loss>>, f64[2]>()` 印 `true`，exit 0（`Loss` 是 `<w: f64[2]> (x: float) => float`）。
- **空包**：拒。一条没有 `with` 参数的函数（`type Plain = (x: float) => float`）被问 `FirstOf<WithOf<Plain>>`（`camel /tmp/pat-probe/d1.cml`，`echo $?` 给 `1`），原话逐字：

```
error[SemanticDiag::TypeFunctionNotApplicable]: The type function 'pack<>' is not applicable: a type pack is a list of types, and `...` writes its members where a type list is.
  --> d1.cml:6:12
   |
 6 |     let v: FirstOf<WithOf<Plain>> = 0
   |            ^^^^^^^^^^^^^^^^^^^^^^
```

  **被点名的类型函数是 `pack<>`，不是 `FirstOf`**；理由说成「包不是类型列表」。写 `FirstOf` 的人从这句话里看不出自己错在哪一步。
- **同一个空包，经 `Gradient<F>` 走一遍又不拒了**：`let g: Gradient<Plain> = 0`，`exit=1`，但报的不是空包，是 `The literal '0' cannot be represented as type '(__pack_0_0: float) => ()': the value cannot become that type.`——声明的类型印出来，结果那一格是 `()`。**同一个空包，两处写法两个答案。**

### 2.2 `#Gradient` 今天怎么用它

`stdlib/types.cml#Gradient`，就是这两行：

```
export type Gradient<F> =
    <...WithOf<F>> (...ParamsOf<F>) => Tangent<FirstOf<WithOf<F>>>
```

`with` 列表照抄 `F`，普通参数照抄 `F`，结果取第一个 `with` 参数的余切——**`FirstOf` 在标准库里只有这一处调用**，就是结果那一格。所以 `FirstOf` 已经不只是一条语法在那里，它是 `Gradient<F>` 唯一的支点。（`ValueAndGradient<F>`、两个 `…AndAux<F>` 要的是「其余」那一半，今天写不出来，所以不存在。）

### 2.3 裁定原文里 `(W, ...Rest)` 出现的位置

- **`docs/handover/reviews/questions-2026-10-29.md#裁定（2026-11-04 05:20）`** 第 2 条：`(W, ..Rest)` 合不合法：合法。`match type` 的元组模式允许最后一个成员写成 `..Rest`，绑定一个类型包，读起来与值层 `<w, ..rest>` 一致。空元组匹配不上 `(W, ..Rest)`，就按语言规则在实例化处报错（原则 §5）。同节末行给出整条写法：`type Gradient<F> = match type (..WithOf<F>) { (W, ..Rest) => <W, ..Rest> (..ParamsOf<F>) => Tangent<W> }`——**两半都有：头是 `W`，其余是 `..Rest`，结果写 `Tangent<W>`**。
- **`docs/handover/reviews/questions-2026-10-29.md#负责人定（2026-11-04 05:45）`** 第 3 条：类型层拆包那页的写法跟着改成 ``match type (...WithOf<F>) { (W, ...Rest) => … }``。包一律写 `...` 之后，这就是上面那条裁定的现行拼法，也是本页标题用的拼法。
- **`docs/design-principles.cn.md#5.6 类型函数与原语`** 两条：一条是「类型层的包同样能拆」（值层能写 `<w, ...rest>`，类型层也能把一个类型包拆成「第一个成员」和「其余」；空包拆不出第一个成员，在实例化处按语言规则报错），一条是「`...` 只表示包」（举例里写着 `(W, ...Rest)`）。

### 2.4 今天写 `(W, ...Rest)` 会得到什么（真跑，两条）

**(a) 照裁定第 3 条的拼法放进类型位置**：`type WeightOf<F> = match type (...WithOf<F>) { (W, ...Rest) => W }`

```
error[SyntaxDiag::UnexpectedToken]: Unexpected '('; expected 'case' or '}'.
  --> c1.cml:4:48
   |
 4 | type WeightOf<F> = match type (...WithOf<F>) { (W, ...Rest) => W }
   |                                                ^
```

exit 1。**这个模式的第一个记号就没有位置可站**：`src/parse/syntax/parser_expr.cpp#ParserImpl::parseTypeCasePattern` 今天收的只有形状词与一个类型表达式（`typeCasePattern : 'struct' | 'tuple' | 'func' | '#' 'leaf' | '_' | typeExpr`），分支之间的分隔词是 `case`。

> **注记（照裁定抄的人会以为自己抄错）**：裁定里那段 `match type (...WithOf<F>) { (W, ...Rest) => W }` 省略了 `case`——实现要求写 `case`，照抄会得到上面那行 `Unexpected '('; expected 'case' or '}'`。**这不是抄写错误，是裁定片段与实现之间的一个字的差。**

**(b) 补上 `case` 再写一遍**：`type WeightOf<F> = match type (...WithOf<F>) { case (W, ...Rest) => W }`

```
error[SemanticDiag::UnresolvedTypeReference]: Unresolved type reference: 'W'.
  --> c3.cml:4:54
   |
 4 | type WeightOf<F> = match type (...WithOf<F>) { case (W, ...Rest) => W }
   |                                                      ^
```

exit 1。**这一条更要紧**：解析过了，但 `(W, ...Rest)` 被读成**元组类型**——`W` 是一个类型名，不是绑定，`...Rest` 是展开。所以「模式」这一读法今天不存在，它退化成「用名叫 `W` 的类型拼一个元组」。

## 3. 三条路与各自代价

- **① 落模式语法（改动面在 parser 与类型检查）。** **谁要改**：语法一条线加类型一条线——`src/parse/syntax/parser_expr.cpp#ParserImpl::parseTypeCasePattern` 加一个绑定模式分支；arm 的 AST 节点与 `src/parse/ast/builder.cpp#Builder::visitMatchTypeType` 要带绑定名；`src/compile/gct/builder.cpp#Builder::visitMatchTypeType` 建 `MatchTypeType` 时把名字与「其余」那个包一起传下去；`src/core/type/typecall.cpp#MatchTypeType` 的 arm 今天只有 shape + identities + result，要能存绑定并在结果里替换。**多大**：4 个源文件的一个新语法分支，加 `stdlib/types.cml` 两处声明改写（`Gradient<F>`、`ValueAndGradient<F>`），加 `test/plans/feat/parse/golden/ast_hashes.txt`（312 行）重录、`docs/stdlib-reference.md` 重生成（`scripts/stdlib-reference.mjs --write`）、`docs/language/07_statements.md`（539 行）§7.7 一节与 `docs/language/08_top_level_and_grammar.md` 的语法行、一条新用例。**一个车道一晚的量。**
- **② 保留 `FirstOf<P>`，把它的语义写进语言参考。** **谁要改**：文档 + 一条求值路径。文档是 `docs/language/07_statements.md` §7.7 加一段（只收包、空包拒绝、拒绝时说什么），`docs/stdlib-reference.md` 由生成器从 `stdlib/types.cml#FirstOf` 的注释生成（那四段注释已经写好，这一步是生成一次）；求值路径是 §2.1 那两行说的落差——空包经 `Gradient<F>` 落成 `()`，所以写进参考之前要么把这条路径改成拒，要么参考里写清「经声明体时不拒」。再加一条负例把拒绝原话钉住（`test/plans/feat/compile/type_packs.plan.toml` 一条）。**多大**：一节文档 + 一次生成 + 一条负例 + 一处判断。**今晚能做。**
- **③ 两者都留。** **谁要改**：① 的全部，再加一条「哪个是正写」进 `docs/design-principles.cn.md#5.6 类型函数与原语`，再加一条两写法同型的用例（同一个 `F`，`match type` 拆出来的 `W` 与 `FirstOf<WithOf<F>>` 必须 `is_same` 为 `true`）。**多大**：① 和 ② 的代价都付，另外多出「一个计算两种写法」的登记——正是 §5.6「`...` 只表示包」那条要消掉的东西；`ValueAndGradient<F>` 一旦两种写法都在，同一个形状在标准库里有两份声明，读数得两边都对。

## 4. 默认（我方已按此继续）

**先用 `FirstOf<P>` 这条既有通道，等有模式语法再换写。** 理由两句：**`FirstOf` 已经是真类型函数、有实例化期拒绝**（§2.1 的 exit 1 就是它，`true`/`false` 也是它）；**而模式语法要动 parser 的 `parseTypeCasePattern`，在今晚的合并队列里排不进去。**

本轮按默认做到哪一步：`Gradient<F>` 已经用 `FirstOf` 写完（`06cdadd45`），不改；**「其余」那一半没有通道**——`ValueAndGradient<F>` 与两个 `…AndAux<F>` 要 `Rest`，一条都写不出来，停在原地等模式语法，**不拿 `FirstOf` 硬凑**。作者若改成 ①/③，回退的是 `stdlib/types.cml` 里 `Gradient<F>` 那两行与 `typecall.cpp` 里 `FirstOf` 那一支，其余不动。

作者推翻时用这条判据，不另立一页：同一个 `F`，模式拆出来的 `W` 与 `FirstOf<WithOf<F>>` 必须是 `is_same` 的 `true`；同一个空包，两处写法必须给同一条拒绝原话（今天给的是两条，见 §2.1）。