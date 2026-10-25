# 另一个轴的切片是拷贝，不是视图（非连续视图仍缺）

> 说明（协调者）：提出问题时的代码在 `handover/h585-slice-all-calls`（基点 = 翻转线上的切片提交 `c9be5d619`；核心已接管所有 `slice(...)` 调用）。页里引用的行号以那条分支为准；三页都是规格问题，不是实现缺陷。


车道：`handover/h585-slice-all-calls`（族 = 核心接管全部 `slice(...)` 调用）。

## 一、是什么

裁定（第 5 条）要核心答"任何秩、任何轴"的切片，设计原则 5.4 说 `t[a:b]` 是**共享数据块的视图**。今天的核心切片只在一种情形下交出视图：**第一轴 + 步长 1 + 紧凑数组**；其它情形（另一个轴，或任何步长 ≠ 1）交出的是**拷贝**——元素值正确，但结果不共享父数组的数据块，写父数组不会在切片里看见，反之亦然。

翻转线为"带步长"记过一条偏差（"a stepped slice is a copy: the elements are the same and the program does not have to know, but the result does not share the array's block"），本车道把同一条读法扩展到"另一个轴"。**这是一条已知缺口，不是一个待定的设计**：非连续视图（`Array` 的带步长视图）今天就缺，落点是 H17。

## 二、逐字证据

1. 分支（`src/builtin/operators/struct.cpp`，`__slice_sub__`）：轴 0 + 步长 1 + `isCompact()` → `Array::view(arr, extents, range.start * arr->step(0))`；否则走拷贝（`parentIndexOfSlice` 逐元素落位、`Array::create` 造新块）。

2. `include/camel/core/rtdata/array.h`：`view(Array *source, span<const int64_t> extents, uint64_t elementOffset = 0)` —— 只有紧凑/连续视图；`isCompact()`、`isView()` 是它的判据。

3. 设计原则 5.4（`docs/design-principles.cn.md:348-362`）：`t[a:b]` 是共享数据块的视图，开界可省，步长写作 `t[a:b:s]`，没有负下标。

4. 今天的读数里没有一条用例守着"共享"。`test/cases/modules/tensor/subscript/slice_rank2.cml`（本轮新增/改写）逐字：

```
    println(shape(m[0:1]))
    println(shape(m[0:2]))
    println(sum(m[0:2]))
```

读数是 `[1, 3]`、`[2, 3]`、`21.0`——只断元素与形状。`test/cases/modules/tiles/core_views.cml:10` 的 `chained(x) = slice(permute(x,[2,0,1]),0,1,4,2)` 比对 CPU 与 GPU 的**数值**（`chain_cpu.npy` / `chain_gpu.npy`），也不分辨共享。

## 三、今天的读数

- 第一轴 + 步长 1：真视图（`Array::view`），共享块。
- 另一个轴、或步长 ≠ 1：拷贝。`t[0:4:2]` 读作 `[2]`/`4.0`（`slice_step.cml`），`t[3:0:-1]` 读作 `[3]`/`9.0`（`slice_step_negative.cml`），`t[:, 1:2]` 与 `t[0:1]` 在 `f32[2,3]` 上读作 `[1, 3]`（`slice_rank2.cml`）。
- 梯度不受影响：伴随按位置回散（`:tangent/scatter_slice_arr`，7 词，轴随行），拷贝与视图的梯度一致；`feat.modules.tensor.subscript`、`feat.modules.autodiff` 全绿。

## 四、选项与代价

**选项 A：给 `Array` 加带步长的视图（`view` 带 strides，或新增 `stridedView`）。** 5.4 对任何轴、任何步长都成立，拷贝消失。代价：数组模型引入步长视图，`isCompact()`/`isView()`/`step()` 的所有使用点、调度器、GC 与"谁拥有块、何时可释放"都要复查——这正是 H17 立在那儿的原因；`include/**` 是公共头，改动面比本车道大。

**选项 B：保留拷贝，把 5.4 的视图读法限定成"第一轴 + 步长 1"，并把偏差写进 5.4。** 代价：`t[:, 1:2]`、`t[0:4:2]` 在语言层不再有视图语义，依赖共享（原地更新、别名）的程序必须自己拷回；文档不点明这条限制，5.4 的读数就会被误用。今天的状态就是这个选项，只是 5.4 还没写它。