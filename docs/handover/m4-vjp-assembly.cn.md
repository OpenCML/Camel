# M4 组装：库层 `vjp` 已按设计 4.4 拼好，落地只差 M3 转置那次调用

对象：`stdlib/vjp.cml`，分支 `lane-m4-assembly`（含 `lane-m4` 与 `origin/develop`，含 `split` 改成残差图定那笔）。
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
$ node test/tools/have-library-vjp.mjs          # 退出 0：组合编得过，跑不起来
the library `vjp` stops in the graph (M4): the type layer takes the shape `split` hands back and
the module compiles, but the walk that runs a function backwards refuses a construct inside it.
  evidence (the walk's own reading, then the diagnostic raised at the `transpose` call)
    TRANSPOSE function=split_of_jvp_of_f_backward skip=call node=11 callee=none
    error[RuntimeDiag::RuntimeError]: An error has occurred: transforms: transpose has no transpose for this function.
  what is missing: transposing that construct -- giving it a transpose of its own and replacing
  the site it is written at, which is the step named to the M3 lane.
```

`split` 改成"残差归图"（作者 08:10 裁定）之后，类型层不再拒：残差位是没人写得出的 `_`，
`Split<F>` 的形状由编译器在实例化时填，所以 `vjp.cml` 不写它，按 `let (forward, linear) = split(jvp(f))`
让编译器推。一个参数的 `vjp` 里那次 `transpose` 给的是**一个元素的元组**（参数只有一个），
pullback 取出那个数给出——因为 `grad(f)(x)` 和 `vjp(f)(x).1(1.0)` 是一个数。

探针按**撞墙顺序**报，每一条都带凭据：类型层 → 图层（`TRANSPOSE … skip=…` 读数行 + 调用点的
`RuntimeDiag`）→ 跑 `12 / 9`。它不认节点种类、不认行号，所以墙往下一层挪的那天它自己跟着挪，
没有人去改代码。`--missing` 问的是反面（组合建不起来时为 0），
`vjp_api.names_the_missing_step` 靠它在组合真的能建的那天自己退出。

计划：`node scripts/test.js test/plans/feat/compile/transforms.plan.toml`
→ `[SUMMARY] total: 19, pass: 17, fail: 0, skip: 2`（两个 skip 都在等 M3 转置 `Call`）。

## 落地那天跟着动的地方

1. `vjp_api.names_the_missing_step` 自己跳过（`--missing` 变 1）；它今天钉的是
   `stdlib/vjp.cml:51:` / `transpose has no transpose for this function` / `TRANSPOSE function=split_of_jvp_of_f_backward skip=call`，
   那三句随之作废，不用谁去删。
2. `vjp_api.analytic_gradient` 自动开始跑并断言 `12.0` / `9.0`；`have-vjp.mjs` 门后的
   `vjp_gradient.cml` 也已改成 JAX 形状（27.0 三个数不变）。
3. `scripts/stdlib-reference.mjs` 里 `vjp` 的 `pending:` 一行删掉再 `--write`，参考页就收进这个模块；
   `--verify-docs` 在它编得过的当天会自己报"该删这一行了"。

## 这支分支今天为什么也是绿的

组装没让任何既有断言失效：拒绝那一读仍然停在库自己那一行（只是行号与措辞跟着组装走），
两个数仍然由门挡着。所以它可以随时并进 `develop`；两个数在 `transpose` 能转置那次调用之前不会出现，
也不会假装出现。