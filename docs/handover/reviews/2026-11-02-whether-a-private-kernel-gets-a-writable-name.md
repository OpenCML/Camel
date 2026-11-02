# Should a body-less kernel declaration get a writable name?

**状态**：等作者裁定。默认建议：**不动，先记着**。2026-11-02 提出，来自拒绝原因普查的第 1 族。

## 问题

`std::cml` 现在拒绝 374 条用例（全量拒绝 1814 条的 **20.6%**，却是**最大的一族**），理由只有一句：

> `the operator '<uri>' without a printable name: '<name>' is a body-less kernel declaration, and no
> program writes that name`

被拒的原因是**模块把自己的 kernel 声明保持私有**：程序写 `1->shl<64>`，图里的算符是 `math:shl_l`，
而 `shl` 是无体的 kernel 声明（`CallForm::kernelDeclaration`），**没有任何程序写过这个名字**。

要问作者的是：**这类算符该不该有一个可写名字？** 给它一个名字，等于替模块作者决定"这个私有
kernel 可以从外面按名字调用"（设计原则 6.4 说那不是我该决定的）；不给，这 374 条永远打不出来。

## 读数

- 口径：语料 3377、`origin/develop = e8fa221a0`、`node test/tools/cml-sweep.mjs --jobs 16`
  （绝对路径），`printed=559 refused=1814 frontend=1004`。
- 该族计数 **374**，最小例子 `test/cases/modules/math_calls/err_shl_int.cml`（89 字节）：

  ```cml
  import { shl } from math

  func main(): int sync {
      println(1->shl<64>)
      return 0
  }
  ```

- 拒绝原文（`std::cml`，退出码 1）：

  ```
  error[SemanticDiag::CmlUnsupportedConstruct]: std::cml cannot print main: the operator
  'math:shl_l' without a printable name: 'shl' is a body-less kernel declaration, and no program
  writes that name.
    --> test/cases/modules/math_calls/err_shl_int.cml:4:19
  ```

- 同一族里还有 `:op/` 之外的模块算符（`array:sum_l` 背后是私有的 `sum_kernel`、
  `tensor:zeros` 等）。

## 三条路，各自的代价

- **A（给名字）**：模块声明 kernel 时带一个可写别名，打印器用别名。⇒ 打印器一侧可行，但这等于
  **把私有 kernel 变成公开可调**：语义是作者定的，不是打印器能定的。
- **B（维持拒绝，今天这样）**：374 条继续拒绝，理由说得清（现在的拒绝文本已经指名道姓说了为什么）。
  ⇒ 读数不变，谁也不被误导。
- **C（换写法）**：打印器把 `1->shl<64>` 打成函数调用而不是算符（若模块同时导出了函数）。⇒ 需要
  `CallForm` 之外的信息（"这个 kernel 有没有对应的公开函数"），而且 `shl<64>` 的括号实参仍无处记。

## 默认建议

**B：不动，先记着。** 理由：这一族的每一条都需要"私有 kernel 有无公开拼写"这个知识，而打印器没有
这个知识；A 会在打印器里替作者做语义决定，C 需要另一套声明层信息。等作者说清"kernel 与它的公开
拼写是什么关系"，再决定 374 条怎么走。

## 相关

- `docs/design/graph-store/plan-roundtrip.cn.md` 的"拒绝原因普查"一节（top 10 与口径）。
- `CmlPrinter::operatorExpression` 里的 `writable` / `kernelDeclaration` 两个判据点。
- 设计原则 6.4（不替作者决定语义）。