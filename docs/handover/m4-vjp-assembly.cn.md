# M4 组装：库层 `vjp` 自己带上它组合的规则集，落地只差转置"多参数被调函数"那一步

对象：`stdlib/vjp.cml`，分支 `lane-m4-assembly`（含 `lane-m4`、`origin/develop`、`split` 改成残差图定那笔）。
判据：组装"完成"当且仅当 **一个只 `import { vjp }` 的程序**能拿到 `18.0` / `(12.0, 9.0)` 与 `grad` 的 `27.0`。

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

## 库带上它组合的规则集

`vjp` 是 `transpose(split(reinterp<jvp>(f)))`，所以它要 `rulesets.jvp`（正向规则）和
`rulesets.transpose`（反向走图时问的那些规则）。两行都写在 `stdlib/vjp.cml` 顶部：

```camel
import rulesets.jvp
import rulesets.transpose
```

理由只有一句：**一个库要能自己兑现它对外承诺的调用，否则每个调用者都得知道它的内部组成**——
与"pass 是前置条件就诊断它"同一口径。所以 `probe_library_vjp.cml`、`vjp_analytic_gradient.cml`、
`vjp_gradient.cml`、`vjp_carries_its_ruleset.cml` 四个程序**只 import 库**，不再每人抄一行规则集。

A/B（都在 `a05678523` + 本分支上量）：

| 拿掉 | 读数 |
| --- | --- |
| `import rulesets.transpose` | `TRANSPOSE function=autodiff::mul_tangent<float> skip=rule uri=:op/add_d reason=no-such-family`，然后 `transforms: transpose has no transpose for this function`，落在 `stdlib/vjp.cml:68:32` |
| `import rulesets.jvp` | `transforms: jvp has no forward rule for ':op/mul_d' (no ruleset module is loaded (import rulesets.jvp))`，`note: raised here` 指向 `stdlib/vjp.cml:47:35` |

两条都停在**库自己那一行**，不在调用者的文件里。

## 今天的读数（可复现）

```
$ node scripts/test.js test/plans/feat/compile/transforms.plan.toml
[SKIP] vjp_api.carries_its_own_ruleset.nvm    TRANSPOSE function=autodiff::mul_tangent<float> skip=signature
[SKIP] vjp_api.analytic_gradient.nvm          （同一条读数）
[SKIP] vjp_api.gradient_when_the_steps_land.nvm
[SUMMARY] total: 19, pass: 16, fail: 0, skip: 3
```

墙往下一层挪了：不再是"`skip=call`（backward 半里有 `Call`）"，而是**那个 `Call` 的被调函数有四个参数**。
`split_of_jvp_of_f_backward` 里调 `mul_tangent(x, w, dx, dw)`（`stdlib/autodiff.cml:102`），
`modules/transforms/transpose.cpp` 的 `transposeSignature`（`:29`，判据在 `:48-59`）只认"第一个参数是
残差元组"的那种签名，四个 `float` 的被调函数拿不到转置签名，于是 `:833` 报 `skip=signature`。

这一笔**不在 `develop`（`a05678523`）里**，在 `lane-reinterp2` 的 `5b8333c53`
（`transforms: transpose a call to a function of several parameters`，只改
`modules/transforms/transpose.cpp` 一个文件）。把那个文件放进树里量到的是：

```
$ vjp_carries_its_ruleset.cml   → 27.0 / 27.0 / 27.0
$ vjp_analytic_gradient.cml     → f(3, 2) = 18.0, library y = 18.0,
                                  library/analytic df/dx = 12.0, library/analytic df/dy = 9.0
$ probe_library_vjp.cml         → 39.0（= 18 + 12 + 9）
[SUMMARY] total: 19, pass: 19, fail: 0
```

量完就把那个文件还回去了：它不是这支分支的活，也没并进 `lane-m4-assembly`。

## 落地那天跟着动的地方

1. 三个 `skip` 自己变成 PASS（门 `test/tools/have-library-vjp.mjs` 从"组合跑不起来"变成"跑起来并印数"）
   ——`vjp_api.carries_its_own_ruleset`、`vjp_api.analytic_gradient`、`vjp_api.gradient_when_the_steps_land`。
   没有谁要去改 `expect_*`：断言早就写好了。
2. `scripts/stdlib-reference.mjs` 里 `vjp` 的 `pending:` 一行删掉再 `--write`，参考页就收进这个模块；
   `--verify-docs` 在它跑得起来的当天会自己报"该删这一行了"。
3. `docs/handover/m4-vjp-assembly.cn.md`（本文）与 `test/tools/have-library-vjp.mjs` 的 stage 措辞
   跟着 `skip=signature` 那一层退场。

## 这支分支为什么现在是绿的

组装没让任何既有断言失效：拒绝形态的用例换成了正向数值用例（它守的是"库自带规则集"，不是"缺一步"），
三个数由门挡着仍不出现。所以它可以随时并进 `develop`；三条读数在 `5b8333c53` 落地之前不会出现，
也不会假装出现。

```
$ node scripts/diagnostics-catalog.mjs --check     # 退出 0
$ git grep -n '^<<<<<<< '                          # 空
```