# 数组打印形式翻转后剩下的缺口（实测，2026-10-25，h454 lane）

本 lane 执行 2026-10-24 夜作者裁定「打印只有一个形式：数组形式」（元素按自身类型的最短文本、
多维用嵌套方括号）与「元素类型未述的叶子（过渡拼写 `Tensor`）改用终态写法」。改动只落在
`test/plans/feat/{opt,linear,trans,examples,compile,consistency}/**` 与它们引用的
`test/cases/**`，外加 `src/core/rtdata/print.cpp` 一个已获协调者批准的缺陷修复（rank ≥ 2 时
每一层的 `[` 只有最外层写过，见下）。

实测：in-scope 六族红 79 → 15；整层（`smoke,test/plans/feat`，`-Tier fast -Jobs 8`）
`[SUMMARY] total: 6160, pass: 5847, fail: 294` → `total: 6160, pass: 5916, fail: 225`
（转绿 69 条、**新增红 0 条**、无换名）。剩下的 15 条**都不是**打印形式问题，本 lane 不能
自行裁定或不该由本 lane 改，逐条给实测证据与归属。**本 lane 没有发明语义、没有放宽断言、
没有新增 `known_fail`。**

## 一、`constas` 的形状实参不再被静态拒绝（需裁定：新语义还是缺陷）

```
$ camel test/cases/compile/constas/constas_argument_shape.cml
4.0                                                       # exit 0
```
计划期望 `expect_exit = 1` 且 `SemanticDiag::GenericArgumentsMismatch`。要裁定：泛型实参里
的形状/维度实参经 `as` 之后还要不要静态比对？选项 A：恢复静态拒绝（期望不动）；选项 B：改为
运行期形状检查（那期望要改成 exit 0 打印 `4.0`，属于语义变动，同样要作者点头）。

## 二、秩 0 张量上 `for` 的运行期报错文本（h453）

```
期望: iter: a rank-0 tensor has no first dimension
实际: test/cases/loops/fault_tensor_rank0.cml:16:34: [Error]: An error has occurred:
      Tensor argument cannot be null. ...
```
现状把「没有第一维」说成「参数不能为空」，会误导（同族的编译期孪生 `err_tensor_rank0`
已按实测改成 `13:34 SemanticDiag::LoopSequenceNotIterable`，那条是绿的）。要裁定：这条运行期
消息恢复旧文本，还是按新语义改写（由 `for`/秩 0 的 owner 落）。

## 三、`std::cpp` 拒绝时点名谁（trans/cpp owner）

```
期望: Translation pass 'std::cpp' refused the graph: function 'scale' returns a value of
      type 'Tensor'; std::cpp translates numbers …  (line 10, column 1)
实际: test/cases/trans/cpp/refuse_tensor.cml:15:19: [Error]: Translation pass 'std::cpp'
      refused the graph: function 'main' uses the operator 'tensor:ones' on a value of
      type 'int[]', which has no translation in std::cpp yet.
```
`refuse_tensor_json` 同因（校验脚本按期望消息匹配）。要裁定：拒绝的**顺序与归因**应该先说
容器返回值（`scale`），还是先说图里第一个不可翻译的算子（`tensor:ones`）？两条用例的期望
随之改写，但这不是本 lane 能定的。

## 四、`grad of P` 与终态拼写（autodiff owner）

`feat.compile.namespace module_import_builtins` 实际 exit 1：`nn::sgd` 的
`(params: P, grads: grad of P, lr: float) => P` 拒绝了 `(f32[2, 2], f32[2, 2], float)`。
要裁定：`grad of P` 判定一致性时，元素类型「已述 / 未述」的差别怎么算（旧拼写下它曾经通过）。

## 五、`expect_diagnostic_name` 解析不了「带候选清单的诊断」（harness/CLI 缺陷，不是语言问题）

`test/plans/feat/compile/array_alias.plan.toml` 的 `alias.type_text.nvm`、
`alias.err_var_rank.nvm` 报 `expected diagnostic SemanticDiag::NoMatchingFunction`，
但名字就在输出里。实测（按 runner 的做法设 `CAMEL_ERROR_FORMAT=short`，逐行给长度）：

