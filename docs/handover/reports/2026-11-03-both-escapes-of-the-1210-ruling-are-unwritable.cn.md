# 12:10 的两条出路今天都不可写，卡点是单态规则不注册模板

作者 2026-11-03 12:10（`956255585`）定：同一个键下两条规则**按参数类型判重**，`when` 文本不参与；
并给出两条出路——"按参数类型区分"或"合成一条带分支的规则"。本页记的是**这两条出路在今天的语言里都
不可写**的四组读数，以及按默认补上的那一处引擎能力（单态规则的模板注册）。分支 `lane-dedupe`，
改动 `b5f418a2d`。

## 一、判重那处

`src/parse/ast/rule_head.cpp#indexRule`：

- 改前：`if (slot.condition == rule.condition && slot.parameterTypes == rule.parameterTypes)`
- 改后：`if (slot.parameterTypes == rule.parameterTypes)`

`RuleSlot` 里 `condition` 还在（读的人还要看），只是不参与判定。改完**四条转换律立刻变成重复**：
`stdlib/rulesets/jvp.cml` 的 `jvp_float_from_float::<S>` 与 `jvp_float_from_a_step::<S>` 都是
`(x: S)`，只差 `when`。所以判重这一改必须和转换律的改写同一笔落地，否则标准库在装载期就报重复。

## 二、四条死路（全部实测，逐条可复现）

| 形态 | 读数 |
| --- | --- |
| 合成一条 + `Tangent<S>` 辅助函数（`(Tangent<S>)` 泛型重载 + `(dx: ())` 具体重载） | `SemanticDiag::Redeclaration: 'float_from_tangent'. Each identifier must be declared only once in its scope.` —— 本语言不允许按参数类型重载同名函数 |
| 删掉 step 律、只留 float 律 | 层 2181/3/3：`transpose_loop_escape.int_is_a_zero_tangent_space` 红，`jvp has no forward rule for :op/ltod` —— step 律不能删 |
| 合成一条 + `match type S` 分支（右值闭包参数写 `Tangent<S>`） | 编译过、`feat/compile/rulesets` 67/67，层 2181/3/3，红因是 `jvp has no forward rule for :op/ltod (the rule's right side does not take the values and tangents the rule binds)` —— **arity 不随类型变**：step 源要 0 参闭包，`(dx: Tangent<S>)` 仍是 1 参 |
| 按参数类型分流（`(x: int)` / `(x: i32)` / `(x: string)` 三条具体律） | `the rule is named in the index but its templates are not registered`（`modules/transforms/jvp.cpp:242`）—— **单态规则根本不进模板表** |

第三条值得单独说：`match type` 是**能做类型分支**的（`stdlib/types.cml:143` 的 `is_same` 就这么写），
所以"带分支的一条"本来是最像作者意图的形态；它失败的地方不是分支，而是**闭包的参数个数**——一个
`Tangent<S>` 参数在 `S = int` 时是 `()`，但引擎按声明给的参数个数调用，`()` 不会被展平成 0 个。
要让这条成立，需要"参数个数随类型变"，那不是一处补丁。

## 三、按参数类型分流为什么曾经不可写，以及补了什么

第四条是把作者第一句话（按参数类型区分）做出来：`is_integer::<T>()` 恰好是 `int | i32`
（`stdlib/types.cml:153`），`is_string` 是 `string`，所以每个目的地一条泛型 float 律 + 三条具体
step 律，四条参数类型互不相同，判重自然通过。

它卡在**另有其事**：模板表只由 `__rule_generic` 填（`src/compile/gir/builder_operators.cpp`），也就是
"头部声明了类型参数的规则"；单态规则走 `__rule`，两个边是普通函数值，有值，因此从来没有进过模板表。
于是索引里点得出名字、表里没有——转换器那句 `the rule is named in the index but its templates are not
registered` 说的就是这件事。

补的是这一处，**只补注册**（不碰泛型实例化路径，不碰挑律逻辑）：

- `src/compile/gir/rule_templates.cpp#registerMonomorphicRule`：注册一个 `typeParams` 为空、且**注册时就
  有一个实例**的条目；两个边是声明已经建好的两个图（`compile_graph_ptr_t`），不是模板，所以没有元组
  要等、没有 `with` 值要绑，`genericRuleInstance` 对任何元组都答这一个实例。
- `src/compile/gir/builder_generics.cpp#Builder::settleRuleMaker` + `builder_operators.cpp` 的
  `__rule` 分支：拿两边函数值的图（`Builder::functionValueGraphOf`）注册；规则值本身**不动**（id 仍是 0，
  仍是那两个函数值），改写引擎读到的没有任何变化，只有模板表多一条。
- 形状不满足就返回：边不是本程序的函数值（泛型规则的边是模板，由它自己的 maker 注册）、或带了 `with`
  值（那是泛型 maker 的事）。

## 四、转换律的新形态（`stdlib/rulesets/jvp.cml`）

四个目的地 × （1 条泛型 float 律 + 3 条具名 step 律）＝ 16 条：

```camel
export rule jvp_float_from_float::<S>(x: S) when is_float::<S>() {
    float(x) => (dx: S) => float(dx)
}
export rule jvp_float_from_int(x: int) {
    float(x) => () => 0.0
}
export rule jvp_float_from_i32(x: i32) { float(x) => () => 0.0 }
export rule jvp_float_from_string(x: string) { float(x) => () => 0.0 }
```

`f32`/`f16`/`bf16` 同形。注意**没有**按"源×目的地"铺成笛卡尔积：`f16` 没有从 `bf16` 的转换（实测报
`No candidate of 'f16' applies to (<> (bf16))`），铺成积会直接编译不过。step 那三条不涉及转换重载，
所以没有这个问题。

## 五、闸门

改前与改后同一棵树 `/home/zjwei/camel-work/dedupe`，jobs 4，2026-11-03 20:25–20:45：

| 计划 | 改前（`845875568`） | 改后（`b5f418a2d`） |
| --- | --- | --- |
| `feat/compile` | 2187 = 2182 / 2 / 3 | **2187 = 2182 / 2 / 3** |
| `feat/compile/rulesets` | 67/67 | **67/67** |
| `feat/compile/transforms` | 80 = 77 / 2 / 1 | **80 = 77 / 2 / 1** |
| `feat/transforms/split` | 19/19 | **19/19** |
| `feat/parse` | 115/115 | **115/115** |

`transforms` 那两红仍是 `transpose_loop_escape.two_values_of_two_kinds`、`.second_state_of_the_same_loop`
（别人在做的前向值残差，不是这一笔）。

## 六、留着的两件事

1. **候选循环仍有三份**（`jvp.cpp`、`transpose.cpp`、`vmap.cpp` 各一处，共享的是
   `rulePortsOf`/`narrowerPortsThan`）。作者要求"写成一个函数、两处都调"；收成
   `chooseRuleAmong(context, candidates, instanceTypes, operandTypes, dataOperandCount)` 是下一笔，
   等转换律落地、层回绿之后再动。
2. **`match type` 那条路只差"参数个数随类型变"**。若作者认为这值得做，那是一条独立的能力，本页只记
   读数。