# 问题：第一批标准库 `#impl` 的内核在哪个模块声明（`handover/h326-stdlib-impl`，提交 3c6f4eaba）

## 问题
Q2 要求"第一批标准库 `#impl` 现在就做"，但 10.4 同时要求内核是函数**自己模块**的无体声明，
而今天的模块系统里只有一个形状能同时满足两半——既能写函数体、又拥有那个内核算子的名字；
标准库里只有 `array` 是这个形状（它的算子在 libcamel 注册，`stdlib/array.cml` 因此没有同名
`.cmo`）。于是 Q2 点名的三条里，第 2、3 条今天没有声明位置：`tensor:log_softmax` 属于 tensor
模块，`stdlib/tensor.cml` 是 `tensor.cmo` 的声明模块，在里面写函数体报
`DeclarationModuleHasBody`（0x0300017D），别的模块声明这个算子又报
`NativeDeclarationForeignOperator`（0x03000181）；Adam（参数树上的定义）与 `array::sum::<T>`
还多一道 `ImplOnGenericFunction`（0x030001DD）——泛型函数不带内核，落在哪里都一样。

## 选项与代价
| 选项 | 代价 |
|---|---|
| (a) 声明模块可以持有带 `#impl` 的函数体（内核就是它的实现；"声明模块无函数体"这条针对的是 Camel 写的实现） | `DeclarationModuleHasBody` 要么只对带 `#impl` 的函数不再报（窄），要么对整个声明模块放开（宽，等于取消这条规则）；守卫 `native.declaration_module_has_no_body` 随之改写。`stdlib/tensor.cml` 里会同时出现算子声明与同名函数（`log_softmax` 的固定签名与 C++ 算子组同名，H228 第 6 条允许固定声明与泛型同名），`stdlib-reference` 的生成与核对要吃下声明模块里的函数体。语义归属要一并裁：`tensor_definitions::log_softmax` 今天是 `tensor:log_softmax` 的规范定义（7.1），之后同一个算子会有两个 Camel 体，哪个是规范定义、另一个是否删掉 |
| (b) 外模块可以声明内核（`stdlib/tensor_definitions.cml` 直接写 `#op<'tensor:log_softmax'>`） | 这正是 H94 裁定 5 与 Q2 明确拒绝的那条：一个模块写另一个模块的签名。`NativeDeclarationForeignOperator` 与守卫 `native.foreign_operator_refused` 要撤或改写，tensor 模块不再是它自己算子签名的唯一出处。选它需要比"省事"更强的理由 |
| (c) 语言不动，Q2 的第 2、3 条推迟 | 融合与导出的对照对象仍缺：`log_softmax` 现在只有"C++ 算子 + `tensor_definitions` 的定义"这一对（已经存在，但不是同一条 `#impl` 函数的两半）；Adam 一侧连 `stdlib/optim.cml`、`modules/optim`、`optim:*` 算子在树里都不存在，即便有了声明位置也还差泛型那一关 |

## 倾向
(a) 更贴 10.4：内核确实就是实现，声明模块"无体"的理由对带 `#impl` 的函数不成立；(b) 是已被
拒过一次的放宽，要选它得给出比便利更强的理由。**判断属于语言作者**；且无论选 (a) 还是 (b)，
Adam 与 `array::sum` 的泛型那一关都还在，需要另裁"每个实例一个内核"还是"第一批只用非泛型
函数"。在这条裁定之前，Q2 的第 2、3 条处于**推迟**状态；`array` 那一条（不是本分支的工作）
被同一道泛型规则挡着。

## 出处
- 裁定原文：`docs/handover/reviews/H141-H213.md`「答：遍参数的字符串与 `#impl` 的跨模块内核」
  的 Q2 三条；同节末尾的 `grad` 观测不属于本问题。
- 诊断：`NativeDeclarationForeignOperator` 0x03000181、`DeclarationModuleHasBody` 0x0300017D、
  `ImplOnGenericFunction` 0x030001DD、`NativeDeclarationNotLoaded` 0x0300017C、
  `ImplKernelUnknown` 0x030001D9。
- 代码：`src/core/context/context.cpp#loadModuleFrom`（同名 `.cml` 与 `.cmo` 即成声明模块）、
  `src/compile/gir/builder_modules.cpp#declareNativeOperator`（协议检查）、
  `src/compile/gct/builder.cpp`（声明模块里的函数体）、`src/core/module/userdef.cpp#operatorProtocols`、
  `src/builtin/modules/globals.cpp`（`array` 的算子在 libcamel，故 `array` 两半都齐）。
- 证据（2026-10-23 在 88 上跑过）：`test/cases/compile/native/impl_kernel_site.mjs` 三台程序——
  非 tensor 模块声明 `#op<'tensor:log_softmax'>`；`#op<'nn:adam'>` 加泛型 `#impl`；tensor 模块
  自己的 `.cml` 里放 `#impl` 体（它**可以**声明内核，只有函数体被拒）。计划条目
  `impl.kernel_site`。红线：同一文件把 URI 写成 `tensor:log_softmax_absent`，收到的是
  `NativeDeclarationNotLoaded` 而不是所有权拒绝，所以这条守卫分得开"算子不存在"与"模块不拥有
  它"。
- 导出侧现状：`modules/onnx/lowering.cpp` 已把 `tensor:log_softmax` 降到 ONNX `LogSoftmax`
  （13），缺的只有声明位置；融合侧 `rules::fusion` 的张量规则只有 sigmoid 链。
- 同一堵墙的另一半（不是本分支的工作）：`stdlib/array.cml` 的 `sum`/`min`/`max` 都是 `::<T>`
  泛型（`when is_numeric::<T>()` / `is_ordered::<T>()`），今天没有 `#impl`；
  `stdlib/optim.cml`、`modules/optim` 与任何 `optim:*` 算子都不在树里。