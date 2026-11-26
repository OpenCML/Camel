# 七条"诊断名/阶段变了"的红：5 条记录过期、2 条真缺陷

本页回答 task-125：531 条红里被归成"诊断名或阶段变了"的 7 条，逐条判"真缺陷 / 记录过期"，
并给出依据（设计页原句或源码位置）。**本卡不动 `src/`、不改任何期望值、不改用例。**

## 量具与来源

| 项 | 值 |
|---|---|
| 证据面 | `D:\Temp\t122-name-mismatch.tsv`、`.md`（task-122 的抽样面） |
| 逐条原始输出 | `D:\Temp\t122\fail\<plan>\<test>\{stderr.txt,stdout.txt,meta.json}`（2026-11-27 那次运行在 `065bfb1690` 上的落盘） |
| 记录的 tip | `065bfb1690`，checkout `tree23`，`tier` fast，`jobs` 16 |
| 本裁决所在的树 | `lane/mm-capacity`，HEAD `09c93ae622`；`git rev-list --left-right --count 065bfb1690...09c93ae622` = `0 2`（只多我自己那两笔测试提交），被引的源码/plan/用例在两点之间**零改动** ⇒ 下面引的行号与机制对两棵树都成立 |
| 新测量 | **没有**。七条的判定靠已落盘的 `stderr.txt` + 源码 + 设计页，不开新的整层（宿主 `[load]` 68–88） |

一句话结论：**5 条只是行号过期（消息一字不差，设计说就该在运行期报）；`dropped_foreach_lambda` 是真缺陷（检查在库里一次转发之后丢了）；`int_mat_assign` 是真缺陷（程序被正确拒绝，但诊断走错路，泄露内部名 `__assn_mat__`）。**

## 先说一处证据面的错

证据面把五条的"钉住的名字"记成 `InvalidAccessIndex`、`UnusedPureResult`、`DimensionNameNotBound` 一类，于是它们看起来像"名字变了"。
逐个核对 plan 条目：这五条**根本没有 `expect_diagnostic_name`**（`expect_diagnostic_name` 计数为 0），钉的是**消息文本 + 位置**：

| plan 文件:行 | 条目 | `expect_diagnostic_name` |
|---|---|---|
| `test/plans/feat/modules/tensor/bounds.plan.toml:78` | `slice_runtime` | 无 |
| `test/plans/feat/modules/tensor/named_dims.plan.toml:73` | `return_extent_checked` | 无 |
| `test/plans/feat/modules/tensor/named_dims.plan.toml:117` | `unknown_extent_mismatch` | 无 |
| `test/plans/feat/modules/tensor/shape_inference.plan.toml:44` | `si_computed_extent_run_time` | 无 |
| `test/plans/feat/modules/tensor/shape_types.plan.toml:146` | `as_runtime_mismatch` | 无 |

那五条消息在现 `stderr.txt` 里**逐字都在**，红只是红在位置断言上。所以这五条不是"名字/阶段变了"，是"位置过期"。

## 逐条裁决

| # | plan / test | 钉住的（名字/消息 + 位置） | 实际（`stderr.txt`） | 裁决 |
|---|---|---|---|---|
| 1 | `compile.operators` / `int_mat_assign` | `SemanticDiag::NoMatchingFunction`；"operator '@=' is not defined for these operand types"；`8:5` | `SemanticDiag::ImportNameNotExported`；"Reference `__assn_mat__` is declared as imported but is not exported by its module"；`8:7` | **真缺陷**（拒绝正确，诊断错路） |
| 2 | `consistency.new_values` / `new_values.dropped_foreach_lambda` | `SemanticDiag::UnusedPureResult`，`10:9` 与 `12:33`，`expect_exit = 1` | 无诊断，`exit 0`，stderr 空 | **真缺陷**（检查在库包装处丢失） |
| 3 | `modules.tensor.bounds` / `slice_runtime` | "The range 8 to 9 is out of range for an array of length 3"；`slice_runtime.cml:6:` | 同一句；`7:19` | **记录过期**（+1 行） |
| 4 | `modules.tensor.named_dims` / `return_extent_checked` | "conversion to 'f32[3]' failed: the array has shape [4]"；`err_return_extent.cml:8:16` | 同一句；`9:16` | **记录过期**（+1 行） |
| 5 | `modules.tensor.named_dims` / `unknown_extent_mismatch` | 同一句；`unknown_extent_mismatch.cml:9:17` | 同一句；`10:17` | **记录过期**（+1 行） |
| 6 | `modules.tensor.shape_inference` / `si_computed_extent_run_time` | "reshape changes tensor element count: [6] -> [3, 3]"；`computed_extent_run_time.cml:7:25` | 同一句；`8:25` | **记录过期**（+1 行） |
| 7 | `modules.tensor.shape_types` / `as_runtime_mismatch` | "conversion to 'f32[4]' failed: the array has shape [3]"；`as_runtime_mismatch.cml:8:15` | 同一句；`9:15` | **记录过期**（+1 行） |

