# 问：这条暂停是否覆盖"`vjp` 并成一条泛型所需的 lambda 形参表"？

**一句话**：`vjp` 并成一条声明，需要的不是新机制，而是 17:30 暂停的那一台——"按 lambda 所在的函数类型
替它建形参"。名单上它同时是"留下"（作者 16:10 的 ①：`vjp` 用 `..xs` 并成一条泛型）和"被暂停"（前端变
换表那套的后果）。**这是名单自相矛盾，不是做不到**，所以停下来请裁。

## 读数（都是今天实测的原文）

1. 把 `vjp` 写成一条泛型、体内 `return transpose(split(jvp(f)))`：

       error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types:
         <> (((float) => (float, ()), ((), float) => float)).
         --> stdlib/vjp.cml:60:22   (code=0x0300000F)

   **`split(jvp(f))` 是一对函数，`transpose` 要的是一个函数**（`is_function::<F>`）。要把它们接起来
   ——forward 半边算出的残差喂给 transpose 的线性半边——必须由一个形参表是 `..xs` 的 lambda 做。

2. 形参表这一格今天的三条路都试过（`docs/handover/reports/2026-11-02-a-lambdas-rest-parameter.cn.md`
   第 3 节）：无类型的 `(..xs)` 被绑成无法实例化的类型变量；`(..xs: float[..])` 的包原样进运行时图
   （`undetermined type: '..float[..]'`）；`(..ys: ..ParamsOf<F2>)` 的包是一个槽，调用展不开它
   （`cannot be called with argument types: <> (int, float)`）。

3. 类型那一半已经在 `.cml` 里了（`d8105565a`）：`Vjp<F>`/`Pullback<F>`/`Jvp<F>`/`Transpose<F>` 只
   用 `ParamsOf`/`ResultOf`/`Tangent`，没有给类型模型加字段。也就是说**声明能写出来，体写不出来**。

## 三条路与各自的代价

- **A（默认，今天就按它继续）**：`vjp` 维持两条声明（元数 1 与 2），等裁定。代价：`Vjp<F>` 作为返回
  类型暂时用不上，`vjp` 的签名仍在 C++ 里；好处是零风险，`vjp_api` 9/9 与 `grad` 的读数一个字不动。
- **B**：只放开"lambda 的形参表"这一格——按 lambda 所在位置的函数类型摊平它的 `..xs`。代价：这正是
  17:30 暂停的那台机制（"前端在建图时替它建形参"），要重新评估被撤掉的那一套是否必须回来；收益是
  `vjp` 真能并成一条，且 `Vjp<F>` 从文档词变成签名。
- **C**：让 `transpose` 收一对函数（`split` 的产物），自己完成残差到线性半边的接线。代价：`transpose`
  的签名从 `Transpose<F>` 变成"一对函数 → `Pullback<F>`"，现存的 13 条 `split` 用例与 transpose 的
  规则集要跟着改；而且 `transpose` 变成一个"知道 `split` 形状"的算子，两个变换从此互相依赖。

## 需要作者回答的一句

> B 与 C 里选哪条，还是维持 A 到 M6 之后再说？若选 B，17:30 的暂停是否只覆盖"前端变换表提前求值"，
> 不覆盖"lambda 的形参表按所在函数类型摊平"？

在裁定之前，`vjp` 保持两条声明，`..xs` 的下降（`NK::RestValue`）仍是一笔独立的事，二者都不动。