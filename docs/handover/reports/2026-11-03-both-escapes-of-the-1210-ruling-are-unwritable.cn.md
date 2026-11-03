# 12:10 的两条出路今天都不可写，卡点是单态规则不注册模板

作者 2026-11-03 12:10（`956255585`）定：同一个键下两条规则**按参数类型判重**，`when` 文本不参与；
并给出两条出路——"按参数类型区分"或"合成一条带分支的规则"。本页记的是**这两条出路在今天的语言里都
不可写**的四组读数，以及按默认补上的那一处引擎能力（单态规则的模板注册）。分支 `lane-dedupe`，
改动 `b5f418a2d`（能力与律）、`21b5dab0c`（本页）、`b3086f1f2`（合入 `origin/develop`），
以及第六节的收口（`chooseRuleAmong`）。

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

## 六、三处候选循环收成一处

作者 12:10 那句"挑选规则是 `jvp` 和 `transpose` 共用的事：从候选集合里挑能绑上操作数的那一条，
写成一个函数，两处都调它"，落成了 `rule_expand.cpp#chooseRuleAmong`。

三份实现（`jvp.cpp#buildRuleTangent` 的循环、`transpose.cpp#chooseTransposedRule`、
`vmap.cpp#chooseRuleOfUri`）逐字比对后，**判据完全相同**，不同的只有两处：

1. **拒绝句的词**：`jvp` 写 "the rule takes …" / "two laws both accept these **arguments**"，
   `transpose`/`vmap` 写 "the **law** takes …" / "… these **operands**"。收成一个
   `RuleChoiceWords{subject, operands}` 参数，句子一个字不变。
2. **读端口用的实例元组**：`jvp`/`vmap` 用操作数的**元素**类型（`ruleElementTypeOf`），`transpose`
   用 `{Float64()}`（`expandRuleByName` 的缺陷，见 `docs/handover/reviews/questions-2026-10-29.md`，
   不是这一笔的）。收成 `instanceTypes` 参数。
3. 另有一处**下界**差别：`jvp`/`transpose` 要求 `ports.size() >= dataOperandCount`，`vmap` 不要求
   （它的操作数列表带 `<>` 配置）。`dataOperandCount == 0` 表示不设下界，所以它也是参数而不是分支。

签名：

```cpp
RuleChoice chooseRuleAmong(
    camel::core::context::Context &context,
    const std::vector<camel::parse::ast::RuleMatch> &candidates,
    const std::vector<camel::core::type::Type *> &instanceTypes,
    const std::vector<camel::core::type::Type *> &operandTypes, size_t dataOperandCount,
    const RuleChoiceWords &words = {});
```

判据（`git grep -n chooseRuleAmong modules`）：

- **一个定义**：`modules/transforms/rule_expand.cpp:74`
- **一个声明**：`modules/transforms/rule_expand.h:84`
- **三处调用**：`jvp.cpp:136`、`transpose.cpp:121`、`vmap.cpp:150`

收掉 355 行、新增 251 行（`git diff --stat`：`jvp.cpp` −198、`transpose.cpp` −115、`vmap.cpp` −85，
`rule_expand.cpp` +154、`rule_expand.h` +54）。`portsTakeOperands`（`jvp.cpp`）、`portsTextOf`
（`transpose.cpp`）、`vmap.cpp` 里那份 `typesText` 都跟着消失，它们的说明并进了
`chooseRuleAmong` 的文档：`<>` 端口不参与 data 端口的下界、`assignableFrom` 的方向、以及
"开放秩不缩成操作数的形状"。

合入 `origin/develop`（`9e72bc95b`）时 `modules/transforms/transpose.cpp` 是真冲突，两边都在同一个
匿名命名空间里加了函数、都改了同一个走查点：`develop` 的 `ruleTakesOperands`（lane-mulsc，
数组×标量的形状拒绝）本来问的是 `transposeRuleOf` 答的那**一条**律；本分支已无"那一条"，
所以合并后它问的是 `chooseTransposedRule` **选出来的那一条**——`develop` 的问题不变，只是换了主语。
顺序也据此定为"先选律，再问形状"（`transpose.cpp` 走查点）。

闸门（同一棵合并后的树，jobs 4，2026-11-03 20:50–21:05）：

| 计划 | 合并后、收口前（`b3086f1f2`） | 收口后 |
| --- | --- | --- |
| `feat/compile` | 2190 = 2185 / 2 / 3 | **2190 = 2185 / 2 / 3** |
| `feat/compile/rulesets` | 67/67 | **67/67** |
| `feat/compile/transforms` | 80 = 77 / 2 / 1 | **80 = 77 / 2 / 1** |
| `feat/transforms/split` | 19/19 | **19/19** |
| `feat/parse` | 115/115 | **115/115** |

`feat/modules` 1929 = 1917 / 5 / 7，五红全是 `registry.*`（`signatures_match`、`result_types_match`、
`parameter_types_match`、`docs_complete`、`migrations_stay_migrated`）；**收口前后同数**——
用 `git stash` 拿掉这一笔重跑 `stdlib_registry` 计划确认过（13 = 8/5 两遍）。那是陈旧签名页那条
lane 的账（`docs/reference/camel-for-ai-stdlib-signatures.md` 在合并里就有改动），不是这一笔。

## 七、仍然留着的一件事

**`match type` 那条路只差"参数个数随类型变"**。若作者认为这值得做，那是一条独立的能力，本页只记
读数（第四节第三条）。