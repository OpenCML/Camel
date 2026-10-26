# `never` 实参：`with` 实参域要不要同判；标记位（`T!`）要不要也从实参继承"绝不成值"

2026-10-26 裁定第 2 条（"一个实参是 `never` 的调用永远不会执行，所以这个调用本身的类型就是 `never`，不需要选重载，给一条警告指出是哪个实参"）落地后**实测剩下的两个口径**。已落地的部分：规范化实参域里有一个 `never` 就报 `SemanticDiag::NeverArgumentCall`、不选重载、调用类型即 `never`（`src/compile/gir/builder_operators.cpp:2629-2667`）；同裁定第 4 条的两个警告在 `src/compile/gir/builder_diagnostics.cpp:542`（`FailNotExecuted`）、`:561`（`StatementAfterFail`）。

## 1. `with` 实参域要不要同判

`with` 实参在调用处写成 `<...>`（`docs/language/07_statements.md:150-166`），它的类型在重载解析之前就已经算好（`src/compile/gir/builder_operators.cpp:2275-2279`）；而第 2 条的判定今天只读规范化实参域（`normInputTypes`，同文件 `:2147-2153`）。

逐字读数（`with <lo: int, hi: int> func clamp(x: int): int`、`with <v: int> func keep/ignore(x: int): int` 一类的声明）：
- `clamp<0, fail<'Bad'>('no upper bound')>(42)`（sync 体）：`check -O short` **零诊断、EXIT=0**；运行逐字 `error[RuntimeDiag::Bad]: no upper bound`（`5:25`）、EXIT=1。
- `keep<fail<'Bad'>('no value to keep')>(1)`：同样零诊断；运行 `error[RuntimeDiag::Bad]: no value to keep`（`5:21`）、EXIT=1。
- 非 sync 体、且调用结果没人要（`func main(): int { let y = ignore<fail<'Bad'>('nothing needs this call')>(1) … }`）：**不执行、EXIT=0**，并且第 4 条的 `FailNotExecuted` 报在**这个 `with` 实参**上：`warning[SemanticDiag::FailNotExecuted]: This 'fail' is not executed: nothing needs it and it is not in a sync block.`（`6:24`）。也就是说：第 4 条已经覆盖到 `with` 位，第 2 条没有。
- 结果类型不变：`func main(): int[] sync { return clamp<0, fail<'Bad'>('no upper bound')>(42) }` 逐字 `error[SemanticDiag::ReturnTypeMismatch]: Return type 'int' does not match the function's declared return type 'int[]'.`（`5:5`）——调用仍是 `int`；把同一个 `never` 放在规范化实参位（`main(): int[] sync { return double(stop('no value to double')) }`）则**接受**，因为那条调用已经判为 `never`。

- **问题**：`with` 实参也在调用求值之前交出值（上面第 3 条：它在链上就照旧发生故障、不在链上就不执行，和规范化实参同一个规则），按第 2 条的前提它是否也该同判？若该，是一起给三件事（同一条警告、不选重载——`withInputTypes` 正是解析器读的输入，`src/compile/gir/builder_operators.cpp:2475`——以及调用结果类型变 `never`），还是只补那条警告？
- 我们**没做**的原因（等你的话）：第 2 条的措辞说的是调用取的数据实参；`with` 是调用的配置，动它要顺带决定解析器与结果类型，超出那四条裁定的范围，所以只把这个缺口记在这里。

## 2. 标记位（`T!`）要不要也从实参继承"绝不成值"

`never` 值落在**没有标记**的期望位时，位置直接拿走节点本身（`src/compile/gir/builder_expr.cpp:1218-1224`，那里的注释自己写着"A tagged position still enters its tag below."）；落在**带标记**的位（`let x: int! = …`）时，同一函数走到末尾 `:1292-1296` 建一个 CAST 到标记类型（多级标记如 `int?!` 先走 `:1261-1268` 的 `coerceTo`）。

逐字读数（今天两边**看起来一样**）：
- `let x: int! = double(stop('no value to double'))`：`check -O short` 只有 `SemanticDiag::NeverArgumentCall`（`9:30`）、EXIT=0；运行先 `before`、再逐字 `error[RuntimeDiag::Stopped]: no value to double`（`4:16`）、EXIT=1 —— 那个 CAST **没有**把故障变成错误值，程序照样死掉。
- `let x: string! = double(stop('no value to double'))`：同上，零类型错误（`check` EXIT=0，警告在 `8:33`）+ 同一故障 —— 标记位对 `never` 不做任何检查。
- 没人要这个值时两边都不执行、都零诊断：非 sync 体里 `let x: int! = stop('tagged and unwanted')` 与把 `int!` 换成 `int` 的同一形状，都是 `check` EXIT=0、运行打印 `0`、EXIT=0。

- **问题**：标记位上那个 CAST 是**有意的**吗——即"带标记的期望位不从实参继承'绝不成值'，`never` 先被 CAST 成 `int!`"？如果不是有意的，是不是应当与第 1 条在同一处解决（`never` 对任何期望位一视同仁地穿透，`src/compile/gir/builder_expr.cpp:1218`）？
- 我们**没做**的原因：这属 3.9 的既有缺口，不是本次引进的——`builder_expr.cpp:1218-1224` 的注释与 `:1292-1296` 的 CAST 在这次改动之前就在（第 2 条只改了调用处的判定，一行都没动 `builder_expr.cpp`），不在那四条裁定内，所以只问不判。