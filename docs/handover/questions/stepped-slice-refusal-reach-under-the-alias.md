# 别名之下，步长 ≠ 1 的切片拒绝到哪一层（`float[4]` 拒绝、`float[]` 仍按拷贝走）

Author: `handover/h460-bounds` 车道（读数实测于 `7cf12fd05` = `handover/h443-flip-survey` + `develop`，2026-10-25）
Status: open

## 一句话

决策者裁定"数组下标丢了静态越界检查是回归，6 条一条不落、修代码不改用例"之后，`subscript/slice_step`
与 `subscript/slice_step_negative` 只能在**类型写定了第一维 extent 的数组**上拒绝（`float[4]`），因为
**extent 不写进类型的数组**（`int[]`、`float[]`）上，步长 ≠ 1 的下标切片是既有绿色用例钉死的行为
（H17 的"先当拷贝"偏差）。这条"按第一维 extent 是否常量"划界不是设计原则直接给的，需要作者一句裁定。

## 逐字读数

判定点：`src/builtin/modules/globals.cpp` 的 `staticSliceRejection`——先看 `staticFirstExtent(norm[0])`，
为 `nullopt`（`T[]` 的一个未知 extent、开放秩 `T[..]`、以及非数组）时**直接返回**，步长检查在其后：

```cpp
    const std::optional<int64_t> extent = staticFirstExtent(norm[0]);
    if (!extent) {
        return std::nullopt; // `T[]`: the length is not in the type, and the kernel reads it
    }
    if (const std::optional<int64_t> step = constantArg(statics, 3); !step || *step != 1) { ... }
```

- 红前（`handover/h443-flip-survey` + develop，两者 `expected exit 1, got 0`，编译通过、跑完）：
  - `subscript/slice_step`：`[stdout] ran` / `[stdout] [2]` / `[stderr] (empty)`
  - `subscript/slice_step_negative`：`[stdout] ran` / `[stdout] [3]` / `[stderr] (empty)`
- 红后（本车道，`SemanticDiag::ArgumentsRejected`，码 `0x0300001C`）：
  - `subscript/slice_step`：`.../subscript/slice_step.cml:11:20: [Error]: Invalid call to '__slice__' with
    argument types <> (float[4], int, int, int, int): a slice step other than the constant 1 is a view with
    strides, which needs the multidimensional array representation of H17.`
  - `subscript/slice_step_negative`：`.../subscript/slice_step_negative.cml:13:20: [Error]: Invalid call to
    '__slice__' with argument types <> (float[4], int, int, int, int): a slice step other than the constant 1
    is a view with strides, ...`
  - 两处 `b` 的静态类型实测是 `float[4]`（`reshape([1.0,2.0,3.0,4.0],[4])`，车道的探针用例逐字报
    `This value has type 'float[4]'`），**不是** `Tensor`（numeric-unknown）：别名之下两者已同型，
    按 `isTensorType` 分不开。
- 保持绿色、要求"步长 ≠ 1 的下标切片能跑"的用例（本车道**没有**动它们）：
  - `feat.linear.subscript` 的 `subscript.step`（4 个后端 nvm/fvm/jit/prl，`expect_exit = 0`）：
    `test/cases/linear/faults/subscript_step.cml:17` 的 `println(xs[:: -1])`，`xs: int[]`；
    `expect_stdout_exact = "[1, 3, 5]\n[5, 3]\n[5, 4, 3, 2, 1]\n"`。
  - `feat.modules.autodiff` 的 `array_slice_grad.runtime.nvm` / `.macro.fvm`（绿）：
    `test/cases/modules/autodiff/array_slice_grad.cml:41` 的 `xs[4:1:-1]`、`:47` 的 `xs[0:5:2]`，`xs: float[]`。
- 内核一侧的注释仍是**无条件**的拷贝偏差：`src/builtin/operators/struct.cpp:240-243`
  "A step other than 1 is a view with strides, which needs the multidimensional array representation of H17.
  Until it lands, a stepped slice is a copy ... H17 records the deviation"。

## 相关裁定与设计原则（供作者定位）

