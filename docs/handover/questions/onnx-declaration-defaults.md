# `onnx:export_model` 声明落地时量到的两个默认值与两处外移的检查

分支 `handover/onnx-single-declaration`，落地提交 `d924829ea`：三条过渡规则换成一条声明，快层 `fail 0`、8/8。声明在 `stdlib/onnx.cml:48-50`：

```
#op('onnx:export_model')
with <out: string, dynamic_axes: void[] = [], target: string = 'onnxruntime-cpu'>
export func export_model::<F, E>(fn: F, example: E): void!
```

## 量到的三条

**① 裁定字面的 `int[]? = null` 调不通：裸值进不了声明的 `T?` 形参位。**
`export_model<out: p, dynamic_axes: [0]>(f, ex)`（`test/cases/modules/onnx/export_dynamic.cml:22`）报 `SemanticDiag::NoMatchingFunction`（`0x0300000F`），候选打印成 `<onnx:export_model>: <out: string, dynamic_axes: int[]?, target: string?> (fn: F, example: E) => void!`；`dynamic_axes: [5]`（`refusals/bad_axis.cml:11`）同样。全树 13 处给出 `dynamic_axes` 的调用都变红。默认值因此写成内核本来读到的具体值：空轴表，与 `modules/onnx/exporter.h:52` 的 `target = "onnxruntime-cpu"`。

**② `int[]` 会拒绝空字面量 `[]`。** `export_model<out: p, dynamic_axes: []>(...)`（`test/cases/modules/onnx/export_capability.cml:21`）报同一条，实参类型打印成 `<string, [], string>`——`[]` 不带元素类型。轴表因此写 `void[]`；旧 `isIntArray` 的元素检查移到内核（`modules/onnx/module.cpp:100-107`）：非数组或非整数数组是错误值 `ExportUnsupported`。

**③ 两条检查留在声明之外**（改动前是编译期拒绝，见本提交删除的 `resolveExportModel`）：`fn` 是不是函数——声明只能把 `fn` 写成类型变量（`fn: F`），内核守卫（`modules/onnx/module.cpp:88-94`）答 `ExportUnsupported`；`example` 是不是"一个参数函数的实参，或每参数一个例子的元组"——仍由导出器拒绝（`modules/onnx/exporter.cpp:4160` 的 `bind`、`:4324` 的 `checkArity`）。

## 请裁定（两条）

**(a) 这两个检查是否也该由声明承担？** 原则 6.4"声明是签名唯一来源"的边界在哪：是否要能声明"任意参数表的函数"和"实参是 `fn` 的参数树"；若不可写，是否接受这两条留在内核与导出器（其中一条由编译期拒绝降为运行期错误值）。

**(b) `T?` 形参位是否应当接受裸值？** 若应当（`dynamic_axes: int[]? = null` 与 `dynamic_axes: [0]` 并存），这是前端缺口——进 `T?` 位的入口现在只在赋值处（`931c62a4b`），实参位没有；若本就不该写 `int[]? = null`，默认值就照本次落地写成 `void[] = []` 与 `'onnxruntime-cpu'` 这两个具体值。