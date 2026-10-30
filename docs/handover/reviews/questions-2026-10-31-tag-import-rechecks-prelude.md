> **裁定：①**（作者预先授权修）——**隐式已加载的模块被 import 时不得重新检查**；不选②（它会让诊断看引入顺序）。
>
> **裁定：已修。根因不是标签，是 `checkPreludeDeclarations` 造了 prelude 却没登记进 `modulesByPath_`，
> 于是同一文件被检查第二遍。修在 `1ab619a57`。** 页不删，留着：下一个人要知道这条路为什么长这样。
>
> 根因已定位：`Context::checkPreludeDeclarations` 读出的那份模块没有登记进 `modulesByPath_`，于是
> `import` 一个 `prelude` 的名字时 `Context::importModule` 在路径缓存里查不到它，把同一个文件读第二遍、
> 检查第二遍——第二遍没有 `builtinClaims_` 那一手，文件里两条 `#op` 声明（`string` / `:op/*tos`、
> `attempt` / `:mark/attempt`）就报成"没有已加载的模块实现它"。修的是登记，不是声明。

# 从 `prelude` 引入一个名字，会让 `prelude` 自己的 `#op` 声明在没有实现模块的上下文里求值

从 `prelude` 引入标签 `jvp` 之后，报错落在 `prelude` 里两个**一直存在**的声明上。这不是"`prelude` 放不下标签"——四组探针已经否掉了那个推断；这是引入本身触发的重检查。

## 现象到符号

**步骤 4（一次构建，只动两个变量：把 `export tag jvp` 从 `stdlib/autodiff.cml` 搬到 `stdlib/prelude.cml`，把 `test/cases/compile/tags/imported_same_name.cml` 的 `import { jvp } from autodiff` 改成 `import { jvp } from prelude`）**：

```
[SUMMARY] total: 46, pass: 45, fail: 1
(prelude)stdlib/prelude.cml:311:1: [Error]: The declaration 'string' names the operator
  ':op/*tos', which no loaded module implements. A body-less declaration stands for an
  operator of a loaded module.
```

同一形状的第二条：`prelude.cml:564: The declaration 'attempt' names the operator ':mark/attempt', which no loaded module implements.`

**三个可查的点**：① **触发者是"引入"**——把 `jvp` 声明在 `prelude` 里、没人从 `prelude` 引入时（步骤 1：45/46，只有那条旧引用的用例红）一切正常，一旦用例写 `import { jvp } from prelude` 就出现；② **落点是 `#op` 声明**——`string`（`#op(':op/*tos')`）与 `attempt`（`#mark/attempt`）都是 `prelude` 原有的无体声明；③ **报的是"没有已加载的模块实现它"**——诊断说 `prelude` 被检查时那些实现模块不在场。

## 已排除的可能

- **不是"`prelude` 不能声明标签"**：在 `prelude`、`autodiff`、`types` 三处各加一条无用 `export tag`，以及带 `when is_function::<F>()` 再加一条，四组全绿（`feat.compile.rulesets` 各 2/2）。
- **不是 `transpose` 与 `tensor::transpose` 撞名**：四次拆分（只搬 `jvp`、只搬 `vjp`、只搬 `transpose`、三条一起）里，只有搬 `jvp` 的那次红，且红的就是那条还在写 `from autodiff` 的用例；只搬 `transpose` 全绿。

## 为什么不能自己定

改的是"模块在什么条件下被重新检查"，属于语义检查流程；在它定下来之前，"把标签放进 `prelude`"这条裁定无法落地。

## 方向

找"什么条件下模块会被重新检查"：**引入一个已经隐式加载的模块，是否让它走了"首次检查"那条路**。若确认，要修的是"隐式已加载的模块被 `import` 时不得重新检查"，不是标签，也不是 `#op` 声明。

## 选项

- **① 修"隐式已加载的模块被 import 时不得重新检查"**（若定位确认）。**要多造**：引入时的状态判断。**会不会改变已有语义**：不改变——被跳过的只是重复检查，诊断集合应收窄而非扩大。
- **② 让 `#op` 无体声明在被重新检查时也能接受"实现模块稍后加载"**。**要多造**：诊断的时序条件。**会不会改变已有语义**：会——同一份代码在不同引入顺序下诊断不同，这种"看顺序"的语义比现状更难解释。

**默认建议：①**。它修的是根因，且收窄诊断。

在裁定之前，三条标签留在 `stdlib/autodiff.cml`，用例继续写 `import { jvp } from autodiff`，仓库是绿的。