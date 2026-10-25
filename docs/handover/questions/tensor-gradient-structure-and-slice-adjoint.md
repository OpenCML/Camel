# 梯度结构体里的离散字段，与下标切片的伴随交回的值

车道：`handover/h563-tensor-migrate-c`（族 = autodiff/nn 的删拼写迁移）。删拼写之后，autodiff 用例里除了 5 条 torch 越界红，剩下的红只有这两簇：它们互不相关，只是同一次迁移撞上的，合装一页。

## 零、两句话

1. **离散字段**：`labels: i64[..]` 写进结构体之后，`with_grad` 交回的梯度结构体里**没有 `labels` 字段**，而三条用例断言的正是"该字段的梯度是零"（读 `g.labels` 再求和等于 0.0）——是物化一个零字段，还是保持缺字段（读它就是编译错误、由"没有这个字段"表达那个零）？
2. **下标切片的伴随**：`t[a:b]` 的伴随（`tensor:slice_grad`）交回的运行期值**没有内核读得了**（`sum`/`subtract`/`multiply` 都报 `Unsupported Tensor dtype`），而同一形状用函数形式 `slice(t, axis, start, end)` 写出来、伴随逐位正确——是给下标形式的伴随补内核/定型，还是在语言层按名字拒绝它？

## 一、证据（逐字）

### 1.1 离散字段

用例（`test/cases/modules/autodiff/gather_discrete_index_zero.cml:19-22,34-36`）：

```
type Model = {
    weights: f32[..],
    labels: i64[..], // the integer index array
}
...
    let (v, g) = with_grad(gather_loss)<model>()
    let weights_grad = sum(g.weights)
    let index_grad = sum(cast(g.labels, 'float32'))
```

读数（`test/plans/feat/modules/autodiff.plan.toml` 的 `gather_discrete_index_zero.runtime.nvm`，逐字）：

```
expected "gather_discrete_index_zero value=1.0 weights_grad=6.0 index_grad=0.0\n", got
"(gather_discrete_index_zero)/.../gather_discrete_index_zero.cml:36:31: [Error]: Invalid access index: 'labels'.
 Ensure the index is within bounds for arrays or refers to a valid field in structs.
 (name=SemanticDiag::InvalidAccessIndex, code=0x0300000D)\n"
```

`discrete_zero.cml:81:31`（`let label_grad = sum(cast(g1.labels, 'float32'))`）与 `discrete_zero.cml:87:31`（`let index_grad = sum(cast(g5.labels, 'float32'))`）是同一条诊断，同一列号。

已经通过的先例把"不读它"写成了既有做法（`test/cases/modules/tensor/dtype/gather_grad_f64.cml:30-33,40-45`，其 plan 条目 `gather_grad_f64.nvm`、`.fvm` 全绿）：

```
with <m: M>
func gather_loss(): float {
    return sum(m.w[m.labels])
}
...
    let (v2, g2) = with_grad(gather_loss)<model>()
    let row0 = sum(take(g2.w, cast([0], 'int64')))
    let row1 = sum(take(g2.w, cast([1], 'int64')))
    'gather={:.1f} grad_sum={:.1f} row0={:.1f} row1={:.1f}'->format<v2, sum(g2.w), row0, row1>->println
```

——正向读 `m.labels` 完全正常（先例自己也这么做），梯度侧只读 `g2.w`；`g2.labels` 没有任何写法可用。

### 1.2 下标切片的伴随

用例（`test/cases/modules/autodiff/slice_grad.cml:30-34,67-69`、`slice_open_end.cml:19-29`）：

```
with <x: f32[..]>
func slice_loss(): float {
    let c = reshape([2.0, 3.0, 5.0] as f32[], [3])
    return sum(x[1:4] * c)
}
...
func distance(g: f32[..], expected: f32[..]): float {
    return sum(abs(g - expected))
}
```

读数（同一 plan，逐字）：

```
slice_grad/.../slice_grad.cml:68:22: [Error]: An error has occurred: subtract: Unsupported Tensor dtype.
  Check the runtime environment and input data for unexpected issues. (name=RuntimeDiag::RuntimeError, code=0x04000000)
slice_open_end/.../slice_open_end.cml:29:68: [Error]: An error has occurred: multiply: Unsupported Tensor dtype.
```

对照探针（本车道在一个程序里写三种写法：正向 `x[1:4]`、函数形式的伴随、下标形式的伴随；同一输入 `x = [1,2,3,4,5,6]`、同一形状、同一手写伴随 `[0,2,3,5,0,0]`。探针用例按约定已删，读数逐字留在这里）：

```
forward x[1:4]=33.0
function form grad sum=10.0 elements=[0.0, 2.0, 3.0, 5.0, 0.0, 0.0]
.../h563p17.cml:30:62: [Error]: An error has occurred: sum: Unsupported Tensor dtype.
  Check the runtime environment and input data for unexpected issues. (name=RuntimeDiag::RuntimeError, code=0x04000000)
```

