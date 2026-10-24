# `+=` 的规则已落地，4.2 还只有两条特例句子，且“结果类型等于 `a` 的类型”缺一个读数

Author: 车道 `handover/h442-compound-assign`（`D:\Projects\Camel-wt\h442-compound-assign`），2026-10-24。Status: open — 需要作者两件事：(1) 把裁定的那条统一规则写进 4.2（作者页我不改）；(2) 确认“结果类型等于 `a` 的类型”里的“类型”按哪个读数比——今天编译器里同一个张量有两种读数，选不同的会给 `t /= 2.0` 相反的答案。测量在 Linux 主机（`88zjwei`）上、`origin/develop`（`83a259196`）与本车道改动同一构建窗口内完成，读数走 `camel-remote.ps1` 与计划运行器；命令行 `camel <case> std::fvm`，`CAMEL_ERROR_FORMAT=short`。

## 数字先

- 裁定逐字（`docs/handover/reviews/questions-2026-10-23.md:618`，`## 2026-10-24 晚一批（六份）` → `### compound-assignment-on-a-tensored-array`）：“**路线 1，并统一成一条规则**：`a op= b` 就是 `a = a op b`，只要 `a op b` 有定义且结果类型等于 `a` 的类型——不论类型是谁定义的（规则统一）。数值数组有张量模块声明的 `+`，它就有 `+=`；`int[]` 若没有 `+`（或 `+` 的结果类型不同），`+=` 照样是错误。`compoundAssignmentByRebinding` 改为按“运算符是否可用”判断，那段注释随之改写。”
- 已落地（`src/compile/gir/builder_operators.cpp:926`）：按“plain 运算符对这些操作数是否可用 + 它的结果类型是否等于 `a` 的类型”判断，`isOtherType()`（“类型是谁定义的”）不再参与；`A + A → B` 由 `operator '+=' is not defined for these operand types (A, A)` 报出（改之前是 `operator '=' is not defined for these operand types (A, B)`）。
- 4.2 现在只有两条**特例**读得到：`docs/design-principles.cn.md:255`“`a &= b` 即 `a = a & b`”，`:257`“**张量的 `t += u` 是算出新张量再重新绑定**，与数值一致，不原地修改”。裁定把两条统一成一条规则，4.2 里读不到这句话。

## 需要裁定

### 1. 4.2 缺那句统一规则（文档债，作者页我不动）

把 `:255` 里 `&=` 那半句与 `:257` 合成一句（例如“`a op= b` 就是 `a = a op b`，只要 `a op b` 有定义且结果类型等于 `a` 的类型，不论类型是谁定义的”），两条特例句子留还是删由作者定。`docs/language/**` 属作者，同理。

### 2. “结果类型等于 `a` 的类型”要指定一个读数

同一个张量，今天编译器在两处读得不一样（都逐字实测）：

- 声明位置：`var t: Tensor<f32, [2]> = ones([2])` 之后 `let q: Tensor<f32, [2]> = t / 2.0` → `SemanticDiag::DeclaredTypeMismatch`，逐字 “This value has type 'Tensor', which is not the declared type 'Tensor<float32, [2]>'.”——声明处按 `Tensor<float32, [2]>` 读，而 `t / 2.0` 给的是裸 `Tensor`。
- 赋值/运算符位置：同一个 `t`，`t = t / 2.0` 合法（EXIT=0，打印 1.0）。
- 于是 `t /= 2.0` 的答案是相反的：**按声明读数比** → `operator '/=' is not defined for these operand types`（本车道第一版实现就是这样，`test/cases/modules/tensor/compound_assign.cml:33` 因此变红，我把读数对齐后恢复绿）；**按运算符解析看到的读数比** → 合法，与 `t = t / 2.0` 一致。
- 我取了后者：裁定第一句是 `a op= b` 就是 `a = a op b`，`t = t / 2.0` 合法，`t /= 2.0` 就该合法；实现里目标类型取的是调用点交给运算符解析的那一份类型（`normTypes[0]`）。
- 需要作者确认：规则里的“结果类型等于 `a` 的类型”是指“运算符解析看到的 `a` 的类型”（我的实现），还是“`a` 声明处写的类型”（那 `t /= 2.0` 与 `compound_assign.cml:33` 的期望要改成错误）。`A + A → B` 这种既不相等也不可赋值的，两种读数都报错，不区分。
- 这个分歧来自过渡类型：`Tensor` 与 `Tensor<f32, [2]>` 今天仍是两个类型（P4 未落地，`test/cases/compile/array_alias/array_alias.plan.toml` 8 条 XFAIL），P4 落地后可能自行消失。若作者认为不必现在定，请把这条并进 P4 的验收，而不是让我在两种读数里挑一个当语义。

## 余留（不是裁定项，只是实现的边界，供作者知情）

判断“plain 运算符是否可用”时，程序自己写的 `operator` 只按**组**看（`hasProgramOperatorOverloads`：这个组在程序/导入里有没有重载），不按签名逐个比；签名匹配留给真正的 `a op b` 解析去做（它在结果类型那一步被用到）。副作用只有一个：**同一个模块里存在另一个类型上的 `+` 时，`int[]` 的 `+=` 报的是 `+` 而不是 `+=`**（仍是编译期错误）。逐字实测：模块里声明 `operator +(a: P, b: P): P` 并有 `var xs = [1, 2]; xs += ys` → `SemanticDiag::NoMatchingFunction`，`(int[], int[])`，“operator '+' is not defined for these operand types”，`:14:5`。要让它报 `+=`，就得在判断阶段按签名逐个比（或把 `a op b` 的解析放进丢弃式诊断域里探一次），两条路都要动到运算符解析的主路径，本车道没有做——裁定要求的“是错误”已经满足，缺的是名字。批注：不这么宽松的话，`acc += { x: 1.0, y: 1.0 }`（复合字面量按 5.1 进入参数类型）会从今天能编译变成拒绝，所以宽松是**故意**的。

## 我没有做

- 没动 `docs/design-principles.{cn,en}.md`、`docs/language/**`、`docs/design/**`、`docs/handover/reviews/**`。
- 没实现 `sync-chain-liveness-and-splice`（`docs/handover/reviews/questions-2026-10-23.md:600`，属另一车道）。
- 没给“数值数组”的 `f32[3]` 拼法写用例：`var a: f32[3] = ones([3])` 今天因 P4 未落地报 `DeclaredTypeMismatch`，新用例按今天能跑的 `Tensor<f32, [3]>` 写（`test/cases/compile/operators/tensor_add_assign.cml`）；`f32[3]` 那一半留给 P4 落地后由 `array_alias` 一族承担。
- 没给 `let` 上的 `+=` 新增用例：`test/plans/feat/linear/var_reference.plan.toml` 的 `let_compound_assigned`（`test/cases/linear/byref/let_compound_assigned.cml`）已锁 `ImmutablePassedToVarParam`（内置类型走 `<__assn_add__>`），我只是实测模块类型走 `<__assn__>`（`'t' is immutable and cannot be passed to a var parameter of '<__assn__>'`），未改动该行为。