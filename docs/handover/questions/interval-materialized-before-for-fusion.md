# 区间今天先物化成 `int[]`，`for i in 0..n` 的融合还没做

## 事实（H248 组 1 切片 A，2026-10-24）
- 裁定：`a..b` 是左闭右开的整数区间，类型 `int[]`，与 `range(a, b)` 同义；`range(a, b)` 要删。
- 实际落地：`a..b` 在 GIR 里被换写成一次 `$builtin::range` 调用（`src/compile/gir/builder_operators.cpp` 的 `Builder::intervalValue`），
  也就是**先物化成一个数组值**。`test/cases/compile/stream/interval.cml` 记录的正是这个可观察结果（`1..5` 打印 `[1, 2, 3, 4]`）。
- 因此 `for i in 0..n` 今天会先建出 `n` 个整数的数组再迭代；裁定要求的"在 `for` 里融合、不物化"没有实现。
- `range(a, b)` 也还没有删除（它是既有内建，删除面很宽：`stdlib/**`、`test/**`、文档与示例都在用，属于另一条车道）。

## 为什么切片 A 这样停下
- 融合不是本切片能局部做的事：它要么在 GIR 里认出"区间直接作为 `for` 的可迭代对象"这一形状，要么在 `for` 的降级里特判，
  两者都要动 `for` 的编译路径，而组 1 的裁定只覆盖 `..` 自身的两个读法与 `T..` 的类型身份。
- 先把可观察语义（类型 `int[]`、左闭右开、只收整数）钉死在测试里，融合就是之后一次纯性能/表示层的改动，不会改程序看得到的结果。

## 请作者裁定
1. `for i in 0..n` 的融合是**要求语义上不可观察**（只是不物化），还是允许在元素值/副作用上有所不同？
2. `range(a, b)` 的删除是否单独派一条车道（我这边不动 `stdlib/**` 的既有调用点），还是由融合那条车道一并处理？
3. 区间元素超过 `int64` 可表示的个数时（例如 `0..(1<<62)`），是编译期诊断还是运行期失败？今天走 `$builtin::range`，行为由它决定。

## 出处
- 裁定：`docs/design/io-concurrency-streams/plan.cn.md`（只读）
- 实现：`src/compile/gir/builder_operators.cpp`（`intervalValue`）、`src/compile/gct/builder.cpp`（`ReservedDataOp::Comp`）
- 用例：`test/cases/compile/stream/interval.cml`、`test/plans/feat/compile/stream.plan.toml`