```
347 len: |(array_alias_type_text)…/type_text.cml:14:18: [Error]: No matching function found … Available overloads: 
 60 len: |    takes: (a: f32[..], b: Tensor<[3]>, c: f32[2, 3]) => int
 57 len: | (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```
诊断正文后跟候选清单时，`(name=…, code=…)` 尾标落在**单独一行**，于是
`test/tools/run-tests.mjs:658` 的逐行正则（要求尾标与 `path:line:col: [Error]: …` 同一行）
永远匹配不到。`type_text` 的另一条断言（候选行里同时出现三个拼写）**已经满足**：实际输出含
`takes: (a: f32[..], b: Tensor<[3]>, c: f32[2, 3]) => int`。修在 CLI（尾标放回消息行）还是
runner（接受独立尾标行）需要裁定；本 lane **不放宽**这条断言。

## 六、不是语言裁定、按 lane 归属的实现缺口

| 条目 | 实测 | 归属 |
|---|---|---|
| `feat.opt.screens nn.lrit` / `nn.mnist` | `=` 收到 `(f32[?, ?], Tensor)`（78:14 / 92:14）——`test/cases/modules/**` 里 `Tensor` 与 `f32[?,?]` 混用 | h450 |
| `feat.opt.screens onnx.export_branch` | `(void, float)` 于 `>`（13:22）：裸 `Tensor` 取出的叶子是 `void` | h450 |
| `feat.opt.generic fold_cast.rewrites` | `a conversion of a constant was not folded: 4 CAST nodes left` | h457（passes） |
| `feat.compile.operators tensor_add_assign` | `var a: Tensor<f32, [3]>` 显示成 `f32[]`，`+=` 15:5、`*=` 17:5 无重载 | h455（`var` 拼写） |
| `feat.compile.diag_catalog catalog.doc_is_generated` | `docs/diagnostics.md is not what diagnostics/registry.tsv produces; run node scripts/diagnostics-catalog.mjs --write` | docs owner（本 lane 不动 `docs/**` 期望） |
| `feat.consistency.runner runner.backend_matrix` | 自测第一行即 `plan-selection: 6 tracked file(s) match no rule …: notes/*.py (notes/h419-after-collect.py …); notes/*.log …; notes/*.txt …` | h419 / `test/plan-selection.json` owner |

## 七、本 lane 自己解决掉、不需要裁定的两条（记录判定线）

- `compile.return_type tensor_shape`：实测已是终态拼写
  `Return type 'f32[2, 3]' does not match the function's declared return type 'f32[3, 2]'`，
  于是**改用例**（`Tensor<[2, 3]>` → `f32[2, 3]`）并把期望改到实测文本，而不是把
  `Tensor<[2, 3]>` 这种过渡拼写钉进期望。
- `opt.generic arms_laid_out_whole_tensor` / `trans.annotated_dump boundaries`：把裸 `Tensor`
  的形参、返回、装载值改写成终态拼写（`f32[..]`，`boundaries` 用 `f32[4]`），
  实测 `r 0.4375` / `gate=1.0` 与用例文档注释里的期望值一致；两份 `.dot` 快照用原始 dump
  重录（`normalize_node_ids` 按它自己的注释要求黄金文件保留绝对 id）。

## 附：`src/core/rtdata/print.cpp` 的共享修复

`pushArray` 只压每个层级的 `]`，外层 `[` 由调用者写一次，于是 rank ≥ 2 的 `println` 丢掉所有
内层 `[`（实测 `println(zeros([2,2,2]) + 1.0)` → `[1.0, 1.0], 1.0, 1.0]], …`）。
h438（`32db3ae92`）与 h451（`ece64b777`）已各自修过同一个缺陷，本 lane 取同一补丁：
`pushArray` 末尾补 `pushText(stack, "[")`，`printValueImpl` 不再自己写外层 `[`。