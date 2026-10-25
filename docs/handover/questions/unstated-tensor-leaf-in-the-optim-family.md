# 未述元素类型的叶子：`optim` 家族的参数树该写什么

车道：`handover/h569-tensor-migrate-d`（族=裸 `Tensor` 删除后的用例迁移）。
本页只记录**实测**、可选项与代价，不为过渡类型发明语义。落点：`65c96f704`。

## 问题（一句话）

删掉裸 `Tensor` 之后，一个「元素类型由值决定、并且同一个结构里可以装着不同元素类型的叶子」的位置
（`stdlib/optim.cml:311` 的 `unstated`，以及照它写的 `test/cases/modules/optim/**`）没有等价写法：
写 `f32[..]` / `f64[..]` 会把用例正在测的「未述」性质写死，写类型变量又不能在同一个结构里给出两个不同的元素类型。

## 证据（逐字）

命令：

```
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h569-tensor-migrate-d -Plan test/plans/feat/modules/optim.plan.toml -Tier fast -Jobs 8 -NoBuild
```

读数：`[SUMMARY] total: 26, pass: 0, fail: 26`，`RUN_EXIT=1`。

26 条失败**每一条**都带同一行库内错误：

```
(optim)/home/zjwei/camel-work/h569-tensor-migrate-d/out/latest/stdlib/optim.cml:311:42: [Error]: The type 'Tensor' states no element type: a tensor's element type is part of its type, so this spelling is not a type of a value. ... (name=SemanticDiag::TensorElementTypeRequired, code=0x030001ED)
```

其中 **12 条**（`optim.f64_leaf_keeps_its_element_type.*`、`optim.native_tensor_leaf_keeps_its_element_type.*`、
`optim.named_argument_crosses_the_overloads.*` 各 4 条）除了这一行和随之而来的 `Cannot import module 'optim'`
（`dtype_exact.cml:17:1`、`native_dtype_exact.cml:31:1`、`named_args.cml:17:1`）**没有别的错误**：
它们自己的用例文件已经写明元素类型，只被库内那一处挡住。

另外 **14 条**还带用例自己的类型位置：

```
agreement.cml:43:13 / :55:8 / :81:14 / :104:17   (optim.trajectory_matches_native.{nvm,fvm,jit,prl})
training.cml:26:13 / :37:16 / :43:14             (optim.training_loop_matches_native.{nvm,fvm,jit,prl})
unstated_dtype_exact.cml:32:11                   (optim.unstated_tensor_leaf_follows_its_value.{nvm,fvm,jit,prl})
unknown_optimizer.cml:14:19                      (optim.unknown_optimizer_is_refused)
unknown_optimizer_update.cml:14:19               (optim.unknown_optimizer_update_is_refused)
```

这些用例的文字写明它们测的就是「未述」：

* `unstated_dtype_exact.cml:1-2`「A tensor leaf whose element type the program does not write follows the element
  type of its value, on both sides」；`:31-34` 的 `Leaves` 一个 `wide: Tensor`（值 `full([2], 0.1)`，f64）、
  一个 `narrow: Tensor`（值 `full([2], f32(0.25))`，f32）；`:58-59` 断言两边分别 8 字节和 4 字节一个元素。
  同一个结构里两个叶子的元素类型不同。
* `agreement.cml:36-41`「Every tensor leaf leaves its element type to the data, and the element type of the value
  is the element type both walks update at」；`:52-53`「`tree_map`'s callback is written for a `Tensor`, so every
  leaf of this tree is one」。
* `stdlib/optim.cml:309-312` 自己的注释：「the two writings above answer at different element types and the walk
  carries one type for the leaf, so this is the seam between them.」——库里那一处是刻意的接缝，不是漏改。

## 可选项与代价

1. **语言侧给出「元素类型随值」的合法写法**（`Tensor<>` 之外的推断型叶子，或按值求元素类型的形参）。
   代价：新增语义，正是本轮删除要收掉的东西，需要设计原则层面的裁定；`stdlib/optim.cml:311`、
   `agreement.cml`、`unstated_dtype_exact.cml` 一行不用改，14 条红随库那处一起消失。
2. **把 `stdlib/optim.cml:311` 的 `unstated` 及其调用点写成 `f64[..]` 或 `f32[..]`**。
   代价：`unstated_dtype_exact` 与 `agreement` 所测的性质（未述元素类型的叶子按值取元素类型）随即消失，
   这两个用例只能改写成别的命题或删掉；`optim` 的叶子走查在类型上也只剩一种元素类型。
3. **给参数树引入按字段的类型变量**（例如 `type Leaves::<W, N> = { wide: W[..], narrow: N[..] }`）。
   代价：`with_grad` / `init` / `update` 的整条计算签名要跟着参数化，`stdlib/optim.cml` 与 `stdlib/nn`
   的计算签名都要改；只改用例无法验证。
4. **只改用例、把结构收成单一元素类型**（两个叶子都 f32）。代价：`unstated_dtype_exact.cml:58-59`、`:73`、
   `:95-100` 那批字节宽度断言失去意义，`agreement.cml:104` 的 f64 叶子同理，等于删测试。

## 阻塞的 plan 条目与条数

`test/plans/feat/modules/optim.plan.toml`，26 条全红：

```
optim.trajectory_matches_native.{nvm,fvm,jit,prl}          (4)
optim.training_loop_matches_native.{nvm,fvm,jit,prl}       (4)
optim.f64_leaf_keeps_its_element_type.{nvm,fvm,jit,prl}    (4)
optim.native_tensor_leaf_keeps_its_element_type.{nvm,fvm,jit,prl}  (4)
optim.unstated_tensor_leaf_follows_its_value.{nvm,fvm,jit,prl}     (4)
optim.named_argument_crosses_the_overloads.{nvm,fvm,jit,prl}       (4)
optim.unknown_optimizer_is_refused                          (1)
optim.unknown_optimizer_update_is_refused                   (1)
```

其中 12 条只被 `stdlib/optim.cml:311` 一处挡住，与上面选项 1/2 的选择无关；另外 14 条还要用例自己的类型位置
一起定下来。`test/cases/modules/optim/**` 的六个文件本车道未改。