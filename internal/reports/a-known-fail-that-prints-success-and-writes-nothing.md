# 一条 `known_fail` 打印了 success、什么都没产出：`export_train_step_dynamic` 与其 `_mean` 孪生

这两个条目今天都 **exit 0、打印 `exported`、不写 `.onnx`**。计划里它们仍是 `[XFAIL]`——因为失败只剩
`verify` 那一侧的一条 `ENOENT`。也就是说：**用例在说成功，产物不存在，而标记把这句话当成了"已知失败"**。
这一页给三样东西：今天的原文读数、那一行吞掉错误的 `let _ =`、以及"exit 0 是错误被丢弃还是写文件的路没走到"
的分辨读数。两个原因处置相反，所以必须分辨，不能靠一句"红了"了事。

## 一、今天的读数

两条用例都在一个空目录里跑，`ONNX_OUT` 指向该目录（计划里由 `envs` 给的正是这个形状）：

```
$ camel test/cases/modules/onnx/export_train_step_dynamic.cml      (ONNX_OUT=D:\Temp\kf-evidence\dynamic\export_train_step_dynamic.onnx)
exported
exit=0
$ ls D:\Temp\kf-evidence\dynamic
0                       # 目录里 0 个条目：.onnx 不存在

$ camel test/cases/modules/onnx/export_train_step_dynamic_mean.cml (ONNX_OUT=D:\Temp\kf-evidence\dynamic_mean\export_train_step_dynamic_mean.onnx)
exported
exit=0
$ ls D:\Temp\kf-evidence\dynamic_mean
0
```

计划那一侧看到的只有产物缺失（同一棵树，`feat/modules/onnx`）：

```
[XFAIL] export_train_step_dynamic
    [INFO] verification exited 1
    [INFO] verification output: Error: ENOENT: no such file or directory, open
           '...\modules.onnx\export_train_step_dynamic\export_train_step_dynamic.onnx'
    [INFO] at readOnnxModel (test/cases/modules/onnx/onnx_ops.mjs)
```

一句话：**`expect_exit = 0` 与 `expect_stdout_contains = ["exported"]` 都满足了，唯一没满足的是"文件在那儿"，
而这一条是 `verify_script` 说的，不是程序说的。**

## 二、吞掉它的那一行

`test/cases/modules/onnx/export_train_step_dynamic.cml#main`（`_mean` 同形）：

```cml
let _ = export_model<out: get_env('ONNX_OUT') ?? '', dynamic_axes: [0]>(step, (m, x))
println('exported')
return 0
```

`export_model` 的返回类型是**错误可携带的**，见 `stdlib/onnx.cml#export_model`：

```cml
export func export_model::<F, E>(fn: F, example: E): void! when is_function::<F>()
```

`void!` 是"一个 void，或者一个错误"。`let _ =` 把它绑到 `_`，于是**错误值不经过 `main` 的出口**——
语言只在错误值到达出口时才诊断（`RuntimeDiag::UnhandledError`，帮助语原文："A value of type T! that has
failed must be opened before the program ends"）。所以程序继续走，打印 `exported`，返回 0。

## 三、分辨读数：路径走到了，错误被丢了

把同一份用例复制到仓外（`D:\Temp\kf-probe-dynamic.cml`、`kf-probe-mean.cml`，**不提交、不改用例**），
只动一处：不丢弃返回值，而是把它交给 `main` 的出口。

```
$ camel D:\Temp\kf-probe-dynamic.cml        (ONNX_OUT=D:\Temp\kf-onnx-probe\probe.onnx)
exported
error[RuntimeDiag::UnhandledError]: An error value reached the exit of 'main' without being handled:
  ExportUnsupported: in 'step': tensor:matmul_grad_lhs: the rank of an operand is not known statically.
   --> D:\Temp\kf-probe-dynamic.cml:27:26
 27 |     return (vg.0, sgd(m, vg.1, 0.01))
exit=1
file_exists=False
```

