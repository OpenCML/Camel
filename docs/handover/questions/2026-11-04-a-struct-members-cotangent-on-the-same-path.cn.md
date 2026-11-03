# 成员的余切按成员拼好了：struct 那半边要不要同路走

## 问句

`transpose` 现在把「一个**元组**参数的成员被逐个读」拼回该参数的余切（`transpose.cpp#buildTransposeMember`
的 `Port` 一格：读到的成员取它的余切，没读到的成员取 `0.0`）。**同一个口子的 struct 半边**要不要也拼
——`Tangent<Point>` 是成员切线的 struct（设计原则 9.9），于是 `vjp((qq: Point) => qq.a * 4.0 + qq.b)`
也答得出 `{ a: 4.0, b: 1.0 }`——还是维持今天的**按名拒绝**？

## 为什么这是一个问句而不是一次施工

- 元组那半边是**平**的：成员号就是槽号（`GCAccsBody::value` 对 `TupleIndex` 就是元素下标），所以
  「端口 + 槽表 → 一个元组」不需要记路径。
- struct 那半边**不平**：`GCAccsBody::value` 对 `StructKey` 是字段名的 `SymId`，不是槽号；要拼就得在
  `buildTransposeMember` 里加一层**路径**（哪个字段、嵌套 struct 里第几个字段、数组元素），
  `Tangent` 的 struct 成员顺序由声明定，不由读的顺序定。这是设计原则 9.9 的形状问题，不是一个循环。
- 今天的读数是**一句话**（不是崩、不是空）：
  `TRANSPOSE function=split_of_jvp_of_g_backward skip=unread-parameter port=5 type=Point`，
  exit 1，落点 `stdlib/vjp.cml#vjp` 的 `transpose(lin)`。

## 三条路

- **甲：拼**（路径化槽表）。好处：`vjp` 对「读 struct 字段」这个最常见的形状可用，读数从拒绝变
  `{ a: 4.0, b: 1.0 }`。代价：`buildTransposeMember` 加一层路径（字段名 → 槽位），嵌套 struct 与数组元素
  要递归；`Tangent<Point>` 的成员顺序要按声明而不是按读到的顺序拼；一处拼错就是**错数**，比今天的拒绝差。
- **乙：维持按名拒绝，只把句子改准**（「a parameter whose members the body reads」）。代价：`vjp` 对读
  struct 字段的函数不可用（常见形状）；好处：一句话说得清，且就是今天的读数，`split_struct_port` 与
  `split_struct_field_read` 两条守卫都不动。
- **丙：只在「每个字段都被读到」时拼，读到一半就拒**。好处：不做路径的完整实现；代价：答案依赖
  「读全了没有」，同一个函数改一行 body 就从答数变拒绝，读的人无法从类型上预期。

## 默认建议

**乙（维持按名拒绝），本 lane 不动 struct 那半边。**

理由：甲是**形状**层面的活（声明顺序、嵌套、数组元素），与 `2026-11-04-a-struct-field-read-refused-or-copied-by-key`
那页要裁的是同一块地；本 lane 的任务是**元组那条崩**（最坏一档），先把它按「对」修好并钉住读数；在
同一轮里顺手做甲的平表版本，等于用一个没量过的路径换一个已经说清楚的拒绝。要做甲时，它应当带着
「嵌套 struct / 数组元素 / 声明顺序」三条读数的用例一起进，而不是半个。

## 本 lane 顺带做死的两件事（不是问句，作者不同意就回退）

1. **编码器的空引用检查改活了。** `src/runtime/graph.cpp#emitDraftPayload` 的 `ASSERT(draftRef < size)`
   在 `NDEBUG` 下是 `((void)0)`，于是「一个节点的输入是空引用」读成 `runtimeRefsByDraftId[0xffffffff]`
   = +16 GB，只有信号、stdout 空。现在它是 `throw std::runtime_error`，句子点名**哪张图的哪个节点、
   哪一条输入表（norm/with/control）、表长与计划长度**，并由驱动按 `reportPassFailure` 的路子报出来
   （exit 1）。选 throw 不选 `GUARD`：`GUARD` 是 `fail_invariant` → abort（仍然是一档「崩」），而这里
   的失败是「一次 pass 造了编码器写不出的图」，该由那次 pass 认领，而不是 abort。
2. **`split` 一行没动。** 切本来就是对的（`split(jvp(g))` 单独跑 `forward y= 11.0` / `lin dp= 4.0`），
   动它会把今天答对的数弄坏。核对读数在新的 `test/cases/transforms/split/split_tuple_member_read.cml`
   里与 `vjp` 的三个读数并排钉着。