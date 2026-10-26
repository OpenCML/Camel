# 2026-10-26 晚间问题的裁定

## a-void-arm-and-a-cell-typed-arm-collide

**单元格（cell）不是值，任何取值的位置都先读出它的值。** 定为 (a)，再加一条：
1. **不取值的位置，汇合处不取值、丢掉臂的值**，与"没写 `else` 的 `if` 汇合处不取值"是同一条规则。`void` 函数里的 `return if … then null else (x = x + 1)` 就属于这种位置：两臂的效应照常发生，没有值要汇合。
2. **取值的位置，单元格先读出它的值再参与汇合**。一臂是单元格 `var T` 时，按 `T` 汇合，`void` 臂照原则给 `null`，得到 `T?`。单元格类型永远不进入汇合、实参值或绑定。
   - 这两条一起修掉 `coerceArmExit` 的段错误。
   - Release 下被编掉的那条断言，改成一条带位置的内部错误，不能再静默变成空指针。
3. **写捕获的调用交出的版本只在被读取处物化**（即 (c)）。它们是图上的状态边，不是用户可见的值类型，所以不和 (1)、(2) 冲突。
4. **Debug 下 `Node::ruleBinding` 被用在 DECL 上的断言是真缺陷**，你们修：`isGenericHere` 对 DECL 不该走 rule 绑定。

## eight-shape-assertions-and-one-frozen-reference-after-the-sync-edges

1. **形状断言改成断言不变量，不断言旧形状**。
   - 断言这些：没有不等待任何东西又没被读的 GATE 存活；会等待的 GATE 都在；每条链尾有自己的 GATE；代数策略的结果与不变量一致。
   - 不断言具体节点数和位置。
2. **再也产不出来的冻结参照直接删掉**，连同引用它的检查。不设"已知不可再生"的标记，不留测不出东西的测试。
3. 双写只留在车道分支上，同意。删控制边那一大批要一次提交落地，不进 develop 的中间状态。

## holed-composite-literals-do-not-take-the-expected-type

**期望类型在参数位置、声明位置、箭头接收者位置走同一条路传进复合字面量的每个元素，包括含洞的字面量。**
- **字面量元素**取期望类型。
- **计算出来的元素（洞）**不做转换，只检查它是否已经是这个类型。
- 所以 `[1.0, float(n)]` 放在 `f32[]` 的位置，报 `ElementTypeMismatch` 是对的：`float(n)` 是 `float`。但三处必须报同一条诊断，指向那个洞，而不是"找不到匹配的函数"。
- 正例是 `[1.0, f32(n)]`，在三种位置都要通过。

照这个改 `compositeEntersIn`，复用 `compositeWithHolesInType`。§15 的 `entering-operator-args` 就按这一条结案。

## is-a-doc-comment-after-a-blank-line-still-a-doc-comment

**文档注释的归属由注释的语法决定，不由空白决定。**
- `///` 和 `/** */` 属于其后的第一个声明，中间有没有空行，AST 都一样。改解析器，那 21 个用例不动。
- 普通的 `//` 注释不归属任何声明。
- 那 2 个 `native_declarations` 的输入，按它们自己那一项的裁定处理，与本条无关。

## module-name-citations-after-module-removal

同意。引用指向文件里的具体声明，不指向模块名；这一处的改法就是规则。

## three-uncovered-questions-from-the-axis-slice-revival

1. **内建声明由前奏的声明文件编译**，取 (1)：声明模块是签名的唯一来源（原则 6.4），内建的也一样，不回到 C++ 注册。基准里"`library` 必须为空"改成不计前奏：前奏总在，它不是用户导入的库。前奏正由决策者这边换成 `std.cml` 加 `prelude.signatures.cml`，文件名以落地的为准。
2. 同意，检查改到真有 C++ 规则的地方。
3. **`xs->slice<a, b>` 删掉，不留糖**：今天已裁定，同一件事不留第二种写法。前奏的接入（决策者这边的车道）已经把 `slice`、`head`、`tail`、`extend` 的调用处全部迁到 `xs[a:b]`、`xs[0]`、`xs[1:]`、`concat`，那 27 个文件不用你们改；前奏合入时连同降级的糖一起删。前奏草稿里的 `slice` 是另一个函数（带 `axis` 的按轴切片），不是这个拼写。
4. **前奏声明被拒，`camel check` 失败**：报出了错误，命令就以非零退出，到处同一条规则，前奏也不例外。

## 合并备注（b7ad5258c）

- `attempt` 改为函数变换那条车道（4775c956c）收下。合并时把它新写的 `?->` 改成 `->`，lambda 的 `sync () => {…}` 改成 `() => sync {…}`。今后从 fsh-dev 开新车道，避免带进已删的写法。
- **语言参考英文版缺 6.10.1 故障隔离一节**，中文 `06_expressions.md` 有，英文 `en/06_expressions.md` 没有，而 `en/04_execution_model.md:243` 还链接着它。按中文补上，按 `attempt(f)(x)` 的写法。

## 合并备注（onnx 命名实参，4e77ad6a5）

- 181 处调用改成命名的 `with` 形式，收下。合并时 `export_impure.cml` 用 `f64(...)` 取代已删的 `cast(..., "f64")`，期望的列号相应改为 87。
- **三条过渡规则现在就可以删**：核心缺口已经在 fsh-dev 上修好（`264fa733c`：有默认值的 `with` 参数可以省略，有声明的原生算子从声明取名字和默认值）。`onnx:export_model` 改成声明里的一条 `with <out: string, dynamic_axes: int[]? = null, target: string? = null>`，删掉三条规则。
- **流程**：`08d1e5377`、`ce5e732b5` 两个 `wip:` 提交又进了 develop。车道上的过渡提交合并前压掉，这一条请协调者在合并前检查。
