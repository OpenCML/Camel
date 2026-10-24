# Python 调用 Camel：绑定这一层今天靠什么站住、哪些语义要作者裁定（H253）

Author: 车道 `handover/h424-python-bindings`（`D:\Projects\Camel-wt\h424-python-bindings`），2026-10-24。
Status: open — 需要作者就下面第 1、2、4、5 条给裁定；第 3、6 条是我按现有文档落下并写清边界的，只有确认为"这就是契约"或"这也未定"两种回答。
测量在 Linux 主机（`88zjwei`）上、基线 `origin/develop`（`578d43f3b`）与本车道改动同一构建窗口内完成，读数走 `camel-remote.ps1` 与计划运行器。

## 数字先

- 端到端今天**通了**：`import camel` → `camel.load("rules.cml")` → 数组经 DLPack 零拷贝进出 → 诊断/故障带位置。
  计划 `test/plans/feat/bindings/python_binding.plan.toml`：`total 1, pass 1, fail 0`，7.24 s。
- 零拷贝有地址证据：`identity(xs)` 的 `in` 与 `out` 是同一地址；`scale(xs, k)` 不同地址且调用方缓冲区未变。
- 成本形状：64 MiB 交出去 12.9 us，本进程拷贝同样字节 4818 us（13.9 GB/s），相差 374 倍；8 个结果分别占 4.0 MiB / 34.0 MiB。
- `pip install` 出来的 `camel_lang-0.2.30-cp310-cp310-linux_x86_64.whl` 12 846 898 字节，`--no-index` 装上后**不设 `CAMEL_HOME`** 也能跑示例。
- 今天**只能调非泛型的 `export func`**：`DefinitionStore::libraryFunction`（`include/camel/runtime/definitions.h`）本身就不接受泛型函数，绑定没有绕过它。这是第 1 条要裁定的直接后果。

## 已经站住的部分（有文档或有实测，不算"发明语义"）

- **数组的 DLPack 通道与所有权**：`docs/design/arrays-memory/README.cn.md` §3.2 说这组字段与 `DLTensor` 一一对应、"交换是零拷贝的"，§7 的红线说"交给外部的数据指针在所有者释放之前一直有效"。绑定就按这个走：进来用 `fromDLPack` + `mm::autoSpace()`（只有这个分配器是零拷贝，其余 memcpy，`src/core/array/dlpack.cpp`），出去用 `toDLPack`（胶囊销毁时 release 那块数据块）。
- **作为宿主时的根**：同一文档 §6.2 逐字写"Python 持有的 Camel 值登记为回收的根"、"跨语言的环第一期不回收"。绑定今天的形态是：一次调用期间，实参（以及被 `toDLPack` 导出前的数组）登记在 `RootHandle` 里，调用返回后绑定**不长期持有任何一个 Camel 值**——出去的数组靠数据块自己的计数活着，标量/字符串在返回时复制成 Python 值。所以 §6.2 那句"跨语言的环第一期不回收"今天不触发；要问的是第 7 条。
- **运行期故障变诊断**：`RuntimeFault` → `Diagnostic` 用的是 `DiagnosticBuilder::of(fault.diag(), nullptr).commit(...)`，与 `src/execute/standalone_bridge.cpp` 同一条路，没有另造一套文本。

## 要裁定的

### 1. 泛型 `export func` 怎么从 Python 传类型实参

`libraryFunction` 拒绝泛型，所以 `stdlib` 里大量 `func f::<T>(...)` 从 Python 调不到；要调就得在 Camel 里再写一层具体类型的包装函数（本车道的示例与测试就是这么做的）。

要定的是：外部宿主传类型实参是**调用的一部分**（例如给 Python 侧一个"实例化签名"或 `f::<f32>` 的写法），还是**明确不支持、要求库作者写具体类型包装**？前者要定义实参书写形式、缺省推导规则、失败时的诊断；后者要写进文档，免得每个宿主各写一套包装。

### 2. 从外部宿主调用时，"模块搜索根"与入口目录的契约

