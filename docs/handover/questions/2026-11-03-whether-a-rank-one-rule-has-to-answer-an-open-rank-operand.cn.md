# `T[]` 形参的规则要不要也覆盖 `T[..]` 的实参？

`T[..]` 覆盖 `float[]` 这件事，HEAD 上已经成立（读数在下面）；今天真正还挡着用例的是反方向：
形参写成 `T[]` 的规则不接 `T[..]` 的实参。这一页问的就是这一条要不要改。

## 一、四种拼写各是什么（文档原文 + 源码）

`docs/design-principles.cn.md` 5.4：

- L381：`原生的多维数组是核心类型：T[维度...]，元素类型加形状，值语义。int[]（一维、长度运行时才知道、可以追加）就是 int[?]；f32[3]、f32[n, 784]、u8[h, w, 3]；int[][] 仍是数组的数组`
- L382：`? 是不需要名字的任意大小（编译期未知，调用处做运行时检查，与 as 相同）；..s 是任意维数。`
- L387：`秩 0 就是元素本身：T[..] 包括秩 0 … t: f32[..] 的 t[0] 是 f32[..]；要元素写 t[0] as f32 … for 遍历 f32[..] 每轮交出 f32[..]，遍历 f32[]（秩已知为 1）每轮交出 f32`
- L468：`声明里未定的 extent 不参与按类型特化：形参写 int[]、T[?]、T[..]，实例的形参类型就是声明的类型代入类型变量之后的结果`

| 拼写 | 类型 | 秩 | 长度 | 源码 |
|---|---|---|---|---|
| `float[]` | `ArrayType::create(f32)`，一个 `Unknown` extent，`anyRank_=false` | 已知 1 | 未知 | `src/compile/gct/builder.cpp:6900`；`array.h:119` |
| `float[..]` | `ArrayType::openRank(f32)`，`anyRank_=true`，`extents_` 空 | 未知（含秩 0） | — | `builder.cpp:6902`；`array.cpp:290`；`array.h:123` |
| `float[3]` | `create(f32, {3})`，一个常量 extent | 已知 1 | 3 | `builder.cpp:6900` |
| `float[.., ..]` | **不是合法拼写**：`error[SemanticDiag::DimensionAfterAnyRank]` | — | — | `builder.cpp:6878-6883` |

前三个同一族（`ArrayType`），是三个不同的成员：`array_type_selftest.cpp:125` 断言 `int[]` 不等于 `int[..]`；
`:103` 断言 `openRank(f32)` 打印成 `f32[..]`。`float[.., ..]` 的报错原文：
`A dimension pack ('..') must be the last item of its '[...]', and only one may be written.`（合法写法是 `f32[3, ..rest]`）。

## 二、匹配发生在哪一层

`modules/transforms/jvp.cpp:131-186 portsTakeOperands`，判据是第 177-178 行：

```cpp
if (wanted[i] == nullptr || portTypes[i] == nullptr ||
    !wanted[i]->assignableFrom(portTypes[i])) {
```

即"规则的形参类型能否接住操作数的类型"，用的是 `src/core/type/composite/array.cpp:616 ArrayType::assignableFrom`
（`:644-666` 是 `anyRank_` 的两条分支）。`assignableFrom` 自己就是判据，没有第二张表。

## 三、缺陷还是设计

**是缺陷，而且已经修掉了。** 今天 HEAD 上复现不出报告里的那句读数。修它的不是"匹配器少一条任意维覆盖一维"，
而是把关系从**相等**换成**可接住**——`5fcd502bc`（2026-11-03，HEAD `e3453a60a` 的祖先）：

```
-        if (wanted[i] == nullptr || portTypes[i] == nullptr || !wanted[i]->equals(portTypes[i])) {
+        if (wanted[i] == nullptr || portTypes[i] == nullptr ||
+            !wanted[i]->assignableFrom(portTypes[i])) {
```

`equals` 下 `float[..]` 与 `float[]` 不相等，于是恰好打印报告里那句 `the rule takes (float[..], float[..]), and the operator has (float[], float[])`。
`T[..]` 覆盖 `float[]` 从来就是类型系统里定好的（`array.cpp:650-661`，`array_type_selftest.cpp:139-140` 断言 `f32[..]` 接 `f32[3,4]` 与 `f32[4]`），
缺的只是规则匹配器没问这个关系。

