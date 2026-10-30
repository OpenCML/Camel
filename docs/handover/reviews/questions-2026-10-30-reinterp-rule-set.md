# `reinterp<H>`：规则集 `H` 以什么形式交给算子

第 3 步的验收是"标量函数的 `jvp` 跑通"。先量了一遍：`reinterp` 在整个仓库里 **0 行实现、0 个调用者**（28 处拼写全在 `docs/` 里）。挡住它的不是建图——`modules/autodiff/pullback.cpp#buildReverseClosure` 已经从空 `GraphDraft` 建过图——而是 `H` 没有形式，以及取到原语规则的那一半不在建图能力所在的模块里。这一个是设计稿自己列为"第 3 步开工前定"的两项之一（`docs/design/program-transforms/README.cn.md` 第 7 节）。

## 现象到符号

- **规则是泛型 `rule` 声明**，住在库里：`stdlib/autodiff.cml#add_jvp`、`#mul_jvp`、`#neg_jvp`（`rule mul_jvp::<T>(x: T, w: T) when is_float::<T>()`）。
- **按声明读规则只对"有声明"的函数成立**：`stdlib/graph.cml#rule_of` 与 `modules/autodiff/rules.h#ruleOf` 读的是函数图上的 `#jvp(rule)`。标量算术是内置算子（`:op/mul_l`），**没有声明可挂**。
- **把规则名当标签实参今天被拒**：`diagnostics/registry.tsv#TagArgumentNotValue`，`#jvp(mul_jvp)` 那句写法进不了程序。
- **变换的签名不写在库里**：`stdlib/signatures/autodiff.cml` 自己写明 "these declarations are not compiled"；类型由 `modules/autodiff/operators.cpp#GradResolver` 一类 C++ 解析器算。所以 `reinterp`／`jvp` 的类型也只能在 C++ 里算，`H` 只能从这里进。
- **规则值的表示在另一侧**：`modules/rewrite/rule.h#RuleObject`（`ruleId`、`lhs`、`rhs`、`withValues`）；把某条规则在某个类型元组下的两侧取出来的唯一入口是 `include/camel/compile/gir/rule_templates.h#genericRuleInstance`。
- **两个模块各有一半，中间没有契约**：`modules/rewrite` 读 draft（引入的是 `camel/runtime/draft_session.h`），但**不建任何节点**（全模块没有 `GraphDraftGroup`、`addPortNode`、`addCallNode`、`addRegion`）；`modules/autodiff` 建图，却**不读 `RuleObject`**（`RuleObject` 只出现在 `modules/rewrite`）。
- **类型函数只有三个**：`stdlib/types.cml#ParamsOf`、`#WithOf`、`#ResultOf`；**没有 `Tangent`**，所以"提升后的函数类型"今天写不进库里的声明。

## 为什么不能自己定

1. **`H` 落在哪个模块是模块边界的决定。** 规则是数据（`Rule` 值），建图在 `autodiff`，取规则两侧的入口在编译侧。谁读 `RuleObject`、谁建图，必须并到一处或新增一条公共契约；今天没有任何一处跨模块传 C++ 规则类型，设计原则 10.1／10.4 也只说"声明即契约"，没说规则可以这样过界。
2. **`Lift` 决定 `reinterp` 是不是 `reinterp`。** 没有 `Lift`，"按规则集重新解释"就退化成"前向模式 + 注入的规则"，那是 `jvp`，不是 `reinterp`；这一步叫哪个名字、`Lift` 是类型函数还是变换自己的选择，是设计稿第 4.1、7 节要定的。
3. **`Tangent<T>` 是语言可见的类型函数**，加它等于给 9.9 的切空间起一个用户能写的名字。整数叶子是"自身的切空间"还是"没有切空间"是语义，不是施工方该定的。
4. **最小可用也过不去。** 试过把规则集当普通实参从库传进算子：那要新增一条"库到模块的值通道"（今天库交给模块的只有声明和标签），外加一个跨模块的只读视图——两件都是契约。

## 选项

三个选项各自**要额外造什么**，列在每条里——圈选时看的就是这一栏。

- **A. `H` 是 `Rule[]`，`reinterp` 与规则引擎同住。** 库写 `reinterp(f, forward_rules)`，`jvp` 是库里的包装。**要多造**：① 建图能力与规则读取并到一处（`rewrite` 内或新模块 `transforms`，建图从 `autodiff` 搬过去，纯重构）；② **一条库到模块的值通道**——规则集要以实参出库，而今天库交给模块的只有声明与标签；③ **`Tangent<T>`**——`jvp` 包装与"提升后的函数类型"都要它，否则签名只能在 C++ 解析器里算、`Lift` 写不进库。好处：设计稿 `H` 的字面形状，规则可拼装，不必给原语补声明。
- **B. `H` 不进语言，变换自己取规则。** `reinterp` 留在 `autodiff`，用户面只有 `jvp(f)`。**要多造**：① 一个跨模块的只读契约（`RuleObject` 的 `ruleId`／两侧／`with` 值公开给 `autodiff`）；② 一条库到模块的通道（库在导入时把自己那批规则交给模块一次）；③ `Lift` 的口径由变换自己定并写明——`reinterp` 就此退化成 `jvp`，这一点要在这里认下来。好处：模块边界不动，用户面最小。
- **C. 规则住在原语的声明上。** 给十来个标量算子补声明，并让 `#jvp(rule)` 收规则名。**要多造**：① 标签实参多一种（放开 `TagArgumentNotValue`：解析、绑定、标签事实各一处）；② 十来个标量算子的声明（执行落到既有的"声明 + 内核站点"）；③ `Tangent<T>` 仍缺，"提升后的签名"照旧写不进库。好处：设计稿第 3 节的形状，缺规则在类型层看得见。

**默认建议：A**。理由：`H` 作为值是设计稿写的形状；建图能力搬家不改语义，车道上一步做得完；B 要新造"库到模块的值通道"并把 `Lift` 的退化认下来，是更大的契约；C 动的是编译器的绑定与检查，离"让标量 `jvp` 跑通"最远。

裁定之前第 3 步不动：`reinterp`／`jvp` 仍是 0 行，验收读数无从谈起。