## A 组（第 3–7 条）：记录过期 —— 只差一行，而且运行期是对的

### 设计页依据（这五处按设计就该在运行期报）

- **`slice_runtime`**：`docs/design-principles.cn.md:269`"**越界**：编译期能知道长度时（尤其是固定长度的类型），`slice` 与下标的越界应当是编译期错误……运行期的 `slice` 越界是故障"。用例的长度来自 `load_npy`（静态不知），所以运行期报 `RuntimeDiag::SliceOutOfRange` 正是设计要的。
- **`as_runtime_mismatch`**：`:411`"**形状未知的数组进入声明了形状的位置**：不隐式放行，用 `as` 显式转换，**转换处做检查**"。转换处就是运行时。
- **`return_extent_checked` / `unknown_extent_mismatch`**：`:410`"`?` 是不需要名字的任意大小（编译期未知，**调用处做运行时检查**，与 `as` 相同）"。两处 extent 静态未知 ⇒ 在调用处查。
- **`si_computed_extent_run_time`**：`:417`"字面量、维度名、`constas` 值参数各种写出 extent 的方式走同一个检查。步长为常量 0 的切片、常量下标或常量切片端点越出**已知 extent**，都是编译期错误"——只对**已知** extent 要求编译期；这里 `n` 静态未知，reshape 运行时失败是对的。

### 新记录应当是什么

名字、阶段都不动（`RuntimeDiag::SliceOutOfRange`；`conversion to …` / `reshape changes tensor element count: …` 这两句消息原文保留——注意 `RuntimeDiag::RuntimeError`（`0x000000`）只是兜底 tag，plan 钉的是消息，不是 tag），**只把位置 +1**：

| 用例 | 旧位置 | 新位置 |
|---|---|---|
| `slice_runtime.cml` | `6:` | `7:` |
| `err_return_extent.cml` | `8:16` | `9:16` |
| `unknown_extent_mismatch.cml` | `9:17` | `10:17` |
| `computed_extent_run_time.cml` | `7:25` | `8:25` |
| `as_runtime_mismatch.cml` | `8:15` | `9:15` |

位移归因：五个文件最后一次改动都是 `eb20efab7b`"notation: the corpus writes a return type after `=>` and a composition as `<>`"（一次 `camel-format -i` 过了 4163 个文件），单文件 diff 分别是 2+/1−、4+/3−、4+/3−、4+/3−、3+/2−，净 +1，与五个位移一一对得上。

## B 组（第 2 条）：真缺陷 —— 检查在库的一次转发之后丢了

用例 `test/cases/consistency/new_values/dropped_foreach_lambda.cml:9-14`，两处都把元素函数写在原地：

```cml
    (0 .. 3)->foreach<func (i: int) sync {
        out->append<i>          // 10:9
    }>
    (0 .. 3)->foreach<func (i: int) { out->append<i> }>   // 12:33
```

期望 `UnusedPureResult`（`10:9` 与 `12:33`），实测 **exit 0、stderr 空**。

### 机制（应当报的那一处，与让它不报的那一笔）

丢弃值的报告在**调用点**做，要求两件事同时成立：

- `src/compile/src/gir/builder_call.cpp:2003-2018`：解析到的算子必须带 `callSpec->discardsCallbackResult`（`foreach` 的数组算子声明了它：`src/stdlib/std/builtin/executors/builtin.cpp:932` `declareCallSpec(":mark/foreach_arr", {.discardsCallbackResult = true})`），**且**该实参的**构造节点**在 `lambdaGraphs_` 里（`src/compile/src/gir/builder.cpp:1176-1178`：只有 `name.empty()` 的函数——即写在原地的——才登记）；
- 而 `foreach(r: Range)` 是 2026-11-26 新加的 **Camel 包装**：`30ba175fab`"stdlib: a range has foreach, as it has map and filter"，`src/stdlib/prelude.cml:1139-1144`

  ```cml
  with <f: (item: int) => void>
  export func foreach(r: Range) => void {
      return to_array(r)->foreach<f>
  }
  ```

  实参以**名字 `f`** 下传。到真正丢弃值的那个算子调用处，手上只有一个名字，没有那个字面量的节点 ⇒ `lambdaGraphs_.find(input.expr)` 落空 ⇒ 一条都不报。

### 为什么判缺陷，而不是"记录过期"

同一族的别的写法都还活着：`: void` 函数体走 `src/compile/src/gir/builder.cpp:1758`（`rejectDiscardedValue(bodyKeyOf(graph.get()))`，用例 `dropped_void_body.cml` 绿）；语句形态（后面还有 `return null`）走 `builder.cpp:2020-2021` 的逐语句路径（`unused_in_lambda.cml` 绿）。只有"元素函数写在原地 + 经范围包装转发"这一种静默。用例注释与 plan 的 `desc` 写的都是规则本身（"its body's last statement `out->append<i>` is the error UnusedPureResult (block body and expression body)"），不是"某一种拼法碰巧不报"。

