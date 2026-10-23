# 问题：两处需要裁定（协调者 2026-10-23）

两份都来自今天合并后的实测，不是推测。**Q1 是设计原则与实现的直接冲突**；**Q2 是一条语言规则挡住了 10.4 的差分测试**，顺带牵出一个我没法自己解释的数值异常。

## Q1 9.3 的遍参数：字符串「取原文」，还是必须是常量表达式？

**原则（规范）**：`docs/design-principles.cn.md:524`

> 命令行上，参数写成跟在遍名之后的 `key=value`：`camel m.cml std::opt safe_fp=true onnx::export out=model.onnx std::fvm`。含 `=` 的词是参数，归属于它前面最近的遍；**值按参数声明的类型解析（字符串取原文），不需要引号**。

**实现**：真解析器拒绝那个例子。

```
$ camel m.cml onnx::export out=model.onnx
InvalidPassParameter … the value of 'out' is not a constant:
only bool, int, float and string constants are allowed
```

**今日的实际写法**（`docs/cli.md:170`、`AGENTS.md`「Pass Execution Model」）：整词加引号让内层引号活下来——
`camel m.cml 'onnx::export out="model.onnx"'`。

**发现方式**：新守卫 `test/tools/check-doc-pass-forms.mjs`（`docs.word_form_only`）把 452 页里的 **246 个调用 / 38 个不同 spec** 交给真解析器逐条跑，这一处被判为拒绝；守卫按句豁免并写明理由，**没有改任何语义或规格来变绿**。

**两条路**：

1. **按原则改实现**：`string` 声明的参数在命令行上取那个词的原文（`out=model.onnx` 合法），`bool`/`int`/`float` 仍要求常量表达式（`growth_budget=2*8+1`）；含空格的字符串照旧加引号。代价：同一件事有两种可接受写法（`out=m.onnx` 与 `out="m.onnx"`），要写清它们等价；`docs/cli.md`、`AGENTS.md`、守卫的用例与计数都要跟着动。**收益**：原则写的那句便利成立，`out=model.onnx` 是用户会打出来的东西。
2. **按实现改规格**：把 9.3 那句与例子改成「值必须是常量表达式；字符串写 `out="m.onnx"`」，与 `cli.md` 一致。代价：=最小改动，只是「不需要引号」这条便利取消。

**我倾向 1**（原则是规范，且这句承诺本身就是它的理由），但这属于 9.3 的规格，请你定。

## Q2 10.4：给**别的模块**的算子写内核，谁能声明？

**规则现状**：一个被标注函数的核，必须是**该函数自己模块**里的无体声明；测试模块只能声明自己协议的算子——`SemanticDiag::NativeDeclarationForeignOperator`，`src/compile/gir/builder_modules.cpp:397-460`。

**后果（实测）**：用例**无法**给一个函数标上 `tensor:sum` 之类的张量核，于是

- **融合**：`rules::fusion` 唯一的张量规则是 sigmoid 链，没有核就不能在用例里走到，融合路径的 kernel/body 一致性**无法比较**；
- **ONNX 导出**：导出器要求被导出函数的参数是张量（`modules/onnx/exporter.cpp:3872`），所以导出路径也**无法比较**。

今天已经合入的差分保证（`test/cases/compile/native/impl_agreement.cml` 等）覆盖的是**普通调用**与 `std::opt`（新加的 `impl.pass.optimizer.run` 在四个后端上跑通，值一致）。**还差的正是融合与导出这两处**，而它们是最容易悄悄分歧的地方（导出器走一条路、运行时走另一条）。

**两条路**：

1. **`stdlib/` 里放一个 stock `#impl`**（在算子的声明边上）：真实使用者会出现，核与体在同一模块里，规则不动。代价：标准库里第一次出现 `#impl`，要挑一个值得的算子并配内核实现。
2. **测试作用域地允许声明别的模块的算子**：例如测试模块可以用一条显式许可换一次外部核声明。代价：放宽了一条正在保护「核属于它的模块」的规则，要写清许可的边界。

**顺带一个我没有解释的观测，可能是同一个根因**：把一个 `with` 函数用 `:op/div_d` 内核标注后，`grad` 在**核路径和体路径上都打印 2.0**，而那个核登记的规则按说是 0.5；**剖析和图都显示跑的是内核算子，值却没跟上**。报告者没有把这个用例提交，而是选择不把不懂的东西钉成断言——我同意。**如果 10.4 的「体就是语义、核只是标记、差分测试保证两者一致」成立，这条要么是实现缺陷，要么是我们对「核的规则何时生效」理解有偏差，请一并裁定或指派排查。**