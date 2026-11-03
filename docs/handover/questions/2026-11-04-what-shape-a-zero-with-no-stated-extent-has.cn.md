# 类型没写宽度时，那个零的宽度是多少？

**状态**：待裁；我方默认见第 3 节（**甲：答 `[]`，并把宽度问题留给"成员拿到值"那一步**）。

写这页的直接原因：`vjp` 一个**常函数**在数组参数上崩（`d032bd123` 量到，基线 `34b659107` 也崩），崩在
`src/builtin/operators/struct.cpp#__tangent_zeros_arr__` 的那句
`autodiff: a zero tangent array needs an array with a tangent`。已经修好的是**算子名**与**操作数**两处
（第 1 节），修好之后 `float[3]` 答 `[0.0, 0.0, 0.0]`、`float` 答 `0.0`、而 **`float[..]` 只能答 `[]`** ——
剩下这一条就是本页要作者裁的一句。

## 1. 读数（实测，含树、时间、负载）

树 `/home/zjwei/camel-work/zerocrash`（`lane-zerocrash`，基线 `34b659107`），宿主 88zjwei（32 核），
2026-11-04 05:4x（本地）／`2026-11-03T16:39-05:00`（宿主），1 m load 34.59／实测跑探针时为 15.86。

1. **三种形状，改前**（`exit` 与两个流全文见车道报告）：

   | 形状 | 改前 | 改后 |
   | --- | --- | --- |
   | `float[..]`（`[2.0, 3.0, 4.0] as float[..]`） | exit 1，`autodiff: a zero tangent array needs an array with a tangent` | exit 0，`([])` |
   | `float[3]` | exit 1，同一句 | exit 0，`([0.0, 0.0, 0.0])` |
   | `float` | exit 0，`(0.0)` | exit 0，`(0.0)` |

2. **改前那次崩是两个缺陷叠在同一处**，不是一句算子名：
   * 算子名写死成 `:tangent/zeros_arr`。数组这个 code 是**两种值共用的**：数值数组的零是
     `tensor:zeros_like`（`modules/tensor/ops/gradient_ops.cpp#registerTensorTangentSpace`，`owns = isTensorType`，
     而 `isTensorType` 就是 `isNumericArrayType`），只有"每个元素占一个 slot 的数组"的零才是
     `:tangent/zeros_arr`（`src/builtin/operators/derivative.cpp#registerBuiltinDerivatives`）。所以
     `float[..]` 被送进了答不了它的 kernel。
   * 操作数是 `ports[port]`，**源图的**端口。被答余切的参数**不是**成员的端口（`buildTransposeMember` 里
     `portOfParam`：参与微分的参数"是答案在说的对象，调用方没有值可以传"），所以这个 ref 是另一套编号里的
     下标，它落在了余切端口上 —— 那个端口的类型是**结果的** `float`。这就是那句话里的 "a float"。

3. **为什么 `float[..]` 只有 `[]`**：成员的签名是 `(float) => (float[..],)`，被转置的函数被交的只有余切
   （作者 2026-11-02 11:30 的裁定），**成员里没有任何一个节点带这个参数的值**，也就没有它的长度。
   今天能写下来的零只有"类型写了多少"：`float[3]` 写了 3，`float[..]` 一个维度都没写 —— 一个维度都没写
   的零有 0 个元素，每个元素都是零（因为没有元素）。`[]` 不是瞎写：`Array::create` 的**空** extent 列表会
   答一个 rank-0 数组，而 rank-0 数组是元素本身（`include/camel/core/rtdata/array.h#createRankZero` 的注释），
   那会静默变成一个标量 `0.0`，比空数组坏。

4. **同一个事实代码里已经有一句裁过了**：`modules/transforms/transpose.h#TransposeRefusalCause` 的
   `ParameterShape` ——「一个被答余切的参数，律的右边要读它的**值**，而它的类型不写形状（`float[..]`、`float[]`）」，
   拒绝语是 `RuntimeDiag::TransposeNeedsParameterShape`，告诉写程序的人**把形状写进类型**（作者 2026-11-03 16:15）。
   今天这条只在"律要读值"时触发；本页问的是同一句话落在"**答案的零**"上时该不该一样。

## 2. 三条路

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲：答 `[]`（今天的实现）** | 零的宽度 = 类型写了多少；没写的维度是 0 | 答案是"全零数组"，但 `[2.0,3.0,4.0]` 的导数读成 `[]` 而不是 `[0.0,0.0,0.0]`。**降级是响的**：谁拿它去加一个 3 长的数组会当场形状错，不会静默出错的数。要把这一行换成 `[0.0, 0.0, 0.0]`，得让成员**拿到那个值**（下面两条之一） |
| **乙：按 `ParameterShape` 拒绝** | 类型不写宽度 ⇒ 拒绝，并告诉作者把宽度写进类型 | 常函数**是**可微的、导数**就是**零，为一个写对了的程序发拒绝；而"把 `float[..]` 改成 `float[3]`"对很多模型不可能（宽度是运行时的）。按严重性次序，`明确拒绝` 比 `错数` 高，但比 `对` 低 |
| **丙：让成员拿到那个值** | 值以**闭包端口**进来（`split` 把它当残差带上），或按签名多一个值端口（`lane-valports` 正在改的那一层） | 这是**唯一**能答 `[0.0, 0.0, 0.0]` 的路，也是 `ParameterShape` 那句话本来的出路。代价：`split.cpp` 要判"后半天要读这个参数"，或 `transposeSignature` 多一个端口 + 每个调用点填它 —— 都在本车道**不许动**的文件里，且与 `lane-valports` 的签名/值端口改动正面相撞 |

