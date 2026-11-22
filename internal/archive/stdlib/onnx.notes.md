# onnx 重写说明

稿件：`onnx.cml`、`onnx.signatures.cml`（换进 `stdlib/signatures/onnx.cml`）。未编译。

## 改了什么

1. **导出是一个遍**（9.1、9.8：遍是函数变换 `(f: F) => F!`，设置是它的 `with` 参数）。`.cml` 里声明的是遍形式 `f->export_model<out: 'm.onnx'>`；程序在运行中导出自己某个子图的**调用形式**多一个 `example`（固定形状），在签名文件里：`export_model<out: path>(step, (model, batch))`。
2. **`path` 变 `with` 参数 `out`**，与遍参数同名同义（`onnx::export out=m.onnx` 与 `export_model<out: ...>`）。`dynamic_axes`、`target` 原先是第 4、5 个可选位置参数，改为带默认值的 `with` 参数（`int[]? = null`、`string? = null`）：配置不是数据，可选用 `T?` 而不是按元数重载。数据（被导出的函数、示例输入）是普通参数。
3. **失败是错误值**：翻译不了（残留的调用、没有 ONNX 降低的算子、说不出名字的 dtype/形状、目标运行时拒绝某节点）是 `ExportUnsupported`，写文件失败是 `IOError`，返回 `void!`（遍形式 `F!`）。原先抛 `RuntimeError` 故障。理由：导出是工具，调用者要能决定如何报告（`test/cases/modules/onnx/refusals/` 本就是在测"被拒绝"）。
4. `supported_operators` 声明为 `pure`：它读编译进模块的表，不碰文件系统或运行时；原来"按有效应注册，直到有人核对"的说法删除，C++ 里改为纯。
5. 删 `module onnx` 行、`@effect` 标签；遍声明写 `uses fs`。

## 函数表

| 项 | 处理 |
|---|---|
| `supported_operators(): string[]` | 保留，`pure` |
| `export_model::<F>(f: F): F!`（遍） | 保留；`with <out, dynamic_axes, target>`；`uses fs` |
| `export_model(fn, example, path[, dynamic_axes[, target]])`（调用形式） | 改为 `export_model<out: path, dynamic_axes, target>(fn, example)`，返回 `void!` |
| 失败种类 | 新增 `ExportUnsupported`（见 error-kinds） |

## C++ 侧改动（`modules/onnx`）

- 调用形式的解析器 `DynamicFuncTypeResolver` 改读 `with` 的 `out`、`dynamic_axes`、`target`，普通参数只剩 `fn` 与 `example`；内核从 `norm.get(2..4)` 改读 `with` 视图。
- 失败路径：`throwRuntimeFault(RuntimeError ...)` 改为 `makeError("ExportUnsupported"/"IOError", ...)`，位置用被拒绝节点的源码位置。
- `supported_operators` 的算子性质 `pure = true`。

## 调用点

| 变化 | 位置 |
|---|---|
| `export_model(f, x, path)` → `export_model<out: path>(f, x)`；带 `[0]` 的 `export_model(f, x, path, [0])` → `export_model<out: path, dynamic_axes: [0]>(f, x)` | 约 92 个文件：`test/cases/modules/onnx/`（含 `refusals/` 7 个）、`test/cases/modules/tiles/`、`test/cases/modules/autodiff/`、`test/cases/compile/native/`、`test/cases/opt/generic/`、`test/cases/consistency/`、`docs/`（教程、`docs/technical/25_tensor_stack_and_onnx_export.md`、`27_entry_parameters_and_pure_export.md`）；机械替换，一条脚本即可 |
| 钉住"拒绝是故障"的用例改为钉 `ExportUnsupported` 错误值 | `test/cases/modules/onnx/refusals/` |
| `supported_operators` 的 arity/type 用例 | `test/cases/compile/native/onnx_*` |

## 开放问题

1. **`uses fs` 与 `when` 子句的书写顺序**（`... : F! uses fs when is_function::<F>()`）：语法上 `resourceClause` 与 `when` 的先后我没有查证，编译不过时对调即可。
2. 调用形式在运行中导出，写文件是效应，在 `sync` 块外且结果不被使用时不会执行（3.1）；用例里已经是在 `sync` 的 `main` 里，不受影响。
