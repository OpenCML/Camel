# H462（`static loop` 去括号）落地后留给作者与集成的三件事（2026-10-25）

作者 2026-10-24 夜裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` 的
loop-for-and-static-loop）只说了一件事：`static loop` 的头**不要括号**，`in` 后面跟任何**编译期能求出
的序列**，求不出时报「`static loop` 的序列必须在编译期已知」。本文件记的是这条裁定**没有**裁定、
但落地时不得不选一个读法的地方，以及本 lane 按纪律没碰的文件。全部结论都来自本树实测
（`handover/h462-static-loop-parens`，host `88zjwei`，计划 `smoke,test/plans/feat -Tier fast -Jobs 8`）。

## 一、`a..b` 是半开的：本 lane **已经按半开落地**，这一条需要作者确认

裁定写的是「常量区间 `0..5` 都行」，没说 `0..5` 是 `[0,5)` 还是闭区间。本 lane 取**半开**，依据是
语言里 `..` 唯一的既有读法（普通 `loop` 同一条头）：

```
loop        acc = 0 for i in 0..5 do acc + i   ->  10      # 0+1+2+3+4，实测 develop 就如此
static loop acc = 0 for i in 0..5 do acc + i   ->  10      # 本 lane 落地后，与上面一致
static loop acc = 7 for i in 3..3 do acc + 1   ->  7       # 空区间 = 零轮，值是首个状态
```

如果作者要的是闭区间（`0..5` 走 0…5），那 `..` 在循环头里就要和它在别处（`intervalValue` 产出的
`int[]`、h453 的普通 `loop`）**分家**：同一个符号两种读法正是裁定反对的「同一件事两种写法」。
所以本 lane 认为半开是唯一自洽的读法，但它是**推定**，请作者一句话确认。

## 二、「任何编译期能求出的序列」今天只认常量区间 —— 边界由代码而不是由裁定画出

实测能求出的（用例 `test/cases/compile/static_expansion/static_loop_sequence.cml`，全部通过）：

| 写法 | 实测 | 说明 |
|---|---|---|
| `for i in 0..5` | 走 0…4 | 字面量两端 |
| `for i in 0..bound`（`let bound = 4`） | 走 0…3 | 名字，绑定的是常量且不是 `var` |
| `for i in 0..i`（嵌在 `for i in 0..4` 里） | 每轮按外层的编译期常量 | 内层序列是外层那一轮的常量 |
| `for i in <序列>` 里的 `i` 当键 | `t.[i]` 静态下标成立 | 迭代变量是编译期常量 |
| `for i in 0..n`（`n` 是形参） | 报 `SemanticDiag::StaticLoopSequenceNotKnown` | 裁定原话的落点 |

**没有**落地、也没有被裁定覆盖的（本 lane 一律报 `StaticLoopSequenceNotKnown`，不是短语法错）：

1. `range(a, b, step)` 或任何用函数调出来的序列——即使实参全是常量，本 lane 也没接
   （`constas`/`#macro` 折出来的值算不算「编译期能求出」，要作者定）；
2. 按值走的序列：`for x in ts`（`ts` 是编译期元组）、`for x in [1, 2, 3]`（数组字面量）。
   今天 `in ts` 会报「序列未知」，而 `in indices ts` 走的是**位置**。裁定只说「`indices ts`、常量区间
   `0..5` 都行」，没说要一个按元素走的形态；
3. 浮点 / 字符 / 枚举区间的元素类型：今天只认 `int` 两端（与 `..` 的 `intervalValue` 一致）；
4. 多名字（`for i, F in ...`）在 `indices` 与常量区间下**非法**，只有 `fields` 有第二个名字。

## 三、本 lane 按纪律没碰、需要作者或别的车道处理的文件

1. **作者页里的旧写法一字未改**（`docs/language/**`、`docs/design-principles*` 是作者页）：
   `docs/language/06_expressions.md:458,460,493,496,505,507,511`、`docs/language/07_statements.md:339,341`、
   `docs/language/en/06_expressions.md:458,460,493,496,505,507,511`、`docs/language/en/07_statements.md:339,341`、
   `docs/design-principles.cn.md:370`、`docs/design-principles.en.md:370`、
   `docs/design/generic-structure/README.cn.md:125,304`、`docs/design/showcase/README.cn.md:143`。
   这些页现在教的是**已被删掉的旧语法**，请作者改写（语言参考与给 AI 的参考各要一个可编译的例子，
   本 lane 只在 `docs/reference/camel-for-ai.md` §6 里补了）。

   **代价是实测出来的、必须记下**：`docs.snippets_compile` 这条测试基线红 2 处，改后红 **8** 处，
   多出来的 6 处正是作者页里带括号的例子（这些页的 `camel` 代码块由 `check-doc-snippets` 编译）：

   ```
   基线： docs/design-principles.cn.md:283      Unexpected '...'; expected '}'
          docs/design-principles.en.md:283      Unexpected '...'; expected '}'
   改后： +docs/language/06_expressions.md:458          Unexpected '('; expected a name.
          +docs/language/06_expressions.md:505          Unexpected '('; expected a name.
          +docs/language/07_statements.md:339           Unexpected '('; expected a name.
          +docs/language/en/06_expressions.md:458       Unexpected '('; expected a name.
          +docs/language/en/06_expressions.md:505       Unexpected '('; expected a name.
          +docs/language/en/07_statements.md:339        Unexpected '('; expected a name.
   ```

   红**集**（测试名）没变，仍是 `docs.links.citations` + `docs.snippets_compile` 两条；但这一条的
   红**内容**从 2 处涨到 8 处，只有作者能收（改这 6 个代码块就是改作者页）。
2. **例外一处、需要说明**：`docs/language/08_top_level_and_grammar.md` 与 `en/` 里的
   **机器生成文法块**被重新生成了（`node scripts/check-grammar.mjs --write`）。原因是
   `test/plans/feat/parse/frontend.plan.toml` 的 `grammar.reference_in_step` 是 fast 层、基线为绿的
   测试，它跑 `check-grammar.mjs --check`：不改生成块就**必然**多一条红，而本任务的验收要求红集只减不增。
   改动只有 3 行（`staticLoopExpr` 的 `staticForHeader`→`staticLoopHeader`，以及新增的两行
   `staticLoopHeader` 产生式），正文散文一字未动。
3. **一句话的行为变化**：`for j, F in indices ts`（第二个名字配 `indices`）以前报
   `Unexpected 'F'`，现在在逗号处报 `UnexpectedToken`、期望 `in`（因为头和普通 `loop` 同形）。
   用例 `static_expansion/indices_two_names.cml` 只钉诊断名，仍然绿。