## 四、两个交叉读数（实测，`e3453a60a`，载荷 24.9）

① `T[..]` 模式对四种实参（`energy(w: ...) = sum(w * w)`，`jvp(energy)([1,2,3],[1,0,0])`，手算 value 14 / tangent 2）：

| 实参 | 结果 | 读数 |
|---|---|---|
| `float[]` | **过** | `RANK1 a_float_array value=14.0 tangent=2.0`；`source=ruleset rules=jvp_mul_tensor,jvp_sum` |
| `float[..]` | **过** | `RANK1 b_open value=14.0 tangent=2.0`；同样的 rules |
| `float[3]` | **过** | `RANK1 c_float3 value=14.0 tangent=2.0`；同样的 rules |
| `float[.., ..]` | 不适用 | `DimensionAfterAnyRank`，这个拼写不合法 |

旁证：`float[..]` 形参接 `float[]` 实参（与规则无关，只有调用与 `let`）也过：`RANK1 e_callsite value=6.0`。

② `stdlib/rulesets/*.cml` 的 65 条 `rule` 声明里：**32 条**形参含 `[..]`（jvp 8、transpose 3、vmap 21，另有 probe_* 0 条），
**只有 3 条**用秩一的 `[]` 接被求导的操作数——`jvp.cml:309 jvp_idx_arr::<T>(xs: T[], i: int)`、
`transpose.cml:138 idx_arr_t`、`vmap.cml:237 vmap_rule_idx`，全是 `:op/idx_arr`。
`[..]` 是绝对主流，所以"`[..]` 不覆盖一维"若成立，代价是半数规则对最常见形状失效——但它不成立。

## 五、今天仍然挡人的那条（本页的问句）

**反方向不通，而且是有意的**：`T[]` 的规则不接 `T[..]` 的实参。实测（`rank1_probe_f`）：

```
REINTERP jvp function=first skip=rule uri=:op/idx_arr
    reason=the rule takes (float[], int), and the operator has (float[..], int) (of which 2 is a data operand)
```

这与 5.4 一致（L387：`t: f32[..]` 的 `t[0]` 是 `f32[..]`，不是 `f32`），所以一个秩一的律**无法**回答开秩的下标——
它的结果类型就不对。`test/cases/compile/transforms/jvp_matmul.cml` 的头注把这道墙记成"`jvp_idx_arr` 是给 `T[]` 写的"，
被挡住的正是 `matmul(A, x)[0]`。

**问句：开秩数组的下标要不要一条自己的律（`jvp_idx_open::<T>(x: T[..], i: int) => T[..]`），还是让调用方先 `as` 成秩一再求导？**

- 路 A（默认建议）：不动匹配器；在 `rulesets/jvp.cml`、`transpose.cml`、`vmap.cml` 各加一条开秩下标的律，结果类型 `T[..]`。
  代价：三条律 + 一个用例；`jvp_matmul` 的 `A·dx` 那半才能读到，且 `T[..]` 的切向量空间要按 5.4 处理。
- 路 B：把 `T[]` 的匹配放宽成也接 `T[..]`。代价：结果类型会答成 `T`，与 L387 冲突，是把错的东西放行——不建议。
- 路 C：不改，让用例继续 red/known_fail。代价：开秩数组的下标永远不能求导，`jvp_matmul`、`rules_angle_args` 之后所有经过下标的图都停在这里。

未做的一件小事：`array_type_selftest.cpp` 有 `!open3->assignableFrom(anyF32)`（`:144`），却没有
`openF32->assignableFrom(anyF32)` 这一条——正是报告里那一对的另一半；这一行没有断言，所以它只能靠本次实测钉住。
若走默认建议，顺手补这一行（一行，不改行为）。

## 默认建议（按作者裁定 486c203e1：一条 rank-one 的律保持 rank-one）

按此裁定执行——rank-one 的律只答 rank-one 的操作数，遇到开放秩操作数就拒答，并点名位置；不为了答开放秩而把律放宽成 rank-N。作者的裁定原文：「不接，照路 A。`T[..]` 的值可能是 2 秩，`T[]` 的形参不能接；可赋值性只有 `assignableFrom` 这一条规则，不为规则匹配另开口子。要对任意秩成立的律，就在 `rulesets` 里写 `T[..]` 的那一条。页里说的那一行顺手补上。」