**需要负责人裁决的一点**：设计页没有直接写"库里转发的元素函数要不要报"（`:267` 那条讲的是同步块/`if` 臂/未使用的 `T!`，与本规则不是同一条），规则目前的权威是用例与 plan。按"检查丢了"判缺陷；若负责人认为包装后的写法可以静默，那这条改判"记录过期"，把用例改成数组/算子直传形态即可——但那是改用例，不在本卡范围。

**新记录应当是什么**：若修复，名字 `SemanticDiag::UnusedPureResult` 不变，位置仍是 `10:9` 与 `12:33`（`expect_diagnostic_line = 10, column = 9` 已经是对的）。要动的不是期望值，是"包装里还能不能看见调用点的字面量"。

## C 组（第 1 条）：真缺陷 —— 拒绝正确，诊断走错路

用例 `test/cases/compile/operators/int_mat_assign.cml:8` 一行 `a @= 2`。现在的原文：

```
int_mat_assign.cml:8:7: [Error]: Reference '__assn_mat__' is declared as imported but is not exported by its module.
Check that the module exports the name '__assn_mat__' or remove the import declaration.
(name=SemanticDiag::ImportNameNotExported, code=0x03000015)
```

### `@=` 是合法的复合赋值

- 设计 `:319`："**复合赋值 `a op= b` 就是 `a = a op b`**：只要 `a = a op b` 是合法的赋值（`a op b` 有定义，结果能赋给 `a`），`a op= b` 就合法，不论类型是谁定义的；否则是错误。"
- 设计 `:302` 把矩阵乘当运算符对待："……要把**矩阵乘**或加减法写成两行，把运算符留在上一行末尾。"
- 语法侧一直在：`src/parse/include/camel/parse/syntax/token_kinds.def:97` `CAMEL_PUNCT(AtAssign, "@=")`、`src/parse/src/syntax/parser_expr.cpp:127`、`src/parse/src/ast/builder_expr.cpp:500`（`BinaryDataOp::AssignMat`）。

所以 `a @= 2` 该在编译期被拒，而且该拒在**算子**上。

### 应当报什么（记录是对的，不是过期）

`b98f40ee00`（2026-10-11）自己的口径："such a combination is rejected when the program is compiled, **at the operator**, and the message of `NoMatchingFunction` names the operator the program wrote."——正是 plan 记的 `SemanticDiag::NoMatchingFunction` + `(int, int)` + "operator '@=' is not defined for these operand types"。

### 机制：哪一处应当报，哪一笔让它不报了

- `@` 的复合赋值名映射到 `__assn_mat__`：`src/kernel/core/operator_names.cpp:56` `{"@", "__assn_mat__", "__mat__"}`。
- 这个组在 builtins 里是**空组**：`src/stdlib/std/builtin/modules/globals.cpp:1232-1233`

  ```cpp
  // No built-in type has `@`; a module that has it (tensor) provides `@=` or it is rejected.
  OperatorGroup::create("__assn_mat__", {}),
  ```

  空组没有任何算子导出这个名字，于是**名字解析在算子解析之前就失败**：`src/compile/src/gir/builder_expr.cpp:1294-1298`（`importedName && !opt.has_value()` ⇒ `ImportNameNotExported` + `BuildAbortException`）⇒ 用户看到一个自己从没写过的内部名字，还被告知"检查一下导入声明"。
- 这一行的形式在 `b98f40ee00` 就落地了（`git log -S'OperatorGroup::create("__assn_mat__", {})'` 命中 `b98f40ee00`、`0109b74b73`、`10c9d41558` 的搬运），所以不是新改动弄坏的，而是**从实现之初就没走到"算子层拒绝"那条路**。

**新记录应当是什么**：不改。记录与设计一致，该改的是实现（让空组走算子层拒绝）。真修好之后，位置应当是算子调用处 `8:5`；现在 `8:7` 是 `__assn_mat__` 那个名字 token 的位置。

**顺带一条（本卡只看机制、不处理）**：同一处意味着**张量上的 `@=` 也不通**——tensor 的 matmul 只导出 `__mat__`/`matmul`（`src/stdlib/tensor/ops/linalg_ops.cpp:184` `.exports = {"__mat__", "matmul"}`），没有任何算子导出 `__assn_mat__`。而全仓库没有一条用例钉张量 `@=`（只有 `test/cases/tools/grammar/operators.cml:32` 的语法夹具），所以没人发现。

## 交回

- 7 条 = **5 记录过期（只改行号 +1）** + **2 真缺陷**（第 1、2 条）。
- 改动：`src/` 0 处、期望值 0 处、用例 0 处；没有跑任何测量。
- 需要负责人裁决的只有一处：B 组"库里转发的元素函数该不该报"。其余六条可以直接按上表处置。