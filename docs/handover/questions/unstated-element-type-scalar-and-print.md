# 未述元素类型的张量：取出的标量是什么，以及数组打印走哪条路（实测，2026-10-24，h419 lane）

本文件只报两件**实测**到的事，两件都落在「声明的元素类型未述」（裸 `Tensor`，即
`ArrayType::numericUnknown()`，design principles 5.4「元素类型未述的数值数组」）这一条规则上。
两件都需要作者裁定，所以 h419 一片只做了不受影响的部分（整数列实参按值自己的 dtype 读，
见 `docs/handover/reports/tensor-flip-integration.md` 的 h419 一节）。**本 lane 没有自己发明语义。**

## 一、`t[0]` 对裸 `Tensor` 取出 `void`，于是 `x[0] + 2.6`、`t[0] > 0.5` 找不到重载

实测（`camel <case>`，本树 `handover/h419-flip-next`）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (void, float); operator '+' is not defined for these operand types.
   --> test/cases/opt/generic/arms_laid_out_whole_tensor.cml:23:24
 23 |     let n = int(x[0] + 2.6)
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (void, float); operator '>' is not defined for these operand types.
   --> test/cases/trans/annotated/boundaries.cml:13:22
 13 |     return if t[0] > 0.5 then relu(t) else t * (t[1] * 2.0)
```

入口是 `modules/tensor/ops/layout_ops.cpp` 的 `takeInfer`（下标 `t[0]` 的推断）：

```cpp
    if (input.shape) {
        out.insert(out.end(), input.shape->begin() + 1, input.shape->end());
    } else {
        return tensorOf(input.dtype, std::nullopt);   // dtype 未述 -> tensorOf 走 elemType=Void
    }
    return tensorOf(input.dtype, out);
```

`tensorOf` 里 `!facts.dtype` 的分支是 `ArrayType::numericUnknownFromForms(forms)` /
`ArrayType::numericUnknown()`；形状耗尽（秩 0）时 `forms.empty()`，于是结果类型就是
**元素类型本身** = `Void`（秩 0 裁定：秩 0 张量的静态类型就是元素类型）。所以 `x[0]` 的类型是
`void`，而 `add`/`gt` 没有 `(void, float)` 的重载，报 `ArgumentsRejected`。

**要裁定的问题**：从「声明未述元素类型」的张量里取出的秩 0 标量，它的静态类型是什么？

- 选项 A：仍然 `void`（现状），调用点自己用 `cast` 说清元素类型。代价：`x[0] + 2.6`
  这类写法必须写 `cast(x[0], 'float32')`，`test/cases/trans/annotated/boundaries.cml:13`
  与 `test/cases/opt/generic/arms_laid_out_whole_tensor.cml:23` 这两条用例的写法（以及复制它们
  的 exemplars）都要改，属于改用例写法。
- 选项 B：未述元素类型**传播到标量**——存在一个「元素类型未述的标量」，算术上按数值标量接受，
  运行期按值自己的 dtype 结算。代价：类型系统里要有一个「数值标量、具体哪种未定」的表示
  （今天只有数组有这个表示，标量没有），并且它要能参与重载决议；这会牵动 `promote`、
  算术算子的重载表。
- 选项 C：`t[0]` 处报一条明确的诊断（「元素类型未述的张量不能取出标量，请先定元素类型」）。
  代价：`boundaries.cml` 等用例的期望要改成这条诊断。

本 lane 只测到「今天两条用例报的是 `NoMatchingFunction: (void, float)`」，与文档里
「未述元素类型」是同一条规则的两处表现，但没有裁定就不动 `takeInfer`。

## 二、数组打印的第二条路把裸 `Tensor` 的每个元素打成 `null`；但 79 条红的 golden 是 tensor 打印形式

实测：`src/core/rtdata/print.cpp` 的 `printValueImpl`（数组分支）用**声明类型**的
`elemType()` 逐个槽 `pushSlot`，收到声明类型为裸 `Tensor`（`elemTypeCode() == Void`）的数组时
每个元素都打成 `null`。判据（`zeros([2])` 一个张量，三种拿法）：

```
let x: Tensor = zeros([2])   ->  [null, null]      <- 声明未述元素类型
let y = zeros([2])           ->  [null, null]      <- lit() 返回值，同样未述
let z = zeros([2] as f32[2]) ->  [0.0, 0.0]        <- 声明 f32[2]
to_bytes(x)                  ->  Bytes[00 00 00 00 00 00 00 00]   <- 数据在，8 字节 f32 零
shape(x)                     ->  [2]                              <- 形状也对
```

即**数据在、形状在，只有打印丢**。同一条规则在 `src/core/rtdata/array.cpp:1244-1251`
**早就写着**（注释逐字：「声明没定元素类型时，元素按值自己持有的元素类型打印」），
`Array::print` 也已回退到 `elemType_`；但 `:str` / `format` 走的是 `print.cpp` 这条第二条路
（`array.cpp` 那条加过探针，一次都没被触发过，这就是直接证据）。

**要裁定的问题**：这条路的回退要不要照 `array.cpp` 那条补齐？——补齐本身只是把已经写下的
规则补到一个漏掉的读者上，**但**它救不了那 79 条红：它们期望的是 tensor 打印形式
（`tensor([      8.,       8.], dtype=float32)`，本台账 L202-203、L684-780），而不是数组形式
（`[8.0, 8.0]`）。本 lane 实测两条代表用例：

```
opt.generic/arm_tail_gate  (std::macro std::opt::inline std::nvm)
    实际    [null, null]
    期望    tensor([      8.,       8.], dtype=float32)      test/plans/feat/opt/generic.plan.toml:684-780
modules.tensor/reshape_view_gc
    实际    rows [null, null, …(40 个)…] total 16380.0
    期望    total 16380 与 [      2.,       4.,       6.,       8.,      10.,      12.]
            test/plans/feat/modules/tensor.plan.toml:368
```

即一个是 tensor 形式、一个是「逗号后按 `%8.1f` 宽对齐的数组」形式（元素之间两个空格、
逗号后 7 个空格），都不是今天打印机给的 `[2.0, 4.0, …]`。所以**打印形式这条线是作者裁定的
范围**（谁来打印数值数组：数组打印机，还是 tensor 打印机，以及数组打印机的对齐规则），
本 lane 只测量、不改 golden、不新增语义。

## 三、与本 lane 已做的那一片的关系

h419 做的那一片（整数列实参 `take`/`take_grad`/`one_hot` 按值自己的 dtype 读，5 条转绿）
与上面两件事同源但互不影响：它只动「谁有 dtype 就读谁」，没有动 `takeInfer` 的返回类型，
也没有动打印。`f32[..]` 那条链的实测计数（`bare-tensor-parameter-instantiation-measured.md`
里那个用例的 36 处）在本 lane 的改动前后**都是 36**，即本 lane 没有新增任何 `f32[..]` 位置。