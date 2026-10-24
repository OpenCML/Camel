# 调用点读 body-less 声明的 `when`：一个静默算错数的缺陷

分支 `handover/h399-when-at-call`（`d9ea5a905` 修复、`4b945cf7b` 记录）。前一条车道
（`handover/h393-sorts-per-element`）在改写排序族时发现"交换两条同名声明的书写顺序会换掉被选中的算子"，
本车道把它钉到最小、判定了归属并落地。

## 1. 缺陷比描述更重：不是算子选错看得出来，而是静默算错数

同一程序、两条同名 body-less 声明、`when` 互斥（i32 臂走 `:op/sub_i` 减法，int 臂走 `:op/add_l` 加法），
只把两条声明的书写顺序对调。改前（基线 `cf813dfd0`，主机实测）：

```
i32_first:  i32=-4    int=4294967292      # 两行都编到 :op/sub_i；int 那行把 i32 减法跑在 int 上（回绕，无诊断）
int_first:  i32=42    int=42              # 两行都编到 :op/add_l
i32_first: N02 [label=":op/sub_i" ... value_type="i32"];  N08 [label=":op/sub_i" ... value_type="int"]
int_first: N02 [label=":op/add_l" ... value_type="i32"];  N08 [label=":op/add_l" ... value_type="int"]
```

`4294967292` 是 i32 回绕，不是拒绝：被选中的算子只由书写顺序决定，`when` 一次都没被读，
`value_type="int"` 的节点编到 `:op/sub_i` 也不报错。

## 2. 归属：不需要新的语言裁定

`docs/design-principles.cn.md` 4.4（第 272 行）已经写明：条件不成立 ⇒ "这个重载不适用"，
重载选择去试同名其他候选，**没有一个适用时在调用处报错，列出每个候选没满足的条件**。重载选择就发生在调用点，
所以调用点必须读 `when`——不读，这句里的"不适用"与"报错列出条件"都无处成立。作者对 `impl-declaration-arms`
的裁定（`docs/handover/reviews/questions-2026-10-23.md` 第 288–294 行）又把"规则统一"到所有带 `when` 的同名重载。
按同文件第 384 行（"代码与原则或已有裁定不一致 ⇒ 按原则改代码，不必走问题通道"）直接改。

## 3. 落地（`src/compile/gir/`，核心 API 未动）

- `Builder::ruleVerdictForCall`（`builder_modules.cpp`）：把 `declaredRuleApplies` 的问法拆成三答案
  `Applies / ConditionUnmet / NotThisRule`；`declaredRuleApplies` 改为其中一个答案，`#impl` 采纳路径
  （归约族 `sum`/`min`/`max` 的臂）行为不变。
- `Builder::resolveGroupedCall`（`builder_operators.cpp`）：调用点逐条试候选——"签名收下这组实参 → 再问它自己的
  `when`"；条件为假的不是候选。写出来的调用（`visitLinkNode`）与算子名当值用（`overloadValueFor`）都走它。
- 一条都不适用 → 已有的 `SemanticDiag::WhenConditionUnmet`，候选按"签名 — when 条件"逐条列出。列表**不能按签名
  去重**：两条臂的签名文本相同（`(a: T, b: T) => T`），只有条件不同；第一版按签名去重把两条压成一条，是探针查出来的。

改后（同树同命令）：

```
i32_first:  i32=-4    int=42    exit=0
int_first:  i32=-4    int=42    exit=0      # 两次逐字相同
error[SemanticDiag::WhenConditionUnmet]: No candidate of 'arms_kernel' applies to (<> (float, float)):
every candidate's 'when' condition is false. Candidates: (a: T, b: T) => T — when is_arm_i32::<T>();
(a: T, b: T) => T — when is_arm_int::<T>()
```

## 4. 回归证据与全层数字

- 新增 `test/cases/compile/native/when_order_at_call_i32_first.cml`、`..._int_first.cml` 与计划三条
  （`native.when_order_at_call.i32_first` / `.int_first` 钉逐字 `i32=-4\nint=42`，`.on_the_graph` 钉
  `:op/sub_i`=i32、`:op/add_l`=int，并断言不存在"算子与元素类型不符"的节点）；三条在改前会红。
- 未写 `known_fail`：三条都绿。"两条 `when` 同时成立"的兜底行为**故意不钉**——把未决行为写进用例等于钉成规范。
- 整层（88zjwei，`smoke,test/plans -Tier fast -Jobs 8`）：**total 6020 / pass 6005 / fail 0 / skip 11 /
  known failures 4，`RUN_EXIT=0`**，wall 265.3 s；`native_declarations.plan.toml` 119/119 通过。

## 5. 没做的

- 没动核心 `OperatorGroup::resolve`/`resolveForCall`（核心不读 `when`，筛选留在编译器调用点）、声明期检查
  （`WhenDeclarationOverlap`/`separateWhenConditions`）、`signaturesCanAgree`。
- 没动 h393 已落地的排序族写法与 bf16/f16/bool/string 的拒绝。
- 没裁定"两条 `when` 同时成立时取谁"——已单独成文：
  `docs/handover/questions/two-when-conditions-hold.md`（选项 A/B/C 与代价、建议）。