# 删掉过渡类型之后：四处待裁定（h550 lane，2026-10-25）

> 本文只报选题与逐字读数，不定语义。作者页面（`docs/handover/README.cn.md`、`reviews/**`、
> `docs/design-principles*`、`docs/design/**`、`docs/language/**`）未改，除
> `docs/design/arrays-memory/plan.cn.md` 第 5.1a 节依赖表里一处已删符号的引用（文档链接检查器要求）。

本 lane 按第 5.1a 节做删除步：`ArrayType::numericUnknown*` 与 `numericUnknown_` 删除、`Tensor` 的
`Reference` 变成"元素类型必填的名字"、签名里的裸 `Tensor` 报 `SemanticDiag::TensorElementTypeRequired`
（语义段新号 `0x0001ED`）。下面的四处读数都在这个状态下取的。

## 一、`stdlib/optim.cml:311` 的"元素类型谁也不写"的叶子

`adam_value_leaf` 用一个运行期条件在两种写法之间选：`size(to_bytes(p)) == numel(p) * 8` 时走 f64 的
`adam_wide`，否则走 f32 的 `narrow_tensors(adam_wide(...))`；两条分支的结果交给 `unstated` 这个恒等函数，
它的返回类型原先写裸 `Tensor`：

```cml
func unstated::<A, B, C>(t: (A, B, C)): (Tensor, Tensor, Tensor) {
    return (t.0, t.1, t.2)
}
```

现在的读数（`camel stdlib/optim.cml`）：`The type 'Tensor' states no element type` 落在 `optim.cml:311`
的返回类型上；所有 `import optim` 的计划随之红（快层 27 条，路径落在 `out/latest/stdlib/`）。

问题的形状：这个函数存在，是因为**同一个叶子在一次遍历里只有一种类型**，而两条分支的元素类型不同
（f32 / f64），值自己知道该是哪一种。类型系统里已经没有"元素类型待定"的类型，于是 `if` 两支的类型
join（`ArrayType::unify`）现在没有答案——删除步之后它返回"没有共同类型"，而不是原来的标记数组。
可选写法（都要作者裁定，不是本 lane 能定的）：

1. 叶子改成总是 f64：f32 那一支也算 double、由存入时一次舍入（`modules/tensor/kernels/update.h` 的
   融合内核就是这么做的），`narrow_tensors` 不再参与类型；
2. 保留两种写法，但在 `if` 的一支上显式写转换，让两支同型；
3. 语言给出"两个元素类型不同的数组 join 成什么"的裁定（见第四节）。

## 二、`show` 的伞形形参：元素类型谁也不写的数组没有候选

`stdlib/tensor.cml` 里那条 `show(t: Tensor)` 是伞形声明（任何数组、任何秩、任何元素类型）。
删除步把它的单个 arm 写成 `show::<A>(t: A[..])`：秩 2、秩 1 的数组都仍然有候选（实测
`show(new([[1.0, 2.0], [3.0, 4.0]]))` 与 `show(new([1.0, 2.0]))` 都编译并打印），但**元素类型谁也不写的
数组没有候选**：

```
$ camel show_probe2.cml            # show([])
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <> (void[]).
help: ... Available overloads:
          <tensor:show>: (t: A[..]) => void uses stdout
```

`A[..]` 绑不出元素类型（空数组字面量 `[]` 的类型是 `Void[]`，绑 `A = Void` 会走到"元素类型待定的数组"
本身，它已经不是值类型），所以这不是写法问题。快层因此红了 1 条实测用例
（`native.tensor_show_empty_array`，它原先钉的是"编译通过、运行期由内核报错"）。请裁定：
`[]` 该有元素类型（由上下文给出），还是 `show([])` 应当就是编译错误、用例改写成负例？

## 三、`to_bytes` 等算子的 `when` 子句：bool 元素类型算不算

删掉的那 7 个 arm 里，`to_bytes(t: Tensor)` 接受**任何**元素类型；旁边的泛型 arm 写的是
`when is_numeric::<T>()`。于是 `bool` 数组丢了算子（快层 4 条 + `f32_program_bytes_unchanged` +
`poison:…binary_roundtrip`）：

```
No candidate of 'to_bytes' applies to (<> (bool[3])): every candidate's 'when' condition is false
```

值层面的字节编码是支持 bool 的（`test/cases/modules/tensor/binary_bool_error.cml`、`binary_bytes.cml`
钉的就是它）。请裁定每个张量算子接受的元素类型集合：给 `to_bytes` 加一个含 bool 的谓词，还是去掉 `when`
（回到伞形 arm 的宽度，由内核在运行期拒），还是保持现状并改这 5 条用例。

## 四、两个元素类型不同的数组 join 成什么

删除步之前，`unify(int[3], f32[3])` 的答案是"元素类型待定的数组"（标记数组）。现在它没有答案：
`src/core/type/array_type_selftest.cpp` 钉的是 `unify(...) == nullptr`，快层里没有因此变红的用例
（第一节的 `optim` 是唯一已知的消费者）。请裁定：这个 join 的拒绝是终态想要的吗？如果程序需要一个
join（比如 `if c then xs else ys`，两支元素类型不同），是由写法显式转换，还是语言给一个答案？