`_mean` 的探针读数逐字相同（`kf-probe-mean.cml:26:26`，同一句 `tensor:matmul_grad_lhs`，同样 `file_exists=False`）。

**分辨结论**：`exit 0` 是**错误值被 `let _ =` 丢弃**，不是"写文件的路没走到"。判据是探针里那句 `ExportUnsupported`
——它只能由 `export_model` 自己产生；若那条路没执行，探针会打印 `exported` 后安静地 exit 0，一句错误都没有。
（另一个方向的旁证：两次运行里目标目录都还是空的，说明"尝试过、被拒、没写"。）

两个原因的处置正好相反：**错误被丢弃**要修的是用例这一侧（错误值必须被打开，或在计划里让失败可读）；
**写文件的路没走到**要修的是导出器。今天的读数是前者。

## 四、这条 mark 的**理由句今天仍然是真的**，但它不可见

`known_fail_reason` 写的是"the export is still refused by `tensor:matmul_grad_lhs: the rank of an operand is
not known statically`"——探针把这句话原封不动打印出来了，**所以它不是陈旧的，它是不可观测的**：没有
`known_fail_msg` 去钉它，而用例自己把同一次拒绝丢掉，`verify` 只看得到 `ENOENT`。这一条不能按"过期"处置，
要按"失败被藏起来了"处置。

## 五、必须写死的一句：没有 `known_fail_msg`，runner 把**任何**失败都记成 xfail

`test/tools/run-tests.mjs#executePlans` 的判定（原注释也在那里）：

```js
if (entry.test.__knownFail) {
    if (result.status === 'fail') {
        const pinned = entry.test.__knownFailMsg
        const seen   = `${(result.failures || []).join('\n')}\n${result.stdout || ''}\n${result.stderr || ''}`
        if (pinned !== null && !seen.includes(pinned)) { /* 变成 fail：changed its mind */ }
        else { result.status = 'xfail' }
    }
```

`pinned === null`（没有 `known_fail_msg`）时，`else` 分支无条件把结果记成 `xfail`。**这意味着一个完全不同的缺陷
可以借用旧名字，而永远不会出现一句可复核的话**：今天 `export_train_step_dynamic` 的失败已经是"用例吞掉导出错误"，
与它标注的理由判据无关，runner 却照旧记 `xfail`。全树这样的条目有 8 条：

`transforms.with_grad_from_the_library`、`export_train_step_dynamic`、`export_train_step_dynamic_mean`、
`ort_train_step_dynamic`、`pass_export_equals_export_model`、`ort_dynamic_params`、`ort_loop_values_dynamic`、
`ort_loop_loaded_weights`。

结论：**没有理由句本身就算陈旧**——一条不写它等什么的 `known_fail`，下一个人无法判断它是否过期，它是一个
永远不会被复核的条目。

## 六、附：闸门的 `known failures: 34` 与全树 31 个标记是两个口径

`test/tools/run-tests.mjs#executePlans` 里 `summary[outcome.status]++` 按**每次执行**加一（一条声明跑两个后端
就是两个 `entry`），而 `testTier` 让 `milestone` 不进 `--tier fast`。所以：

**34 = 31 + 5 − 2**：31 条声明；其中 5 条声明了 `["nvm","fvm"]` 两个后端，多出 5 次执行
（`rules_angle_args.mean_axis_transpose`、`compositions.grad_vmap_f`、`compositions.vmap_grad_f`、
`compositions.jvp_vjp_f`、`compositions.grad_vjp_g_zero`）；再减掉 2 条 `milestone` 的标记
（`test/plans/perf/showcase_c2.plan.toml` 的 plan 级 `tier` 与 `test/plans/feat/modules/onnx.plan.toml#pass_export_equals_export_model`
的 entry 级 `tier`），得 36 − 2 = 34。同理，那 8 条无 msg 的条目在 fast 层里是 7 条——`pass_export_equals_export_model`
正是被 milestone 挡掉的那一条。