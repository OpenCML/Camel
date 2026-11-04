# 放宽 `Float64` 这道闸门时，十余处写死的类型会在同一刻变成缺陷

一页一问：**`lane-f32trans` 放宽转置的 `float` 闸门，要不要在同一笔里把这十余处的读法一起改掉？**

## 事实

- 闸门只有一处：`transpose.cpp#transposeSignature` 的
  `if (function->exitType() != Type::Float64()) return nullptr;`（`transpose_operator.cpp#transposeOperatorInfer`
  拒绝时说的是同一句话）。所以今天凡是能走到 `buildTransposeMember` 的函数，退出类型都是 `float`。
- 于是这个文件里十余处 `Type::Float64()` **今天写的都是真答案** —— 普查里它们的判词是“恒等，
  但其条件是闸门”，而不是“恒等”。
- `lane-f32trans` 正在待合队列里：它一落地，那些处**在同一刻**从“恒等”变成“静默答错”，而
  **今天没有任何一条用例会响**（所有用例的退出类型都是 `float`）。名字错了会崩得响，
  类型错了会安静地把 f32 当 f64 读。

## 施工清单（开闸门的人逐行核）

| `file#Symbol` | 今天写的 | 闸门开了之后该读的 |
|---|---|---|
| `transpose.cpp#transposeSignature` | `exitType() != Float64()` 才收 | 收“退出类型有切空间”的函数，余切类型取 `tangentSpaceOf(exitType()).space`；没有切空间才拒 |
| `transpose.cpp#addUriFor` | `:tangent/add_arr` / `:op/add_d` 两个名字 + `addType = Float64()` | `space.space->addUri` 与 `space.space`；没有切空间 = 没有余切，**拒绝**，不是默认 `float` |
| `transpose.cpp#cotangentTypeOfResultType` | `result == nullptr` 或 `!hasSpace()` → `Float64()` | 两种都是“没有余切空间” → 拒绝。这个函数自己的注释已经写明它只“问类型、不重述闸门”，两个回落是它没做完的那一半 |
| `transpose.cpp#buildTransposeMember#accumulate` | `space ? space : Float64()`，再按 `code()==Array ? ":tangent/add_arr" : ":op/add_d"` | `space->addUri`（数值数组的和是 `tensor:add`）；`space == nullptr` 是调用方的错，应拒绝 |
| `transpose.cpp#buildTransposeMember#zero` | `toSlot<double>(0.0)` 配 `Float64()` | 零点按真实切空间的宽度写（f32 是 `toSlot<float>(0.0f)`），类型取该空间 |
| `transpose.cpp#buildTransposeMember#zeroOf` | 硬名 `:tangent/zeros_arr` | `space->zerosLikeUri`（注册表；数值数组是 `tensor:zeros_like`）。与 `zeroOf` 同族的另一处已由 `lane-zerocrash` 修掉，这处是同一条 |
| `transpose.cpp#buildTransposeMember#cotangentTypeOf` 及其十余个调用点的默认实参 | 每个调用点传 `Type::Float64()` 当兜底 | 去掉默认实参：ref 不在源图里 = 拒绝（“拿 draft ref 去问源图”是普查第 5 类），有类型没切空间 = 该位置没有余切 |
| `transpose.cpp#buildTransposeMember#addUriAndType` | 出参 `addType = Type::Float64()` 先占位 | 由 `addUriFor` 填；占位值一旦被读到就是 f32 被当 f64 |
| `transpose.cpp#buildTransposeMember`（闭包成员端口） | `declared != nullptr ? declared : Float64()` | 成员的声明类型（`function->funcType()->normTypeAt(i)`）；读不到 = 拒绝。2026-11-02 这个位置已经因为“端口类型来自字面量 float”修过一次（`docs/handover/reports/2026-11-02-the-port-type-came-from-a-literal-float.cn.md`），这是它的复发点 |
| **`transpose.cpp#buildTransposeMember`（被调方答案类型）** | `calleeExit != nullptr ? calleeExit : Float64()`，多答案时 `TupleType::create(answerCount, Float64())` | 答案**已经在手边**，而且是从函数读来的（不是字面量）：同处的 `calleeSignature = transposeSignature(calleeToTranspose->funcType(), …)` 与 `calleeExit = calleeSignature->exitType()`。两个回落只在 `transposeSignature` 答 nullptr、或答案元数对不上 `answerCount` 时才走到 —— 那是“这个被调方没有可转置的签名”，应当是**拒绝**，不是造一个 `N` 个 float 的 tuple。开闸门那一笔要先把“为什么会有 nullptr 的被调方”回答掉，再让这两个回落变成拒绝 |
| `transpose.cpp#buildTransposeMember`（被调方 FUNC 节点） | `addFuncNode(transposed, Type::Float64())` | 该图自己的函数类型：同函数里 `draft.setFuncType(signature)` 用的那个 `signature`（`transposeSignature` 的答案），不是 `float` |
| `transpose.cpp#buildTransposeMember`（loop 状态余切端口） | `vector<Type *>(stateCount, Float64())` 加两处 `Float64()` 兜底 | 每个端口 `cotangentTypeOf(regionPorts[1 + i])`，无默认 |
| `transpose.cpp#buildTransposeMember`（组件读 `component`） | 越界时 `Float64()` | 组件的类型来自被读的值；缺失是构造错误 → 拒绝 |
| `transpose.cpp#buildTransposeMember`（数组参数零） | `portType == nullptr \|\| space.code() == Float64` → `zero()` + `Float64()`；数组时硬名 `:tangent/zeros_arr` | 按 `space.space` 的类型与 `zerosLikeUri` 建零；f32 走同一句 |
| `transpose.cpp#buildTransposeMember`（join 先建后改） | 先 `Float64()`，随后改写 | 今天恒等（用前已改写）；开闸门后要确认**每条路径**都改写，否则留下一个 float 的 join |
| `transpose.cpp#ruleTakesOperands` | 操作数类型为空时补 `Float64()`，失败再 `{Float64()}` 重试 | 空的类型表就是“没有操作数”，不该编一个 float 出来；与 `rule_expand.cpp#expandRuleByName` 的重试同族（普查里单独点名的那条） |
| `transpose.cpp#primitive` | `Type::Float64()` 的包装 lambda | 死代码（构建报 unused variable）：删掉，或让它接类型 |

## 顺序要求

**闸门和这张表必须在同一笔里改。** 两个方向都不行：
- 只开闸门 → 表里每一行都变成静默错答（f32 值按 f64 读、数组余切按 `:op/add_d` 求和），
  而**没有一条用例会响**：今天所有用例的退出类型都是 `float`。
- 只改读法 → 闸门没开，一行都走不到，改完无法验证：这就是“数字在动、东西没动”。

## 建议

1. 默认建议：**在 f32 转置落地的那一笔里，同一笔把上表逐处改掉**，并把表当 review checklist
   （缺一行就退回）。不是“以后再说”——以后就不会有人说。
2. 那一笔至少带四条用例，正好是“漏了哪一行会红”：f32 标量的转置；f32 闭包成员的端口类型；
   非 float 宽度的 loop 状态余切端口；数组退出的 `pick` 类（零载体那条）。
3. 这一页只记录，不改代码：改读法而闸门不动不可验证，改闸门是另一条车道的活且必须带 f32 用例。
   所以它是交给**开闸门的人**的施工清单，不是“本车道待办”。