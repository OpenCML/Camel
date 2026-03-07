# Camel 核心库命名空间规范

本文档定义 `include/camel/` 下核心库的命名空间层级、路径与命名空间对应关系、别名约定及新代码必须遵守的规则。现状与迁移策略见《Camel 命名空间设计规划》。

## 1. 基本原则

- **根命名空间**：核心库统一使用 `camel` 为根。
- **路径对应**：`include/camel/` 下目录与命名空间一一对应（如 `camel/core/error/` → `camel::core::error`）。
- **范围**：仅适用于核心库（`include/camel/` 及对应 `src/`）；工具与应用（如 `tools/debugger`、`tools/camel-cli`、`src/service/profiler`）保持各自命名空间，不纳入 `camel::` 统一。

## 2. 路径与命名空间映射表

| include 路径 | 目标命名空间 | 说明 |
|--------------|--------------|------|
| `camel/core/source/*` | `camel::source` | 源码管理、Span、Origin、SourceContext |
| `camel/core/mm/*` | `camel::core::mm` | 内存管理与 Profiler |
| `camel/core/error/*` | `camel::core::error` | Diagnostic、Diagnostics、Range、异常等 |
| `camel/core/context/*` | `camel::core::context` | Context、Module、EntryConfig 等 |
| `camel/core/module/*` | `camel::core::module` | Module、BuiltinModule 等 |
| `camel/core/rtdata/*` | `camel::core::rtdata` | Object、Struct、Array、Function 等 |
| `camel/core/type/*` | `camel::core::type` | Type、FunctionType、TupleType 等 |
| `camel/core/data/*` | `camel::core::data` | Data、RefData、CompositeData 等 |
| `camel/core/*.h`（根级） | `camel::core` | ModifierSet、operator 相关、GlobalConfig、DebugBreakpoint 等 |
| `camel/parse/*`（非 ast） | `camel::parse` | Parser、CST 相关 |
| `camel/parse/ast/*` | `camel::parse::ast` | AST 节点与 Builder |
| `camel/compile/gir/*` | `camel::compile::gir` | 图 IR、Graph、Node 等 |
| `camel/compile/gct/*` | `camel::compile::gct` | GCT 节点与 Load |
| `camel/utils/*` | `camel::utils` | 工具（字符串、类型 traits、断言等） |
| `camel/common/*` | `camel::common` | Namespace、Scope、Reference、tree 等 |
| `camel/execute/*` | `camel::execute` | Executor、Pass、Frame 等 |
| JIT（`src/.../jit/*`） | `camel::jit`、`camel::jit::x64` | 与路径一致，保持现状 |

## 3. 别名约定

在根头文件或集中头文件中提供以下别名，便于书写与渐进迁移：

- `namespace GIR = camel::compile::gir;`
- `namespace AST = camel::parse::ast;`
- `namespace GCT = camel::compile::gct;`
- 可选（TypeTraits 迁移后）：`namespace tt = camel::utils::type_traits;`

现有 `.cpp` 中可继续使用 `GIR::`、`AST::`、`GCT::` 或 `using namespace GIR` 等，通过别名与目标命名空间保持一致。

## 4. 新代码必须遵守的规则

1. **新增头文件**（位于 `include/camel/` 下）  
   - 必须使用以 `camel` 为根的命名空间。  
   - 命名空间须与 `include/camel/` 下的路径一致（见上表）。

2. **禁止在核心库中**  
   - 在全局作用域新增类型（如 `class X;` 不加命名空间）。  
   - 新增不带 `camel` 的独立根命名空间（例如不再新增顶层 `namespace GraphIR`，应使用 `camel::compile::gir` 并通过别名 `GIR` 引用）。

3. **实现文件**（`src/` 下）  
   - 实现应放在与头文件对应的命名空间中；若使用 `using namespace GIR` 等，仅限实现文件，避免在头文件中 `using namespace`。

4. **前向声明与友元**  
   - 前向声明放在与定义相同的命名空间中；跨命名空间引用使用完整限定名或别名。

## 5. 应用层命名空间边界

以下为“使用 camel 的应用或服务”，**不**要求改为 `camel::`，仅在规范中明确边界：

- `debugger`：`tools/debugger/*`
- `CmdLineArgs` / `Global` / `Run` / `Check` / `Inspect` / `CLI`：`tools/camel-cli/config.*`
- `profiler`：`src/service/profiler/*`
- `ASTCodeGen`：`src/service/codegen/*`
- `opperf`：`src/passes/sched/fastvm/opperf.h`

## 6. 迁移状态（参考）

| 模块 | 目标命名空间 | 状态 |
|------|--------------|------|
| core/source | camel::source | 已就绪 |
| core/debug_breakpoint | camel / camel::core | 部分（当前为 camel） |
| utils/dll_path | camel::utils | 已就绪 |
| JIT | camel::jit / camel::jit::x64 | 已就绪 |
| core/mm | camel::core::mm | 已就绪 |
| core/error | camel::core::error | 已就绪 |
| parse/ast | camel::parse::ast | 已就绪 |
| parse（parser, CST） | camel::parse | 已就绪 |
| compile/gir | camel::compile::gir | 已就绪 |
| compile/gct | camel::compile::gct | 已就绪 |
| core/context, core/module | camel::core::context, camel::core::module | 已就绪 |
| core/type | camel::core::type | 已就绪 |
| core/data | camel::core::data | 已就绪 |
| core/rtdata | camel::core::rtdata | 进行中（base 已就绪，array/struct/tuple/func/string/conv 待包装） |
| common, execute | 见上表 | 待迁移 |
| utils（strutil/ascii/memperf/TypeTraits/cml） | camel::utils | 待迁移 |

迁移时注意：前向声明、友元、ADL、导出/可见性宏需随命名空间一并调整；可先用 `namespace X = camel::...` 或 `using` 做短期兼容，再在阶段 3 清理。