## 3. 我方默认

**甲**，理由三条：

1. **先把崩去掉，且不去掉"答对的"那一半**：`float[3]`、`float`、以及所有类型写了宽度的数组今天都答**精确的零**
   （`[0.0, 0.0, 0.0]`），这是能答对的；`float[..]` 是**同一处**的另一半，答 `[]` 不改变任何已经答对的读数。
2. **甲不撒谎**：它答的是"类型写了多少"。乙会对一个正确程序发拒绝，而拒绝语让作者改类型 —— 对
   `float[..]` 的模型参数那是错的建议；丙是正解但不在本车道能动的文件里。
3. **可换**：甲的那一行在用例里写死了 `open pullback(1.0)= ([])`，用例注释明写"成员拿到值的那天这一行读
   `[0.0, 0.0, 0.0]`，这一行会红" —— 换路时它自己会说出来。

**阈值写死**：若落地时发现"让成员拿到值"这件事**只需要动 `transpose.cpp` 一处**（不需要 `split.cpp`、
也不需要签名变化），就改丙，并把这条用例的 `([])` 改成 `([0.0, 0.0, 0.0])`。

## 4. 附：写死"造零 / 造常量"的普查（只报不改）

同一类缺陷是"**写死的算子名或类型 vs 读到的类型**"。`git grep` 过一遍，C++ 侧造零的地方如下
（`file#Symbol`）：

| 位置 | 写法 | 读到的类型还是写死的？ |
| --- | --- | --- |
| `modules/transforms/transpose.cpp#buildTransposeMember` 的答案段 | 原来是 `:tangent/zeros_arr`，操作数是**源图**的 `ports[port]` | **本车道已修**：算子走 `TangentSpace::zerosLikeUri`，零由 `zeroOfType` 按类型写 |
| `modules/transforms/transpose.cpp#buildTransposeMember` 的 `zeroOf`（循环状态/逃逸那一支） | 原来是 `:tangent/zeros_arr` | **本车道已修**：改走 `findTangentSpace(...)->zerosLikeUri`（操作数本来就是本图的节点，是对的） |
| `modules/transforms/transpose.cpp#buildTransposeMember` 的 `accumulate` | `type->code()==Array ? ":tangent/add_arr" : ":op/add_d"` | **同事不同病**：`add_arr` 也是写死的数组算子名，而数值数组的和是 `tensor:add`（`gradient_ops.cpp#registerTensorTangentSpace` 的 `addUri`）。今天没有已知红，**未改** |
| `src/builtin/operators/derivative.cpp#idxArrayVjp` | `":tangent/zeros_arr"` | **同一类**：操作数是同图的 `call.inputs[0]`（对），算子名写死（错：数值数组该是 `tensor:zeros_like`）。`t[idx]` 是 core 的读，所以今天多数场合恰好是 slot 数组；但 `float[..]` 的 `t[idx]` 走这里就会撞同一句话。**未改**（不在本车道文件内） |
| `src/builtin/operators/derivative.cpp#idxTakeArrayVjp` | 同上 | 同上 |
| `modules/tensor/ops/layout_ops.cpp#takeVjp` | `":tangent/zeros_arr"` | **同一类**：三词 `take` 的零；操作数对、算子名写死。**未改** |
| `stdlib/rulesets/jvp.cml#zero_tangent` | `match type T { case float[..] => float(zeros_like(x)) ... }` | **写死的是元素类型的清单**（`float`/`f32`/`f16`/`bf16`/`f32[?]`/`f64[?]`），新元素类型要在这里加一行；`zeros_like(x)` 本身是对的（有值）。**未改** |
| `stdlib/rulesets/transpose.cml#index_into` 的律 | `scatter_into(zeros_like(xs), i, ct)` | 对：零取自值 |
| `stdlib/optim.cml#zeroLeaf`（`f32[?]`/`f64[?]`/`_` 三分支） | 写死元素类型清单 + `zeros_like(x)` | 同 `zero_tangent`，**未改** |

`Fill` 一路（`addFillNode` + `GCFillKind::Tuple/Struct/Array`）造的是**聚合值与 payload**，不是"某类型的零"，
所以不在这张表里；`include/camel/compile/gir/…` 与 `modules/autodiff/pullback.cpp` 的 `Fill` 用法都带上了
真类型，未见写死。

**结论一句**：`transpose.cpp` 这两处是唯一"既写死算子名、又拿错图的下标"的一处（所以它崩）；其余几处写死的是
**算子名**（数值数组的零/和），不会崩成那句话，但会在 `float[..]` 走到它们时撞同一类墙。

## 要作者答的一句

**「一个被答余切的参数，类型不写宽度（`float[..]`），而转置后的函数只拿得到余切 —— 答案里那个零的宽度是 0
（答 `[]`，今天的实现），还是按 `ParameterShape` 拒绝并让作者把宽度写进类型，还是先把这个值以闭包端口/值端口
带进成员再答参数的宽度？我默认『甲：答 `[]`』，并在 §3 写死了换丙的阈值。」**