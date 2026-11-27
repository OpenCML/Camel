# 交接：`fillslots.permuted_order_runs` —— 硬失败用错了前提：模板参数不是"参数不吃实参"

`lane/editview-commit @ 1007403717` 上 `test/plans/probe/fill_slots.plan.toml` 只有一条，`fillslots.permuted_order_runs` 在本枝 FAIL、在干净尖端 PASS。这页是二分与诊断的全部证据；**修复不在本枝做**（转给另格），本枝留证据。

## 一、结论

`2609eeef38` 新加的硬失败（"实参不吃调用自身类型实参留给它的参数 ⇒ 没有实例"）用了一个**在模板算子上不成立的前提**：它假定"类型实参已经把形参的类型变量换成具体类型"。`fill` 是模板算子，在这一步 `normTypeAt(i)` 取到的仍是实例化前的模板形参/内部绑定，于是**合法实参被判成"不吃"**，硬失败开火。

## 二、二分表（`test/plans/probe/fill_slots.plan.toml`，单条 `fillslots.permuted_order_runs`，每点一次编 + 一次跑）

```
1007403717  graph: the second commit entry takes the draft …            FAIL
48a9a8e507  editview: the handover card …                               FAIL
076c7d556b  graph: the draft type is registered after …                 FAIL
ace8efd9a0  gir: the operator group is asked of the module …            FAIL
cc44dc6d7f  docs: the module header says what a draft is …              FAIL
5e2aa83727  docs: the graph module writes the draft type …              FAIL
2609eeef38  builder_call: an instantiated call reports the argument…    FAIL  ← 分界就在这里
4081130ea8  graph: a handle type with a type argument is compared…      PASS
e83a20dc29  graph: a draft carries the type of the function …           PASS
6e545dd77f  graph: a commit hands back the function type …              PASS
origin/integration                                                      PASS
```

⇒ 句柄同型判定（`4081130ea8`）与草稿类型（`e83a20dc29`）**都不是**肇事笔；分界在 `2609eeef38`。`48a9a8e507` 已红 ⇒ 与 `1007403717`（`commit_of` 形参改 `Draft<F>`）**无关**。

## 三、肇事笔

```
2609eeef38  builder_call: an instantiated call reports the argument its type arguments do not fit
numstat: 16  0  src/compile/src/gir/builder_call.cpp
```

提交正文摘录：

> An operator call whose explicit type arguments are written (`commit::<F>(d)`) replaces the parameters' type variables and then checks every argument against the parameter it stands at (builder_call.cpp, the `explicitTypes` block). When that check failed, the failure was skipped: nothing was reported, and the call went through with a value of a type its parameters never accepted. A draft of `(x: float) => float` given to `commit::<(x: int) => int>` was accepted that way … The block now reports that call the way an ordinary call reports an argument that fits no parameter (`SemanticDiag::NoMatchingFunction`) …

失败原文（本枝）：

```
test/cases/probe/fill_slots.cml:4:37: [Error]: No matching function found for argument types:
  int, int, (int, int), int, int.
    fill: <__w0: void> (template: (int, int), __n1: F) => (int, int)
    (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

## 四、回归基线（改完必须仍 PASS）

```
test/plans/probe/editview_draft_type.plan.toml
  editview.draft_of_one_type_is_not_another  expect_exit 1 + ["No matching function"]
  editview.a_graph_is_not_a_draft            expect_exit 1 + ["No matching function"]
  editview.a_draft_commits_as_its_own_type   expect_exit 0
现在枝上：total 3, pass 3, fail 0  ⇒ 本笔的改进现在成立，修复不许把它废掉。
```

## 五、落点

`src/compile/src/gir/builder_call.cpp`，`explicitTypes` 块（**只在写了显式类型实参时进入**）：

```
2253: if (!resolutionInputs.explicitTypes.empty()) {
2254:   for (size_t i = 0; i < normInputs.size(); ++i) {
2255:     if (i >= targetOperator->funcType()->normTypesCount()) break;
2258:     Type *parameter = targetOperator->funcType()->normTypeAt(i);     ← 关键
2259-2268:  type->equals(parameter) → continue；checkValue(...) → continue
2270-2284:  否则报 NoMatchingFunction + throw BuildAbortException();     ← 新加的硬失败
```

`parameter` 取自实例化前的 `funcType()->normTypeAt(i)`；`fill` 的签名打印出 `__w0: void` / `__n1: F`，即**模板形参与内部绑定**。前提（"类型实参已把形参换成具体类型"）在模板算子上不成立。

## 六、两条修法与缺的证据

- **(a) 算子是模板 / `parameter` 不是具体类型 ⇒ 跳过这条硬失败**（倾向前者）。理由：这一步的前提不成立，模板算子上不存在"可判的错"，硬判必然误伤合法实参。
- **(b) 先实例化再比**：能多判出"模板算子上实参真的不吃实例化后的参数"，成本是此处要触发实例化，可能在 `commit::<F>` 路径上递归。
- **缺的那行证据**：表达"类型仍是模板形参/未实例化"的谓词没找到（`isTypeVar` / `TypeKind::Variable` / `isGeneric` 在 `src/compile` 与 `src/kernel/include/camel/core` 检索为空）⇒ 下一格先确认 `Type` / `FunctionType`（模板段）或 `OperatorTable`（该算子是否模板）上有没有现成成员，再落 (a)。

## 七、四条判据

1. `test/plans/probe/fill_slots.plan.toml` 单条 → `[PASS] fillslots.permuted_order_runs`（贴原文）；
2. `editview_draft_type` 仍 `3/3/0`（第四节那条基线仍报 `No matching function`）；
3. `editview 20/19/1`、`editview_b 3/1/2`、`editview_draft_type 3/3/0`；
4. `test/plans/probe -Tier fast` 只剩 `editview.sec2_log`（本枝现为 `68 total / 66 pass / 2 fail`，修后应为 `67 pass`）。

## 八、禁

- **不改** `targetOperator->funcType()->toString()`：`__w0` / `__n1` 出现在 overload 转述里是正常的，要改的是判定不是转述；
- **不废掉** `2609eeef38` 的改进：它让"一个函数类型的草稿被当成另一个提交"被拒，这是必须保持的行为；
- 不在本枝继续改（本枝只留证据）。

## 九、已修（`ff94ce915f`）

`builder_call.cpp` 的 `explicitTypes` 块在硬失败之前问一句 `containsTypeVar(targetOperator->funcType())`
（谓词在 `core/type/typevar.h:118`，本文件 `:309/317/389/641/645` 早在用），签名还写着类型变量就 `continue`：
模板算子的形参与实参在这一步本来不同型，这里没有可判的错。`numstat 10 0`。

四条判据全绿（同一次构建 `BUILD_EXIT=0`、`binary built 2026-11-28T06:23:10`）：
`[PASS] fillslots.permuted_order_runs`；`editview_draft_type` 3/3/0（`No matching function` 仍报，`2609eeef38`
的改进保住）；`probe.editview` 20/19/1、`probe.editview_b` 3/1/2；`test/plans/probe -Tier fast` 67/68，
唯一红 `editview.sec2_log`。

副作用面（`feat/compile` + `feat/modules`，`-Tier fast`，同一构建）：
`[SUMMARY] total 4818, pass 4726, fail 81`；**与枝底 `1007403717` 的 81 条逐名完全相同（NEW 0 / GONE 0）**。
其中 39 条不在 `promotion-66` 的 147 行红表里 —— 它们在枝底就已经红，红表对这几组计划（`graph*`、`json_value`、
`stdlib_registry`、`rules_conditions`）已过期，不是本笔造成的。