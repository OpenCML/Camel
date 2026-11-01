# M4 组装：库层 `vjp` 拼好了，作者的三条读数由程序自己给出

对象：`stdlib/vjp.cml`，分支 `lane-m4-assembly`（含 `lane-m4`、`origin/develop`、`split` 改成残差图定那笔）。
判据（已满足）：**一个只 `import { grad, vjp } from vjp` 的程序**给出 `18.0` / `(12.0, 9.0)` 与 `27.0`。

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
与"pass 是前置条件就诊断它"同一口径。所以调用它的四个程序**只 import 库**。

## 两条用例，各钉一行 import

程序源码分辨不出这两行（调用者一行都不 import），所以只能做 A/B：
`test/tools/check-vjp-ruleset.mjs` 把**其中一行**在"库的副本"里注释掉、`CAMEL_STD_LIB` 指过去，
同一个程序跑两遍——拿掉时必须在**库自己那一行**停住并说出缺什么，放回时三个读数都在。

| 用例 | 拿掉 | 读数 |
| --- | --- | --- |
| `vjp_api.the_jvp_import_is_load_bearing` | `rulesets.jvp` | `jvp has no forward rule for ':op/mul_d'`，`note: raised here` → `stdlib/vjp.cml:49:35` |
| `vjp_api.the_transpose_import_is_load_bearing` | `rulesets.transpose` | `TRANSPOSE … skip=rule uri=:op/add_d reason=no-such-family` → `transpose has no transpose for this function` @ `stdlib/vjp.cml:56:30` |

两条都验过反向：把该行从**装机后的库**里去掉，对应用例红（`the library does not import …，so there
is nothing to drop`），放回去绿。工具里也写了原因：跑在计划里时 runner 给子进程
`CAMEL_ERROR_FORMAT=short`，那一种格式只有一行消息、没有 `note:` 和 `-->`，所以工具自己按名字要
`text`（rich）那一种——它读的是那两行位置，不改走图判了什么。

## 今天的读数（可复现）

```
$ node scripts/test.js test/plans/feat/compile/transforms.plan.toml
[PASS] vjp_api.carries_its_own_ruleset.nvm        27.0 / 27.0 / 27.0
[PASS] vjp_api.analytic_gradient.nvm             f(3, 2) = 18.0, library y = 18.0,
                                                  library/analytic df/dx = 12.0, df/dy = 9.0
[PASS] vjp_api.the_jvp_import_is_load_bearing
[PASS] vjp_api.the_transpose_import_is_load_bearing
[SUMMARY] total: 21, pass: 21, fail: 0
```

最后一步（转置"多参数被调函数"）由 `origin/develop` 的 `047f30f0b` 带进来；
`probe_library_vjp.cml` 读 `39.0`（= 18 + 12 + 9）。

## 参考页：`pending` 已退场

`vjp` 跑了得起来，所以 `scripts/stdlib-reference.mjs` 里那句 `pending:` 删掉了，`--write` 重生成：

- `docs/stdlib-reference.md`：**519 条**（`--verify-doc` 说 up to date）；
- `--audit`：**468 / 635**，37 条模块、8 个未读文件（与原表一致）；
- `--verify-docs`：416 个示例编译、**85** 个标 fragment（原 81；多出来的是 `vjp` 的四个 `@example`，
  它们引用未声明的 `f`，本来就是 fragment）。

## 还留着的两条旧账（不在本分支）

1. `Vjp<F>`/`Split<F>`/`ParamsOf<F>` 的泛型声明（设计 6.1）：**参数类型这一半已落**——`vjp`（一元、二元）与
   `grad` 现在写成 `::<T>`（`grad::<T>(f: (x: T) => float): (x: T) => T`），所以数组/张量/`f32` 的函数能走到库里；
   仍然"每 arity 一条"，因为函数体是手工拆参数的，而 `jvp` 只收 `float` 参数
   （`modules/transforms/jvp_operator.cpp:115`），非 float 的 `T` 在这一行被拒。
2. 残差位那个"类型由编译器填"的问题（`questions-2026-11-01-the-residual-slot-has-no-type-until-execution.md`，
   甲/乙 两案已报价）：今天不影响任何读数。