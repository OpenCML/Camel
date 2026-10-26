# 错误体系文档改写（设计原则 3.9）

本目录按仓库路径镜像放着改写后的文件，直接覆盖到仓库同名路径即可。**没有构建环境，所有新示例的输出都是按语义推出来的，没有跑过**：覆盖之后，每个示例先用 `camel` 跑一遍，把输出与页面核对（尤其是 `07_attempt.cml`、`07_pipeline.cml`、`07_exercise.cml`）。

## 教程

教程里讲缺失与失败的是**第 7 章**（`07_missing_and_failure`），不是第 9 章；第 9 章是"按依赖求值"。单文件版 `tutorial.*.md` 的"9. 错误处理"一节同步改了。

| 文件 | 改动 |
|---|---|
| `docs/tutorial/07_missing_and_failure.cn.md`、`.en.md` | 整章重写：开头先讲为什么（哨兵值与异常各自的毛病）；`T?`；`T!`（`return error(...)`，没有 `throw`，`return` 只给出所在块的值）；**流水线一节是中心**（`text->parse_int->check_age->decade !! 'unknown'`，规则、由被调函数签名决定是否拆开、末尾处理一次）；故障；`fail`；`attempt(f)` 作函数变换与三条边界规则；"三件事，三种写法"对照表；图上；两道练习与答案 |
| `docs/tutorial/examples/07_errors.cml` | `throw` 改为 `return` 加 `error(...)`；`?->` 改为 `->` |
| `docs/tutorial/examples/09_errors.cml` | 同上（单文件版教程引用它） |
| `docs/tutorial/examples/07_pipeline.cml` | 新增：流水线例子 |
| `docs/tutorial/examples/07_attempt.cml` | 新增：`fail` 与 `attempt(f)` |
| `docs/tutorial/examples/07_exercise.cml` | 加第二道练习（`in_range` 与管道） |
| `docs/tutorial/tutorial.cn.md`、`tutorial.en.md` | 第 9 节（错误处理）改写，指向第 7 章的新例子 |

`07_nullable.cml`、`07_fault.cml`、`09_fault.cml`、`10_nullable.cml` 不变。

## 语言参考（`docs/language/` 与 `docs/language/en/`，中英各一份）

| 文件 | 改动 |
|---|---|
| `06_expressions.md` | **6.6 重写**：`?->` 删除，`x->f` 对 `T?`、`T!` 的传递（由 `f` 的签名决定拆不拆，两层由外往里）、Rust 的 `Option`/`Result` 模型、缺失时给错误写成分支的两条臂；例子改用 `->`；**6.10.1 重写**：`attempt(f)` 是函数变换（`attempt(f)(x)`），三条边界规则，例子改写；其余提到 `?->`、`throw` 的句子（分支列表、lambda 写在 `->` 右边、`return` 在臂里的说明、`try`/`catch` 段、优先级表第 11 行）同步改；英文版原来**没有** 6.10.1，这次补上 |
| `07_statements.md` | **7.3 改名并重写**为"`return` 与 `fail`"（锚点 `#73-return-与-fail` / `#73-return-and-fail`）：删 `throw` 及其两个诊断说明（`ThrowOutsideFallible`、`ThrowNotAnError`），`return` 是块的值（不离开外层的块）、`error(...)` 返回、`fail<'Kind'>(message): never`；例子改写；7.1 的"使用这个值"、7.4 的"不能重载的"列表同步 |
| `03_data_model.md` | 3.4 的例子与规则（`throw`、`?->`）、"错误与故障"一段（加 `fail`、`attempt`），删去"当前实现的行为，尚待确定"一句里的 `?->` |
| `02_lexical.md` | 行首续行的运算符表删 `?->`；关键字表删 `throw`；运算符表 `??` `!!` 一行；"不是关键字"一行加 `throw`、`raise` |
| `04_execution_model.md` | 分支节点表的一行 |
| `08_top_level_and_grammar.md` | 两处语法：`retStmt` 去掉 `throw`，`link`/`linkExpr` 去掉 `?->` |
| `01_introduction.md`、`README.md` | 章节摘要里的 `return`/`throw`、`??`/`!!`/`?->` |

## 没动的地方（需要你知道）

- `docs/reference/camel-for-ai*.md` 里还有 `?->`、`throw` 与两个诊断名 `ThrowOutsideFallible`、`ThrowNotAnError`：不在本次范围（范围是教程与 `docs/language/`），也含生成的文件，应由生成器重生成。
- `docs/tutorial/07_missing_and_failure.*.md` 的"在图上"一节提到的 `UnusedFallibleValue` 警告原文沿用旧章，没有重新生成。
- 技术文档 `docs/technical/33_fault_isolation.md` 没有看，`attempt` 的链接仍指向它；若它还写着旧形式 `attempt(f, a, b)`，需要同步。

## 写法上的几个决定

- 示例里构造错误一律写 `return if … then error('Kind', '…') else value`，靠"`error(...)` 进入 `T!` 位置一律是失败那一臂"，没有用 `return error(...)` 放在表达式里的写法（只在 6.6、7.3 与教程正文各出现一次）。
- `attempt` 的例子只打印值和 `kind`/`message`，除了沿用旧例子的 `DivisionByZero at main.cml:1:38`（那一行的位置没变），没有新写任何诊断原文。
- 故障的种类名取 `InvalidArgument`（与 `fail` 的用法一致），教程里 `InvalidArgument: whole must be positive` 一行是按"错误值的 `kind` 是故障的种类名，`message` 原样"推出的。