- `docs/handover/reviews/questions-2026-10-24-evening.md` §h450-spelling-and-print-form-leftovers 第 3 条
  （本车道的任务依据）：常量下标 + 已知 extent ⇒ 编译期 `ArgumentsRejected`，与元组的
  `StaticAccessIndexOutOfRange` 同一条规则覆盖数组。
- 设计原则 3.9「越界」：**编译期能知道长度时（尤其是固定长度的类型）**，`slice` 与下标的越界应当是编译期错误；
  运行期的 `slice` 越界是故障。
- 设计原则 5.4「下标与切片」：`t[a:b:s]` 可带步长，**切片是共用数据块的视图**。

## 需要作者回答的两句

1. "类型写定第一维 extent ⇒ 该数组的切片必须是视图（步长只能是常量 1）；extent 不在类型里 ⇒ 仍按 H17 的
   拷贝偏差接受任意步长"——这个划界**是否就是意图**？若是，请落一句（本车道即按此收尾）；
   若不是，请指出应当以什么为判据（今天的候选只有"声明的拼法"一类，而 `reshape` 的返回值实测是 `float[4]`，
   与 `float[]` 只在 extent 是否写上不同）。
2. 若裁定"拒绝应当覆盖所有数组"，则 `feat.linear.subscript` 的 `subscript.step` 与
   `feat.modules.autodiff` 的 `array_slice_grad` 的**行为期望要改**（改用例是决策者的事，本车道不动）；
   若裁定"extent 不在类型里也拒绝"，请一并说清 H17 落地前 `src/builtin/operators/struct.cpp` 的拷贝路径
   是否整个删除。

## 同族但不归本车道：`bounds/unknown_runtime` 的运行期消息

`test/plans/feat/modules/tensor/bounds.plan.toml:66-75` 的 `unknown_runtime` 在本车道**前后逐字未变**
（两侧同一条 FAIL，属既有红集合）：

- 期望：`idx: tensor index out of bounds`（张量模块 `idxKernel` 的消息）。
- 实际：`(unknown_runtime)/.../unknown_runtime.cml:7:14: [Error]: Index 8 is out of range for an array of
  length 3. Ensure the index is within the valid range of the collection. (name=RuntimeDiag::IndexOutOfRange)`，
  即运行期跑的是**核心**的 `:op/idx_arr` 内核（`src/builtin/operators/ops.cpp:793`）。
- 该值由 `load_npy` 得到，类型没有 extent（numeric-unknown），所以运行期由内核读长度——这与上一条问题
  是同一个"别名之下谁拥有下标语义"的另一半。**建议归 `handover/h459-flip-kernels`（内核/消息族）**
  或请作者一并裁定：未定形数组的下标，运行期消息按数组还是按张量说。

## 同族但不归本车道：`let x: T[N]` 的 N 不进值的类型，静态检查因此看不到它

本车道的探针实测（临时用例，已删；主机 `out/latest/bin/camel`）：

- `func main() { let xs: int[4] = [1, 2, 3, 4]; let s: string = xs }` 逐字报
  `[Error]: This value has type 'int[]', which is not the declared type 'string'.`——**值的类型是 `int[]`**，
  标注里写下的 4 没有留下。
- 后果（同一探针）：`xs[5:]` 不报编译期错，跑到运行期才 `RuntimeDiag::SliceOutOfRange`：
  `h460_probe_open_start.cml:5:15: [Error]: The range 5 to 4 is out of range for an array of length 4.`；
  同一个越界写成参数（`func at(xs: int[3], i: int): int`）时，本车道的检查**会**在
  `2:45` 报 `ArgumentsRejected`（`0x0300001C`，`index 5 is outside axis 0 of extent 3`）。
- 也就是说：静态检查的覆盖面 = "类型里真写着 extent 的地方"（形参类型、`reshape`/`ones` 这类结果、
  写了形状的返回类型），**不含** `let x: T[N] = ...` 这个绑定。
- 没有测试钉住这一条（`test/cases/compile/array_alias/bare_tensor.cml:12` 的 `let ints: int[3] = [1, 2, 3]`
  只把它交给 `Tensor` 形参，不看它自己的类型）。设计原则 5.5/5.4 的"形状不自动放宽、需要时写类型标注"
  读起来是"标注写了就该留下"。**请作者裁定这是不是缺陷；若是，归"声明类型"一族，不是本车道。**