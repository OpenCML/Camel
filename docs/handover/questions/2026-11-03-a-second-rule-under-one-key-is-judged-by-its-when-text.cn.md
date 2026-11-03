# 同一个键下挂两条规则，今天按 `when` 文本判重，与裁定「按操作数类型」不一致

## 问什么

作者 2026-11-03 10:10 的裁定是：`a9061b77a` 已经让一个键下能挂几条规则，由引擎按**操作数类型**选择，
`err_duplicate_head` 只拒绝两条规则的**参数类型有重叠**的情况。

在转置那条车道上实测：两条参数类型**不重叠**的规则挂在同一个键下，仍然被拒。所以要么改索引的判重口径，
要么由转置那一处自己按操作数类型派发。问的是走哪条。

## 已量到的

**① 拒绝原文**（`stdlib/rulesets/transpose.cml`，第二条规则写在 `:233`，`feat/compile/transpose_array_product`
整条计划红，模块带错误、`vjp` 与用例连坐）：

```
[Error]: The rule 'mul_tensor_t' and the rule 'mul_tensor_scalar_t' both have 'mul_tensor' with
2 operand(s) as the head of their left side, and one ruleset has one rule per operator.
```

**② 两条规则的完整左式**（`tensor:multiply` 的两种形状，参数类型逐位不重叠）：

<!-- camel-snippet: fragment: the two left sides of one operator's two shapes, side by side -->
```camel
// 已落库的那条（array × array）
export rule mul_tensor_t::<T>(x: T[..], w: T[..]) when is_float::<T>() {
    mul_tensor(x, w) => (ct: T[..]) => (ct * w, ct * x)
}

// 被拒的那条（array × number）：参数 (T[..], T) 与 (T[..], T[..]) 第 2 位不同
export rule mul_tensor_scalar_t::<T>(x: T[..], c: T) when is_float::<T>() {
    mul_tensor(x, c) => (ct: T[..]) => (ct * c, sum(ct * x))
}
```

**③ 为什么必须是两条，而不是一条覆盖两种形状**：`tensor:multiply` 对 `(T[..], T[..])` 与 `(T[..], T)`
是同一条算子（`modules/tensor/ops/elementwise_ops.cpp`：两个形参都是 `ParamKind::TensorOrScalar`），
但两种形状的**律不同**，不是同一个右式：

- 数组位的余切是 `ct * w`，一个逐元素乘法，形状是结果的形状；
- 标量位的余切是 `sum(ct * x)`，一个**归约**，因为广播出去的那个数收的是它喂过的全部元素的余切之和
  （引擎自己的 `multiplyVjp` 用 `accumulateOperand` 做同一件事，它对标量位走 `tensor:sum`）。

并集形参写不出来（`func f(x: T[..] | T)` 在定义体里是语法错误：`Expected ',' or ')' before '|'`，
`docs/handover/questions/2026-11-03-one-rule-key-cannot-hold-an-array-and-a-number.cn.md` 已测），
所以「一条律」这条路今天不存在。

**④ 判重口径是 `when` 文本，不是操作数类型**（`src/parse/ast/rule_head.cpp:335-341`）：

```cpp
        // Two rules of one operator that cannot be told apart -- one spelling, one operand count,
        // one condition -- are a duplicate: neither can be preferred, and a family that holds both
        // would answer by filing order. Two that differ in their condition are not: the engine
        // instantiates each at the operand's type and at most one of them answers.
        if (slot.condition == rule.condition) {
            return slot.rule;
        }
```

两条的 `when` 都是 `is_float::<T>()`，逐字节相同，于是被判成"分不开的两条"。而两种形状的差别在
**操作数列表**、不在条件：任何诚实的条件文字对两条都相同，所以**无论条件怎么写都过不了这一关**。
`include/camel/parse/ast/rule_head.h:64-68` 把条件当成"真正的重复"与"两份不同的律"之间的判据，
这条读法在形状不同的两条上不成立。

**⑤ 再往后还有第二道同向的闸**：`transpose.cpp:1952` 用 `transposeRuleOf` 要规则，而它是单数
（`src/core/forward.cpp:174` → `ruleIn` → `lookupRule`），`lookupRule` 在一个键挂多条时答
`NoRuleForPrimitive`（`rule_head.cpp:378-384`：*"One answer when the key holds one rule…"*），
转置的走法把非 `Found` 一律按 `skip=rule … reason=no-rule` 拒绝。

