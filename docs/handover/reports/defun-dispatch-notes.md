# 去函数化：绿、红、崩，三种状态都不是"对"

> **绿是"pass 放弃这一族"换来的，不是降对了；把 catch 拿掉是红的错误；把类型修好是 `exit 139`。三种状态都不是"对"。**

这条是今晚关于"绿"的最诚实的一句，放在最前面：`feat/modules/autodiff` 的 245/245 不是"去函数化在这条负载上工作"，而是它**放弃**了 `camel_bench::run_training` 这一族——`session.commit()` 被校验拒（`passes a int as ordinary argument 2 of '__g_83__'`），catch 把它吞成 `DEFUNCTIONALIZE broke_invariant=1` 与一条 `DefunctionalizationIncomplete`，程序原样通过。把 catch 拿掉，同一处变成硬错误；把 `argumentsOf` 的字段类型修对（`variant.captures[field]`，不要问调的**结果**类型要枚举），降级完成，`benchmarks/mlp_big/model.cml` 在训练负载上 `exit 139`。

## 一、分派点的值：`c0b48aba9`

`emitCallThroughValue` 在写 `:enum/tag` 之前先确认被分派的值**就是本族的枚举**；不是就抛，点名牌的函数类型与实际类型，并写明"这是 pass 的缺陷，不是程序的"。今天不响（当前的树在校验处就拒了提交，写不出坏 dispatch），它的价值在某天某个站点漏降时：把垃圾指针变成一条点名错误。

## 二、斜体更正：`__g_83__` 的端口表

早先一次读数说"`__g_83__` 是 1 个普通口 + 12 个捕获口"，**那是失败提交后留下的程序**（no-fix 的 gir），不是降完的形状。把类型修好后同一张图是：

```
N0167 [label="PORT", type="PORT", param_kind="normal", param_index="2",
       value_type="{ fc1_w: f32[..], fc1_b: f32[..], fc2_w: f32[..], fc2_b: f32[..], fc3_w: f32[..], fc3_b: f32[..] }"]
N0105 [label="PORT", type="PORT", param_kind="normal", param_index="0", value_type="f32[..]"]
（index 1 是 string dir）
```

三个普通口 `f32[..]`、`string`、`{…}`，调用点 `N024F/N0255/N026F` 的三个实参逐个对上——**形参表是一致的**，`parameterizeCaptures` 的成果确实落到了这张图上。所以那一条不是"族合流没做全"的现场；`exit 139` 还另有原因，尚未定位（同一簇里仍有一张 12 个 `CLOSURE_PORT`、各为 `f32[..]` 的图，是下一个候选）。

## 三、安装偏差：无害

逐个文件比内容哈希（`*.cml`，两侧排序后 `sha256sum`）：`out/latest/stdlib` **44** 个文件，`stdlib/` **44** 个文件，**哈希差异 0 行**，`time.cml` 两侧都在。那 24 秒是纯 mtime 偏差，内容一致，无害——下次不必再怀疑安装。

## 四、现状

- `lane-defun5`：`c0b48aba9`（断言）← `82f8d47a3`（负例改写）← `6ffdf65ec`（合流）；filter 在 `f6f0dddd1`。
- 未落：类型修复（`enumOf` 改从**值**取、字段类型只认 `variant.captures`）+ `CAMEL_DEFUN_DEBUG` 诊断，停在 `D:\Temp\defun-fix-and-diag.patch`；catch 移除 + `defun-wrapper-variant.md` §七，停在 `D:\Temp\defun-catch-doc.patch`。
  实况更正：`D:\Temp\defun-fix-and-diag.patch`（1439 B）里**只有那两处诊断打印，没有类型修复**；类型修复 + catch 移除在 `D:\Temp\defun-attempt3.patch`（12563 B）。取栈用的是后者。
- 已知红：`benchmark_gradients_vs_torch`（`mlp_big`，训练负载，`exit 139`），只在类型修复落地时出现；当前的树因放弃而绿。**按返工令 `477d76091`，它已登记为 `known_fail`，形状见 `known-failure-run-training.md`——绿不再被允许。**
- 取栈结论（`std::macro std::opt std::nvm`，训练负载）：`Variant::updateRefs+31` ← `scanRelocatedObjects` ← `minorGCUnlocked` ← `safepoint` ← `NodeVMSchedPass::call`（6 层嵌套，不随模型长）。`mlp` 与 `mlp_big` 同样崩，所以与规模无关，不是栈深度。