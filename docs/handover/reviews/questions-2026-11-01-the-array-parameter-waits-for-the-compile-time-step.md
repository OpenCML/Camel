# 数组参数不是 `vjp` 的事，是"编译期实例化"那一刀的事

**状态**：待裁（2026-11-01，`c2181c95f`，原 `lane-m4-assembly`，该分支已删）

**问句**：`vjp` 走到 `float[]` 参数时（127 例里 54 例是这一类），缺的到底是哪一步？要不要在 `vjp` 这一层补？

**答（今天量到的）**：不是 `vjp` 的事。缺的三步全在 `jvp` 里，而它们正是作者 14:30 裁定说的"**三个算子在编译期实例化**"那一刀要动的地方——那一刀落下之后，这三步自然一起解决；在那之前，`vjp` 无论怎么写都走不过去。

## 今天 `vjp` 对数组参数的读数

```
error[SemanticDiag::ArgumentsRejected]: Invalid call to 'jvp' with argument types <>
((x: float[]) => float): `jvp` lifts a `float` parameter into the pair `(value, tangent)`, and
parameter 0 is a `float[]`, whose tangent space is self and which this transform does not lift yet.
   --> out/latest/stdlib/vjp.cml:49:38
```

`jvp` 的尺子回答 "self"（`float[]` 的切空间就是它自己，`tangentSpaceOf` 说得很清楚），所以**类型上没有缺口**；缺的是提升的写法。

## 三步，逐步带证据

**第一步：`jvpSignature` 把 `float` 写死了。**
`modules/transforms/jvp.cpp:363` 要求结果必须是 `float`；`:366` 把提升对建成 `(float, float)`；`:373` 要求每个形参是 `float`；`:376-377` 每个形参建两遍 `float`；`:785` 用 `jvpSignature` 提升**被调者**，被调者只要带一个数组形参就返回 `nullptr`。
证据：`jvp(f)`（`f: (float[]) => float`）在编译期被拒（上面那段原文）；`f` 体内把数组传给一个调用（`use(ys)`）时，运行期既没有 `skip=` 读数也没有具名拒绝，只有 `jvp has no forward transform for this function`——说明它在提升签名那一步就停了，还没走到走图的循环。

**第二步：数组值在 forward walk 里拿不到切向。**
`modules/transforms/jvp.cpp:1034`：`if (!complete) { os << "REINTERP jvp function=" << name << " skip=body\n"; return fail(refusal); }`。
证据：`func f(x: float): float { let ys: float[] = [x, 2.0]; return ys[0] + ys[1] }` 的 `jvp` 打印 `REINTERP jvp function=f skip=body` 并以 `jvp has no forward transform for this function` 退出；把数组换成外部捕获的数组（`xs[0]*x + xs[1]`）是同一句。也就是说：一个数组类型的值在走图时没有切向可传——先要"数组的切向是数组"这件事在走图里被写下来。

**第三步：`:op/idx_arr` 一条规则都没有。**
`git grep -c idx_arr` 在 `stdlib/rulesets/jvp.cml`、`stdlib/rulesets/transpose.cml`、`stdlib/autodiff.cml` 三个文件里**都是 0 命中**。`xs[i]` 编译成 `:op/idx_arr`（`std::gir` 的读数），所以即便前两步通了，前向规则（`d(xs[i]) = dxs[i]`）与反向规则（把 `ct` 散到 `i` 位上、其余为零）都还缺。
规则挂载点在算子声明处（`src/builtin/modules/globals.cpp:2027` 声明 `:op/idx_arr`）；旧引擎里有等价物可参照：`src/builtin/operators/derivative.cpp:402`（gather）、`:531`（批规则）。

## 结论

1. 这三步**不是数组自己的事**，是"编译期在实参函数的图上跑变换、从产出的图读结果类型"这一刀的前置条件：提升签名、走图的切向、每个算子的规则，三件都在那一刀里一次做完。
2. 所以它归**编译期实例化**那条车道，不归 `vjp`。`vjp` 只是第一个撞上的调用者（54 例），它今天那句点名拒绝（参数序号 + 尺子的答案）已经是这一层能给的最好答复。
3. 今天堵着谁：迁移表里 54 例数组参数；在那一刀落下之前，它们的正确状态就是这句具名拒绝，不是静默错值。