**⑥ 也就是说**：`a9061b77a` 把索引改成"答名字、由引擎按类型派发"，接到了 `jvp.cpp#buildRuleTangent`
（`forwardRulesOf` + `portsTakeOperands`，逐参数 `Type::equals`），但**没有接到 `transpose.cpp`**，
而判重的口径留在 `when` 文本上。**这条裁定的前提在今天的树上不成立。**

## 不知道的

- 索引若按操作数类型判重，要不要把两条规则的类型元组在索引里物化（那样索引就要会读类型、会比较类型，
  这是它今天不做的事：它只读名字、操作数个数与条件文字）。
- 一条键挂多条之后，`arityOfHead` 的"同一拼写只有一个 arity"读法是否也要跟着放宽（本次没有遇到，
  两种形状的 arity 都是 2）。

## 默认（按此往下做，不等回复）

**②：由 `transpose.cpp` 按操作数类型派发到不同的律**，索引与判重口径都不动。

- 做法：`transpose.cpp` 要规则时改问候选集合（照 `forwardRulesOf` 的样子给 transpose 加一个复数版本），
  按 `portsTakeOperands` 同样的形状检查（逐参数 `Type::equals`）挑出能绑上的那一条；一条都绑不上时，
  用它已经有的 `skip=rule` 具名拒绝。
- 代价：只动 `modules/transforms/transpose.cpp` 一处；与 `jvp.cpp` 已有的派发同形，两处读法一致。
  不动索引，因此 `float(x: float)` / `float(x: int)` 那类"同 spelling 不同条件"的多规则不受影响。
- 为什么不是 ①：把"类型比较"塞进索引，是让一个只读名字与条件文字的索引去读类型；改动面大、影响所有
  家族与所有 `lookupRule` 的调用者，而收益只有"让形状不同的两条共存"这一种情况。
- 在此之前，`mul_tensor_t` 只挂 array × array 一条（已落库），array × number 的形状保持今天那句拒绝，
  记在下面「缺口」。

**拒绝比错数好**：把标量位的余切写成 `ct * x` 并指望类型系统把结果压成标量，会在广播形状上给出错数；
这一页存在的理由就是先写下这个判据，再动手。

## 缺口（今天红在哪）

- `tensor:multiply` 的 `(T[..], T)` 形状：无法转置。`sum(w * w)`、`sum(xs * w)` 这类只用数组位的句子不受影响
  （已通），但 `xs * 2.0` 之外的"数组乘一个活的标量"（`xs * s`，`s` 是参数）仍停在 `skip=rule`。
- 同一形状问题在 `tensor:add` 上没有出现：两操作数同型，一条律 `(ct, ct)` 就够（已落库）。
- `tensor:subtract` 的 `(T[..], T)` 是同一类缺口，`jvp` 侧早就记在
  `stdlib/rulesets/jvp.cml` 的 `jvp_subtract` 注释里。

## 读数（本机不构建，全部走主机 88zjwei，树 `~/camel-work/mult`，基座 `b63227534`）

| 项 | 读数 |
| --- | --- |
| 被拒的那两条 | `(T[..], T[..])` 与 `(T[..], T)`；条件同为 `is_float::<T>()`；`transpose.cml:233` |
| 加第二条后的计划 | `feat/compile/transpose_array_product`：2 条全红（模块带错，`vjp` 连坐） |
| 撤掉第二条后 | 同上 2 条全绿；`feat/compile/rulesets` 67/67、`feat/transforms/split` 19/19、`feat/parse` 115/115、`feat/compile/transforms` 80 = 77/2/1（两条红与 develop 同名） |
| `sum(w * w)`（`w: float[..]`） | 通：value `14.0`，grad `2.0 4.0 6.0`；走的是 `rhs=sum_t,add_tensor_t,mul_tensor_t` |
| 落库的两条律 | `mul_tensor_t`（`052fafe11`）、`add_tensor_t`（`fd964b750`，转置要它才走得到乘积律） |