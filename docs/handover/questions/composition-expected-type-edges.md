# 组合 `..` 的期望类型分发：三处边角要裁定

Author: 车道 `handover/h428-comp-expected`（`D:\Projects\Camel-wt\h428-comp-expected`），2026-10-24。Status: open — 需要作者对下面三处各给一句裁定（裁剪裁定的出处是 `docs/handover/reviews/questions-2026-10-23.md`，`## H248 切片 A 两份` → `### generic-operands-of-composition`）。读数在 Linux 主机（`88zjwei`）上、`origin/develop` 基线（`69f183330`，`h426-interval` 的干净构建）与本车道改动**同一窗口、同一命令形式**（`camel <文件>`，`EXIT=` 是进程退出码）取得。

## 数字先（裁定那两条已落地）

- `let h: (x: int) => int = (x) => ident(ident(x))` —— 改前改后都过（打印 `4`，`EXIT=0`）。裁定说的前提成立。**注意 lambda 的形参表要写括号**：裁定里写的 `x => ident(ident(x))` 在今天的前端是 `error[SyntaxDiag::UnexpectedToken]: Unexpected '=>'; expected '}'.`，能过的写法是 `(x) => ident(ident(x))`。
- 同一位置的组合：改前 `error[SemanticDiag::CompOperandMismatch]: '..' takes two functions or two integers; the left operand is 'a generic function with no type arguments' and the right one 'a generic function with no type arguments'.`（`comp_expected_type.cml:14:30`，`EXIT=1`）；改后打印 `4`，`EXIT=0`。
- 没有期望类型：`let composed = ident..ident` 改前 `CompOperandMismatch`（`0x030001E1`）且操作数印成 `'void'`（`14:20`）；改后 `error[SemanticDiag::CompOperandGenericWithoutType]: '..' composes two functions, and its left operand, the generic function 'ident', has no instance here: write its type arguments ('ident::<int>'), or give the whole expression an expected function type.`（`0x030001E4`，`14:20`，`EXIT=1`），`'void'` 不再出现。
- 返回位置同样分发：`func make(): (x: int) => int { return (x) => ident(ident(x)) }` 改前改后都过；把 `return` 的值换成 `ident..ident` 改前 `CompOperandMismatch ... 'void' and 'void'`（`8:12`，`EXIT=1`），改后打印 `4`，`EXIT=0`。

## 我落地的那半

`Builder::buildComposition` 现在收下期望类型（`visitLinkNode` 原来在进入 `..` 时把期望类型清空，这就是它一直对不上 lambda 的原因），只在**期望类型是一元、没有 `with` 形参的函数类型**时分发：左操作数拿 `(期望的参数类型) => 没有结果类型`，右操作数拿 `(左操作数的结果类型) => 期望的结果类型`。左操作数的结果类型**故意不从期望里取**：`f` 返回什么由 `f` 自己说，`g` 收的就是那个——lambda 里 `f(x)` 的类型来自调用，不来自声明。链 `f..g..h` 据此逐环实例化（`comp_chain.cml` 的三个操作数各取不同实例，写开的 lambda 给同样的 `[4]`）。哪个操作数是"还没定下类型的泛型名"就点哪个的名字。

## 要裁定的一：期望类型不是一元函数时，组合报什么

- lambda 的同位置：`let h: (a: int, b: int) => int = (x) => ident(ident(x))` → `error[SemanticDiag::LambdaParameterWithoutType]: ... the expected type '(a: int, b: int) => int' takes 2 parameters and the lambda 1.`（`lam_binary_expected.cml:8:39`，`EXIT=1`）。
- 组合的同位置：`let h: (a: int, b: int) => int = ident..ident` → 我报 `CompOperandGenericWithoutType`（`comp_binary_expected.cml:8:38`，`EXIT=1`），消息却以 "give the whole expression an expected function type" 收尾——而用户**已经给了**一个函数类型。
- 我的取法：组合按定义是一元（设计原则 4.2），期望类型不是一元就不是"这个组合能有的类型"，我不据此给操作数定实例（否则等于替程序造一个它不该有的实例），于是落到"泛型名在这里没有实例"。要不要给这处一条自己的诊断（例如"`..` 的组合是一元函数，这个位置期望 N 个参数"）？我没有发明。

## 要裁定的二：两个操作数都是泛型、又没有期望类型时点谁的名

`let composed = ident::<int>..ident` 点**右**操作数（`comp_operand_generic_mixed.cml:13:20`）；`let composed = ident..ident` 点**左**操作数（`14:20`）。裁定要求消息里有"哪个操作数是泛型函数"；两个都是泛型时我只报左（第一个）。要不要一次报两个，或在 note 里把两个都写上？

## 要裁定的三：结果里才有的类型变量，要不要用期望替操作数定下来（我按"不替它猜"落地，与 lambda 一致）

`func fill::<T>(n: int): T[] { return [] }`（`T` 只在结果里）作左操作数：

- lambda 写法 `let h: (x: int) => int[] = (x) => ident(fill(x))` → `error[SemanticDiag::GenericArgumentsMismatch]: The generic function 'fill' cannot be called with argument types (int): the type variable(s) T cannot be determined from the arguments or expected result ...`（`free_exit_lam.cml:12:49`，`EXIT=1`）。
- 组合写法 `let h: (x: int) => int[] = fill..ident` → `error[SemanticDiag::GenericFunctionAsValue]: ... its type variable(s) T cannot be determined from the function expected here, '($comp_arg: int) => <null>'.`（`free_exit_comp.cml:12:32`，`EXIT=1`）。
- 两边都不收，所以这符合"与 lambda 等价"：我**没有**让期望的**结果**类型回到左操作数上去定它的结果变量——那样组合会收下 lambda 不收的程序。差别只在措辞与拼法：lambda 是在**调用点**上说的（说实参类型 `(int)`），组合说的是我给它的期望类型，里面 `$comp_arg` 是组合自身的形参名（改前就用在组合自己的类型上）、`<null>` 是"没有结果类型"的打印拼法。要不要给组合位置一句自己的话？

## 我没有做

- `range(a, b)` 的删除/合流（同一节第 3 条）不在本片。
- 没动 `docs/handover/README.cn.md`、`docs/handover/reviews/**`、`docs/design-principles*.md`、`docs/design/**`、`docs/language/**`。
- 三处边角我都按"与 lambda 等价"取了最小的一种，没有新增除 `CompOperandGenericWithoutType` 以外的语义。