树内对照（同一份 `test/cases` 上的 gate 日志，逐字）：

```
[PASS] tensor_grad_check.macro.nvm / .macro.fvm / .macro.jit / .runtime.fvm / .runtime.jit / .simplify.fvm / .simplify.jit / .simplified.nvm
       （用例里 `slice(x, 1, 1, 4)` 的伴随与手写伴随逐位比较，8 条全绿）
[PASS] slice_view   [PASS] slice_const_bounds   [PASS] slice_rank2
       （正向 b[1:3]、b[2:]、b[0:4]、b[3:1] 等）
```

两条伴随规则的实现都在 `modules/tensor/ops/layout_ops.cpp`：函数形式 `sliceVjp`（:737-746）、下标形式 `sliceSubVjp`（:755-784），两者都写 `addTensorOperFor(..., "tensor:slice_grad", ...)`，差别在操作数（函数形式把 `axis` 透传，下标形式送常量 0，以及由 `written` 掩码推出的 `end`，末写端时 `end` 是 `tensor:dim` 的结果）。运行期一个可读、一个读不了；**哪一处让元素类型丢了，本车道只测到现象，没有定位到那一行**。

## 二、选项与代价

### 2.1 离散字段

- **(a) 物化成零字段**：梯度结构体照旧带 `labels`，其值为零。代价：要给"没有切空间的值"定义一个零——元素类型取什么（照抄 `i64` 还是写到某个数值宽度）、形状是否照抄正向；每个梯度的结构体都要为离散字段多带一个成员，等于在类型里给"离散值不可微"开口子。用例三处一行不改即绿（`discrete_zero.cml:81,87`、`gather_discrete_index_zero.cml:36`）。
- **(b) 保持缺字段**：今天就是。代价：这三条用例断言的正是"零字段存在"，只能改写断言——例如把 `InvalidAccessIndex` 本身写成期望（读 `g.labels` 是编译错误，"零"由"没有这个字段"表达），或者只读 `g.weights` 而放弃离散侧的那三个数；两种都改了用例的语义。今天的诊断只说"索引无效"，没有点名"离散字段没有梯度"这条规则，读的人要靠猜。
- **(c) 缺字段 + 换诊断**：(b) 的加强版：读 `g.labels` 仍然拒绝，但诊断点名规则。代价：(b) 的用例改写照旧，另加一条诊断文案与 `scripts/diagnostics-catalog.mjs` 的登记。

### 2.2 下标切片的伴随

- **(a) 补内核/定型**：让 `t[a:b]` 的伴随与函数形式同宽、可读。代价：动 `modules/tensor`（`layout_ops.cpp` 的 `sliceSubVjp`，或 `slice_grad` 自己的定型），影响面是**所有**下标切片的梯度而不只是这两条用例；用例一行不改即绿。
- **(b) 在语言层拒绝**：按名字拒绝 `t[a:b]` 的微分（`grad`/`with_grad` 对该形状报规则）。代价：一个今天能编译的写法变成拒绝，正向切片（`slice_view` 等）不受影响；两条用例要改写成"拒绝"用例（`expect_exit = 1` + 诊断断言），或改用函数形式。
- **(c) 其它**：例如把下标形式的伴随在构建梯度时就落成函数形式的节点组合（`tensor:slice` 与 `tensor:slice_grad` 拼），或维持现状并把这两条记为已知缺陷（`known_fail` 或等价登记）。

## 三、卡住什么

| 声明的 plan 条目 | 红条数 | 卡在哪 |
| --- | --- | --- |
| `discrete_zero`（跑出 `.nvm`、`.fvm` 两条） | 2 | 2.1 |
| `discrete_zero.macro.nvm` | 1 | 2.1 |
| `gather_discrete_index_zero.runtime.nvm` | 1 | 2.1 |
| `gather_discrete_index_zero.macro.fvm` | 1 | 2.1 |
| `slice_grad.runtime.nvm` | 1 | 2.2 |
| `slice_grad.macro.fvm` | 1 | 2.2 |
| `slice_open_end.runtime.nvm` | 1 | 2.2 |
| `slice_open_end.macro.fvm` | 1 | 2.2 |

合计 9 条红 / 8 个声明条目，全在 `test/plans/feat/modules/autodiff.plan.toml`。两条离散用例带 `desc` 的其余条目都是绿（它们只读 `g.weights`），所以这一簇的改写面就是上表这几条。这 9 条之外，autodiff 族只剩 5 条 torch 越界红（`benchmark_gradients_vs_torch`、`resnet_gradient_smooth_vs_torch64`、`resnetbn_gradient_smooth_vs_torch64`、`resnet_gradient_relu_kinks_vs_torch64`、`resnetbn_gradient_relu_kinks_vs_torch64`），与本页无关。