`include/camel/utils/install_layout.h` 的 `buildModuleSearchPaths` **不把入口目录算作模块根**，`src/execute/standalone_bridge.cpp` 里命令行能跑是因为入口文件自己就在搜索路径上。绑定的处理是：给了 `foo/bar.cml` 就把 `foo/` 插到 roots 最前面（`camel/__init__.py` 的 `load`），否则 `import bar` 会找不到同目录的模块。

要定的是：**从外部宿主调一个模块文件时，那个文件所在的目录算不算模块根**？绑定现在的做法是"算"，但这是我为了让它能用而定的，不是文档里的语义；同一件事在 Camel 内部（`import`）与命令行下的行为要一致，否则同一份代码在三种入口下解析出不同的模块。

### 3. `T!` 的错误值到异常类型：一种 kind 一个类，还是一个类 + 名字

今天：任何错误值都是 `camel.FaultError`，`kind`（`error('RangeError', ...)` 的第一个参数）在 `e.name` 里，位置在 `e.file/e.line/e.column`。理由是不给错误值里的字符串赋予"它就是异常类名"的语义——那是宿主语言的类型系统在解释 Camel 的字符串。

要定的是：语言是否要求外部宿主把 `kind` 提升成类型（Python 侧生成 `class RangeError(FaultError)`）？如果是，`kind` 与诊断名字（`SemanticDiag::*`）是不是同一个命名空间？如果不是，请确认"统一 `FaultError` + `name`"就是契约。

### 4. 元素类型不匹配：拒绝还是转换

今天拒绝：形参 `f32[]` 只收 float32 的数组，dtype 不一致报 `ArgumentNotConvertible`（逐字：`argument 1 of energy: a numeric array must offer __dlpack__ ...`）；标量则按形参转换（Python `1` 可以进 `f32` 形参）。

要定的是：数组要不要也允许一次显式/隐式转换（意味着一次拷贝，与 DLPack 零拷贝的前提冲突），还是"数组类型必须完全一致、要换类型就在 Python 侧换"就是契约？我按后者实现并写进了边界，但这是可被推翻的选择。

### 5. 声明了但没 `export` 的函数：诊断还是"不存在"

今天区分三件事并给不同名字：模块没有这个名字 → `NoSuchExport`；模块有这个函数但没有 `export` → `FunctionNotExported`；模块整个找不到 → `LibraryUnavailable`。三条的消息都用运行时 `libraryFunction` 的 `problem` 原文，绑定只给一个更准确的名字。

要定的是：`FunctionNotExported` 这条要不要在**消息里**也说清"外部只能看见 `export func`"（今天名字说了，消息没有），以及这三种情况是否要各自的诊断码（今天 `e.code` 为空，因为它们不是 `Diagnostic`）。

## 已按现有文档落下、只需确认的两条

### 6. 线程与 GIL

`docs/design/arrays-memory/README.cn.md` §6.3 说"在工作线程上调用 Python 前取得 GIL"。绑定是反向的（Python 调 Camel）：它**全程持着 GIL**，一次调用期间不释放，所以一个长调用会挡住其它 Python 线程。这不是文档覆盖的情形，我按"先不引入无 GIL 调用"落下（引入就要定"释放期间 Python 侧还能对这些数组做什么"）。请确认这就是边界，或者裁定外部宿主调用是否要求可释放 GIL。

### 7. 长期持有（模块缓存、闭包）与 §6.2 的"第一期不回收"

绑定今天每次 `camel.load` 复用同一个进程级 `Context`（一个运行时只编译一次库），但**不缓存 Camel 值**。如果以后要在 Python 侧长期持有 Camel 闭包/结构体（例如把回调交给 Camel），就会撞上 §6.2 的"跨语言的环第一期不回收"。请确认 §6.2 就是这条路线的边界（那就是"不要这样做"的明文），还是要在这里加一条"宿主侧只许持有数据块引用"的规则。

## 本车道没有做的（避免被当成已定语义）

- 没有为泛型、遍选择（fvm/jit）、隐式 dtype 转换、异步/多线程调用中的任何一条新增语义；它们要么是拒绝，要么是文档里的既有边界。
- 没有改 `libraryFunction` 或 `fromDLPack`/`toDLPack` 的行为；绑定只在它们之上做转换与报告。