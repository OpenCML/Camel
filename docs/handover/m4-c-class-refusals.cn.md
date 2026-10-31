# C 类 18 条拒绝：新路线里各自该由谁说这句话

对象：`docs/handover/m4-autodiff-case-classes.cn.md` 的 C 类（18 条，全部是“拒绝”）。
问题不是“文案怎么写”，是**谁来拒绝**：库层路线里，拒绝只有三个来源——**`grad` 的签名（`when` 与
`Tangent<T>` 类型函数）**、**`reinterp` 的缺规则诊断（原则 9.9）**、**规则自身的类型检查**；
此外还有一个与求导无关的来源：**名字解析**。

三个数：**照旧 3 条 / 改写 9 条 / 消失 6 条**（3 + 9 + 6 = 18）。

| # | 用例 | 现在这句（来源） | 新路线里由谁说、落在哪 | 出这句话的条件 | 判定 |
|---|---|---|---|---|---|
| 1 | `grad_refuse_no_with` | `it has no \`with\` parameter, and grad differentiates the first \`with\` parameter`（`modules/autodiff/pullback.cpp:4971`） | `grad` 的签名：`Tangent<ParamsOf<F>>` 为空（`stdlib/signatures/autodiff.cml:45`），落在 `grad(...)` 调用处 | `F` 没有 `with` 参数 | **改写**（位置从 pullback 移到签名） |
| 2 | `grad_refuse_var` | `its first \`with\` parameter is a \`var\` of type {…}; gradients do not flow through …`（`pullback.cpp:4979`） | 同上，`Tangent<T>` 对 `var` 类型无定义 | 首个 `with` 参数是 `var`（3.5） | **改写** |
| 3 | `grad_refuse_string` | `its first \`with\` parameter has type {}, which has no gradient`（`pullback.cpp:4996`） | `Tangent<T>` 无 `string` 的规则，落在调用处的类型位置 | 首个 `with` 参数是 `string` | **改写** |
| 4 | `grad_refuse_function` | 同上（`has no gradient`） | `Tangent<T>` 无函数类型的规则 | 首个 `with` 参数是函数 | **改写** |
| 5 | `grad_refuse_struct` | `has type Plain, whose field \`s\` has type string, which has no gradient`（`pullback.cpp:4996` 家族，逐字段展开） | `Tangent<Struct>` 逐字段取，字段的 `Tangent` 无定义时点名该字段 | 结构某字段无切空间 | **改写**（点名仍保留） |
| 6 | `grad_refuse_tuple` | `whose element 1 has type string, which has no gradient`（同上，逐位置） | `Tangent<Tuple>` 逐位置取，点名位置 | 元组某成员无切空间 | **改写** |
| 7 | `grad_refuse_return` | `it returns {}, not a float`（`pullback.cpp:4967`） | `grad` 的签名：`ResultOf<F>` 必须是 `float` 类（`stdlib/signatures/autodiff.cml:45` 的 `when`） | 返回值不是 float | **改写** |
| 8 | `discrete_zero_refuse` | `'. Gradients do not flow through it: there is no derivative rule for …`（`pullback.cpp:3328`） | `reinterp<jvp>`：`Map`/`Set` 在 `stdlib/rulesets/jvp.cml` 里没有规则 → 缺规则诊断（4.1，9.9） | 值被放进 `Map<int, float>` 且参与了求导 | **照旧**（同一句“没有规则”） |
| 9 | `refuse_container_reshape` | `cannot differentiate operator 'tensor:reshape' … no operator carries a tensor's gradient back into one, … type 'float[][]'`（`modules/tensor/ops/vjp.cpp:315`、`:126`） | `reinterp<jvp>` 找 `tensor:reshape` 的 jvp 规则；容器类型上 `#transpose` 规则的 `when` 拒绝（`stdlib/rulesets/transpose.cml`） | `reshape` 的实参是容器（非数值数组） | **改写** |
| 10 | `refuse_container_concat` | 同上（`tensor:concat`） | 同上，`concat` 的容器部分 | concat 的数组部分是容器 | **改写** |
| 11 | `refuse_gradient_element_type_rule` | `of type 'float[]' … element type 'f32' … the derivative rule produced it`（规则产出校验） | 规则自身的签名检查：`#transpose` 规则的返回类型必须与参数元素类型一致（`stdlib/rulesets/transpose.cml`） | 规则产出的梯度元素类型与参数不符 | **照旧**（同一条检查，同一句话） |
| 12 | `with_grad_old_name` | `SemanticDiag::UnresolvedReference`（名字解析） | 名字解析，不变（`src/compile/gir`，与求导无关） | 用了已删除的 `value_and_grad` | **照旧** |
| 13 | `grad_function_value_branch` | 运行期拒绝：“被调的函数由分支在运行期选出” | **没有对应物**：新路线里闭包是普通值、调用是普通调用，`reinterp`/`transpose` 按被调函数的图走 | —（该程序变得可求导） | **消失** |
| 14 | `grad_function_value_static_branch` | 同上（常量条件也仍是分支） | 同上 | — | **消失** |
| 15 | `grad_function_value_parameter` | 同上（被调函数是被求导函数的参数） | 同上 | — | **消失** |
| 16 | `grad_function_value_container` | 同上（函数值从元组读出） | 同上 | — | **消失** |
| 17 | `grad_function_value_recursion` | 同上（递归把自己传下去） | 同上 | — | **消失** |
| 18 | `grad_function_value_passed` | 同上（依赖参数的函数值传给无参数函数） | 同上 | — | **消失** |

## 消失那 6 条要交代的事

这 6 条不是“文案换了”，是**新路线能求导，而旧路线拒绝**。它们必须改成正例（写出期望的梯度值并
与有限差分对照），**不许直接删**：删掉就没人钉住“函数值也能求导”这件事。这件事按 72 小时计划归
M2/M3 之后的库层 `vjp`，记为迁移时的第一批改写件。

## 两处“新路线里没有对应物”的说明

- **`grad_function_value_*` 六条**：旧路线的拒绝来自**展开式求导器**（`pullback.cpp` 把调用就地展开，
  展开时无法回答“运行期选哪个函数”）；新路线没有展开这一步，被调函数在图上是一个调用节点，闭包体在建图期已知。
- **`refuse_container_reshape/concat`**：旧路线按 `vjp` 规则表说“没有算子能把张量的梯度带回去”；
  新路线里 `reshape`/`concat` 的 `#transpose` 规则存在，拒绝条件收窄为“实参是容器”，由规则的 `when` 说。

## 判据

「拒绝先按‘谁来拒绝’分堆，再按文案排——文案是最后一层，不是第一层。」

## 边界

只写表，未改 `modules/autodiff`；诊断文案按迁移顺序最后才动。