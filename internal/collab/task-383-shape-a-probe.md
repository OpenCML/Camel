# task-383 附录：形状 A 的观测对象（`t207_loop_grad_export.cml`）

这份程序是跑出 `states=3 scans=8 components=11` 三行读数的**观察对象**。原文件已按"提交前还原"
清掉（`test/results/log` 在远端，本地无副本），此处按当时结构**逐字重建**：行号未必与当时原文件
逐行重合（当时诊断里的 `:29:21` / `40:37` 来自原文件），但**结构与三个关键写法逐字一致**。

## ① 全文
```cml
/**
 * Shape A (task-207 reading): the gradient of a collecting loop, exported from a `main` whose
 * data and weights are parameters. This is the observation object behind
 * `states=3 scans=8 components=11` / `fromScan=[S0,-,-]` / `no lowering ':struct/append_arr'`.
 * Probe only: not a test case, not committed to the tree.
 */
import { with_grad } from autodiff
import { reshape, slice, sum } from tensor

type P = { w: float[4] }

func rowAt(xs: float[4, 4], j: int) => float[4] {
    return reshape<[4]>(slice(xs, 0, j, j + 1))
}

func lossAt(w: float[4], x: float[4]) => float {
    let e = sum(w * x)
    return e * e
}

func forward(w: float[4], xs: float[4, 4]) => (float[], int, float) {
    return loop acc = [] as float[], j = 0 as int, total = 0.0 while j < 4 do {
        let l = lossAt(w, rowAt(xs, j))
        (acc->append<l>, j + 1, total + l)
    }
}

with <m: P>
func loss(xs: float[4, 4]) => float {
    let (residuals, rounds, total) = forward(m.w, xs)
    return total
}

func main(w: float[4], xs: float[4, 4]) => float[4] {
    let (l, g) = with_grad(loss)<m: { w: w }>(xs)
    return g.w
}
```

## ② 必须复现的三处结构（少一处就不是同一个对象）

1. `forward` 返回 **`(float[], int, float)`** —— 收集器 + 两个标量状态，正是 `states=3` 的来源；
2. `loss` 是 **`with <m: P>`**，`with_grad` 的 **`<m: { w: w }>` 按名绑定** —— 这也是"只填位置名"会
   打坏调用（`'m' is not a 'with' parameter of '<arg0: P> …'`）的那处；
3. `main(w, xs)` 返回 **`g.w`**（梯度的 `w` 字段）。

> 若跑出来的 `scans` 不是 8：按这三条核对构造，**不要重新猜数**。

## ③ 命令行与计划条目（遍形式，不是程序内 `export_model<`）
```
camel test/cases/probe/t207_loop_grad_export.cml std::macro \
  'onnx::export_model out="<tmp>/t207_a.onnx"' std::nop
```
计划条目原文（`test/plans/probe/t207_tape_collection.plan.toml`）：
```toml
kind = "behavior"
case = "../../cases/probe/t207_loop_grad_export.cml"
args = ["${case}", "std::macro", 'onnx::export_model out="${test_tmp}/t207_a.onnx"', "std::nop"]
expect_exit = 0
timeout_ms = 60000
```

## ④ 三行 stderr 原文（插桩在 `emitLoopScanning` 内，形状 A）
```
[t207] loop states=3 scans=8 components=11 fromScan=[S0,-,-]
[t207] emitLoopScanning entry scans=8 carried=2
[t207] no lowering uri=':struct/append_arr' loop(states=3 scans=8 components=11)
```

## ⑤ 前置：先过 `argNameAt`

走到这三行之前，程序必须在 `616f65b436` 之后（"a built signature carries names"）——在那之前它在
gir builder 就以 `func.cpp(365) FunctionType::argNameAt / hasNames_` 中止，根本到不了导出器。
⇒ 接手方**从合并队列之后的尖端起跑**。