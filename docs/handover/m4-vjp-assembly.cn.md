# M4 组装：库层 `vjp` 已按设计 4.4 拼好，落地只差 `transpose` 收残差

对象：`stdlib/vjp.cml`，分支 `lane-m4-assembly`（从 `lane-m4` 的 `de4b952bf` 开，一次提交）。
判据：组装"完成"当且仅当探针 **`node test/tools/have-library-vjp.mjs` 打印出 `12.0` 与 `9.0` 并退出 0**。
那就是落地那天唯一的验证命令——它先建库、再跑对照用例，两个数直接印在上面。

## 组装长什么样

```camel
let (forward, linear) = split(jvp(f))
return (x, y) => {
    let (value, residuals) = forward(x, y)
    (value, (ct: float) => transpose(linear)(residuals, ct))
}
```

`split` 交回的是**两半**，所以按名字拆开、只把线性那半给 `transpose`；
`transpose(linear)(残差, ȳ)` 正是作者 06:50 裁定里"已知值在前、余切在后"的那一条。表面是 JAX 的
（同一裁定）：`vjp(f)(x)` 给 `(y, pullback)`，`pullback(ȳ)` 给余切，`grad(f)(x) = vjp(f)(x).1(1.0)`；
并列形状只在文件内部，没有对外签名。

`Vjp<F>`/`Split<F>`/`ParamsOf<F>` 还没有（设计 6.1），所以 `vjp` 按 arity 各声明一次（一个 `float`
参数、两个 `float` 参数）——`tensor` 对"一个名字按接受的类型声明一次"就是这么做的，泛型落地时换掉声明、不换 body。

## 今天的读数（可复现）

```
$ node test/tools/have-library-vjp.mjs          # 退出 1
the library `vjp` stops at the type layer (M4): `transpose` does not accept the shape `split`
hands back, so `transpose(split(reinterp<jvp>(f)))` has no call at all and no graph is walked.
  evidence (the compiler's diagnostic, and the line of the library it points at)
    error[SemanticDiag::ArgumentsRejected]: Invalid call to 'transpose' with argument types <> (((float,), float) => float): `transpose` takes a function of exactly one parameter.
    --> …/out/latest/stdlib/vjp.cml:47:41
  what is missing: a type for `transpose` over the second half of a split -- the known values,
  then the linear parameters, then the cotangent.
```

探针按**撞墙顺序**报，每一条都带凭据：类型层 → 图层（`TRANSPOSE … skip=…` 读数行 + 调用点的
`RuntimeDiag`）→ 跑 `12 / 9`。它不认节点种类、不认行号，所以 `transpose` 收下残差那天它会自己往下走一层，
转置 `Call` 打通那天它自己走到第三层，没有人去改代码。`--missing` 问的是反面（组合还建不起来时为 0），
`vjp_api.names_the_missing_step` 靠它在组合能建的那天自己退出。

计划：`node scripts/test.js test/plans/feat/compile/transforms.plan.toml`
→ `[SUMMARY] total: 18, pass: 16, fail: 0, skip: 2`（两个 skip 都是等 `transpose`／`Call`）。

## 落地那天跟着动的地方

1. `vjp_api.names_the_missing_step` 自己跳过（`--missing` 变 1）；它的 `stdlib/vjp.cml:47/59:` 断言随之作废，
   不用谁去删。
2. `vjp_api.analytic_gradient` 自动开始跑并断言 `12.0` / `9.0`；`have-vjp.mjs` 门后的
   `vjp_gradient.cml` 也已改成 JAX 形状（27.0 三个数不变）。
3. `scripts/stdlib-reference.mjs` 里 `vjp` 的 `pending:` 一行删掉再 `--write`，参考页就收进这个模块；
   `--verify-docs` 在它编得过的当天会自己报"该删这一行了"。

## 这支分支今天为什么也是绿的

组装没让任何既有断言失效：拒绝那一读仍然停在库自己那一行（只是行号与措辞跟着组装走），
两个数仍然由门挡着。所以它可以随时并进 `develop`；两个数在 `transpose` 收残差之前不会出现，也不会假装出现。