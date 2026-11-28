# 遍即函数：第 2、3、5 步的改前读数与改动清单

**日期**：2026-11-28　**性质**：只读页（闸门未开：未合 20 条，未落代码；这一页在工作树里是 `??`，不提交）
**工作树**：`D:\Projects\Camel-wt\kinds-oper`，枝 `lane/program-pass-call`，尖 `7235e0e2e3`
**基线读数**：`opt.pass_function` 8/8 绿、`stdlib-reference --verify-ops` 退出 0、`check-doc-links` **红 3 条**（见 §6）

## 0. 一句话

三步共用一处事实：**遍的登记串要变成算子 URI、算子记录要持有实现**（第 2 步），
之后"程序里调用"与"命令行走"才可能是同一个入口（第 3 步），调度遍才只剩一条诊断（第 5 步）。
今天这三处的事实分别是：登记串是**名字**、实现存在**第二张表**、调用是**占位**；下面每步先贴读数，再列改动与 diff 面。

## 1. 第 2 步：登记只有算子表一处

### 判据（页 `internal/plans/2026-11-26-pass-as-function.cn.md:29`）
> 遍注册表的读者为 0（全部改问算子表）；`extract` 的 C++ 入口只剩一个；`stdlib-declarations --check` 与 `stdlib-reference --verify-ops` 全过。

### 改前读数（逐条，今天量到的）
```
第二张表本体：src/kernel/execute/pass.cpp:87-99
    `struct PassImplementationRegistry { std::mutex mutex; std::vector<std::optional<PassDescriptor>> kernels; }`
    出现 3 次、只在这一个文件；`passImplementations()` 5 次
它的读者：findPassKernel 6 处（pass.cpp:285 定义、:315 注释、:322 :371 :405 三处调用；params.h:180 声明）
登记入口：registerModulePass 30 处 / 15 个文件（其中 registration.cpp 4 处是 registerPassKernel 的转发，
    camel_pass.cpp 1、pass.cpp 3，其余 13 处是模块自己登记：tensor::fuse、onnx::export、graph::probe、
    test::、tiles::*、transforms::*）
遍的登记点：PassKernel 构造 166 行 / 79 个文件（`const PassKernel xxx("std::...", ...)`）
extract 的 C++ 入口：registerExtractPass（src/stdlib/rewrite/module.cpp:1867；调用点 :5028）—— 已经只有一个
stdlib 检查：stdlib-reference --verify-ops **exit 0**（本轮实测）；
    stdlib-declarations --check 不在 -Check 的检查器表里，需单独跑（本轮没跑，见 §6 待办）
```

### 改动清单
```
1  src/kernel/include/camel/execute/pass/registration.h（四组重载 8 个出口，见 :43-88）
   `path` → `operatorUri`；文件头注释 :4-21 重写：一条遍 = 一个原生算子，登记的是**算子的 URI**
   （`std:opt`），不是源码里的名字（`std::opt`）；名字与 URI 的连接在声明（`#op('std:opt')`）里，只有一处。
2  src/kernel/execute/registration.cpp（4 个转发，:13-44）机械改名。
3  src/kernel/include/camel/execute/pass/params.h
   删 `registerModulePass(path, PassDescriptor)`（:172）与 `findPassKernel(OperId)`（:180），
   换成 `registerPassKernel(operatorUri, PassDescriptor)` 与 `passKernelOf(OperId)`；
   `PassDescriptor`（:147-162）留在这一层不动（它含 `PassMaker`，是执行层的类型）。
4  src/kernel/execute/pass.cpp
   删 `PassImplementationRegistry` 与 `passImplementations()`（:87-99）；
   `registerModulePass` :238-279 → `registerPassKernel(uri, descriptor)`：`OperatorTable::recordPass(uri, make_shared<PassDescriptor>(...))`，
   不再 `intern`（登记不许把一个名字变成算子）；
   `findPassKernel` :285-295 → `passKernelOf(OperId)`；`findDescriptor`/`findImplementation`/`resolvePassOperator`（:297-323）不动
   —— 它们本来就走"名字 → 声明 → URI → 算子表"。
5  src/kernel/include/camel/core/operator_table.h + src/kernel/core/operator_table.cpp
   一条记录（:951 那个 `bool readsTags` 旁边）加 `std::shared_ptr<const PassDescriptor> passKernel;`，
   加 `recordPass(std::string_view uri, ...)` 与 `passKernelOf(OperId)`；
   **层问题（要定）**：`PassDescriptor` 在 `camel::execute` 里，算子表在 `camel::core`。
   可行做法：表头只前置声明 `struct PassDescriptor;`（`std::shared_ptr` 允许不完整类型），
   定义与构造留在 pass.cpp，头文件不反向 include 执行层。**这样可以做到"一张表"，但要你点头**：
   另一种做法是把 `PassDescriptor` 搬进算子记录那一层，动的是 4 个头文件的归属，diff 更大。
6  166 个 PassKernel 点（79 文件）+ 13 个模块 registerModulePass 点：字符串从名到 URI
   （`"std::opt"`→`"std:opt"`，`"tensor::fuse"`→`"tensor:fuse"`，`"onnx::export"`→`"onnx:export"`…），
   纯机械替换（URI 与声明的 `#op('…')` 必须逐个对上，所以用"声明里的 URI 列表"生成，再逐个 diff 核）。
```

### diff 面（预计一笔）
```
约 92 个文件 / 约 180 行：6 个文件是接口（~120 行，含注释重写），86 个文件各 1-3 行是字符串。
风险点：URI 拼错 ⇒ 那条遍的登记落到一个没人查的算子记录上，`stdlib-reference --verify-ops` 会报
（它按声明查实现），所以判据能兜住；但**要一次全改完再跑**，否则中间态两张表混着。
```

## 2. 第 3 步：程序里调用与命令行调用走同一个入口

### 判据（页 `:30`）
> 新用例——同一个 C++ 遍，在程序里对函数值调用与在命令行上跑，打出的规范 jgir 逐字节相同（nvm/fvm/jit 三个后端）；作用于一个非根的函数值（闭包、库函数）也成立；原函数在调用后不变。

四个 sha256 的定义（本页定死，免得改完各说各的）：语料 `test/cases/opt/cxx_isolation/subject.cml`，
```
H1 = CLI 形的文档：camel subject.cml std::cxx::cse std::jgir canonical=true std::nop 的 stdout
H2/H3/H4 = 程序形的文档，分别在 std::nvm / std::fvm / std::jit 上跑出的 stdout
判据：H1 = H2 = H3 = H4（四个 sha256 全等）
```

### 改前读数
```
H1 已有（本轮实测）：11569 字节、sha256 = 18450a3322d603f6668f6ef561c1ddf8d6b73b56c9b69af66d080b8f9a2b7d4c
H2/H3/H4 今天**不存在**：程序形连文档都产不出，停在类型检查：
  (cse_call.cml) /…/cse_call.cml:12:13: [Error]: This value has type '((x: int) => int pure)!'
  and it is fallible: calling it as a function leaves the error it may hold nowhere to be read.
  —— `let g = f->std::cxx::cse` 这一句**过**（图上落了 std:cxx::cse 的算子节点），
     错在把返回值 `F!` 当函数调用（`g(3)`），不是错在取遍的值。
占位实现：src/stdlib/std/builtin/executors/std_pass_call.cpp:22-25
  七个执行器（inline/fold/algebra/saturate/cse/dce/order）各抛
  PassInternalError + "the pass-as-function call path is not implemented yet (plan step 3)"
  —— 这条文案今天**程序走不到**（要先能调用返回值），所以它只是"接上了但没跑"的记号。
```

### 改动清单
```
1  std_pass_call.cpp：七个 `passCallNotImplemented` 执行器改成**一个**内核（与 CLI 同一份实现）：
   取节点上的算子 id → `OperatorTable::passKernelOf(id)`（第 2 步之后才有）→
   从实参的函数值取出它可达的图（"以它为根取出它可达的图"）→ `make(ctx, args)` 建遍 →
   `apply(program)` → 把回来的程序包成一个函数值交回（图是值：原函数不动）。
   七个 URI 的声明（kPassCallUris / getStdProtocolOpsMap）保留，但**行为来自算子记录**，不再各写一个桩。
2  "函数值 ↔ 程序"的边界**已有**，不新写，复用 camel_pass.cpp 那一处（两个方向都在）：
   正向（函数值 → 程序）：`program.h:7-8` —— 函数值持有它被造出来的 Program 与它的 GraphId；
       `camel_pass.cpp:215` 就是从函数值取 `program->root()` 的同一个做法。
   反向（程序 → 函数值）：`camel_pass.cpp:280-289` ——
       `::Function::create(program, program->root()->id(), noClosure, mm::autoSpace())`。
   ⇒ 第 3 步的内核只需：实参函数值 → 它的 Program（根设成它的图）→ 建遍 → `apply` →
     用上面那一行把回来的程序包成函数值。"原函数不变"由 Program 不可变（`program.h:11`）保证。
3  测试：新用例 `test/cases/modules/pass_as_value/cse_call.cml`（程序形，打印规范文档）+
   `test/plans/feat/modules/pass_as_value.plan.toml` 四条（cli / nvm / fvm / jit）用 sha256 比较；
   非根函数值（闭包、库函数）各一条；"原函数调用后不变"一条（两次调用同一函数值，第二次仍得 H1）。
```

### diff 面（预计一笔）
```
约 4-6 个文件 / 约 200-300 行：std_pass_call.cpp（~150；两个方向的转换复用 camel_pass.cpp，不新写）、
计划与四条用例（~80）。**依赖第 2 步**：`passKernelOf` 是这一笔唯一能拿到实现的地方。
```

## 3. 第 5 步：调度遍留一条诊断

### 判据（页 `:32`）
> 程序里 `f->std::fvm` 给出那条诊断（一条负例用例钉住），流水线行为不变。

### 改前读数
```
今天程序里写 `f->std::nvm` 得到的是**导入名**的拒绝，不是要的那条诊断：
  (sched_call.cml) /…/sched_call.cml:10:16: [Error]: Reference 'std::nvm' is declared as imported
  but is not exported by its module. Check that the module exports the name 'std::nvm' …
  —— 调度遍在 std.cml 里不是 `export` 的，所以"作值"在导入这一层就断了。这既是改前读数，
     也说明第 5 步要先把调度遍声明成可导出（名字统一那一半），再给"只在流水线上用"的说明。
```

### 改动清单
```
1  src/stdlib/std.cml：调度遍（nvm/fvm/jit/prl/nop/run）的声明改成可导出（或按第 1 步的模块导出规则补上），
   否则程序里连"拿到这个值"都不成立。
2  诊断：新增一条说明"调度遍执行程序，只在流水线上使用"（语义诊断或运行时诊断，按它落在哪个词上定；
   位置 = 那个遍名），并进 registry.tsv + examples.tsv + 帮助文字 + 生成文档。
   改前读数里没有它，因此这条诊断是**新增**，不是改文案。
3  测试：一条负例（`f->std::fvm` ⇒ 那条诊断、退出 1、位置在遍名上）+ 流水线行为不变（smoke + feat/modules 逐名比）。
4  设计原则第 9 节补"命令行的执行循环在宿主一侧，调度遍只在流水线上"（页 :37 已列）。
```
### diff 面
```
约 5-7 个文件 / 约 80-140 行（std.cml 一行一处、registry/examples/帮助/生成文档、一条用例、一条计划条目）。
```

## 4. 五处里的剩下那一处（`D-1128-199` ②：可单步性做成 C++ 接口）

```
五处的现状：①③④⑤ 已在我的尖上（③ `pipelineNamesProfile` 0 处、⑤ `declareCoreTagReaders` 0 处、
④ settleRunsRefusal + 参数词位置、① std::jit 的告警移到内核）—— 只剩 ②。
今天的名字分支：src/tools/debugger/src/run.cpp:119-143 `debuffablePasses`
  `if (pass == "std::jit")` / `else if (pass == "std::prl")` 两条按名字判，
  然后把遍换成字面量 `"std::fvm"`，提示里还写着后端名（:139 "runs as std::fvm under the debugger"）。
改法（按 D-1128-199 原文，不加新注解）：
  · 在 pass/base.h 加一个纯接口（与 GraphSchedulePass 一样 `dynamic_cast` 问）：
    `class SteppablePass { virtual bool steppable() const = 0; }`（名字待定，语义是"能单步"）；
    顺序解释器实现它，编译执行与并行调度不实现。
  · 调试器遇 `!steppable` ⇒ 换成 `runPassDefaultTarget()`（配置里的 passes.run），
    它也单步不了 ⇒ **带位置拒绝、不写后端名**。
diff 面：约 4 个文件 / 约 60-90 行（base.h 接口、两三个调度遍类、run.cpp 那一块）。
```

## 5. 落笔次序（先说好，再一次改完）

```
1 第 2 步（可独立验）：接口 + 表合并 + 166 处字符串，一次改完；读数 = findPassKernel/PassImplementationRegistry 0 处、
   stdlib-reference --verify-ops exit 0、smoke + feat/opt 逐名比。
   注意：这一步会同时消掉"遍注册表"和"名字登记"，所以第 1 步的读数（别名 0 处）必须保持不变。
2 第 3 步：`passKernelOf` 接上程序形调用；先写四条 sha256 用例（今天 H2/H3/H4 是红的），再改实现。
3 第 5 步：调度遍声明可导出 + 那条诊断 + 一条负例。
4 ② 可单步性接口 + 调试器。
每步一笔，判据先取，不先改断言。
```

## 6. 本枝上一条**合并前必须修**的红（不在三步内，但挡合并）

```
check-doc-links --citations exit 1，3 条未验证引用（我的尖上；指向的符号在我这几笔里搬走了）：
  docs/design/effects/plan.cn.md:60   'kernel/execute/pass.cpp#std::prl' → 文件已不含 'std::prl'
  docs/design/effects/plan.cn.md:248  同一条
  test/plans/feat/linear/jit_without_codegen.plan.toml:19  'kernel/execute/pass.cpp#jitWithoutCodeGeneratorWarning'
真实位置：std::prl 的登记与说明在 src/stdlib/std/prl/taskflow.cpp（:1300、:2254、:3276）；
  std::jit 缺代码生成器的告警在 src/stdlib/std/fvm/fastvm.cpp（FastVMSchedPass::apply）。
三条都只需改引用文字（不动代码、不动断言）。要我改就说一声，改完 -Check check-doc-links 11 秒可验。
另：stdlib-declarations --check 不在 -Check 的检查器表里，第 2 步的判据要用它 ⇒ 需要有它的一条入口
（加进检查器表或给一条 plan 包着），这一条我不自己加。
```

## 7. 未决（交决策者）
```
· 第 2 步的层：**已定（负责人 21:00 判）：算子记录持 `shared_ptr<const PassDescriptor>`** —— 引用不是副本，一张表、多处持有句柄不构成第二份真相；搬层（动 4 个头文件）留作后续。
· 第 3 步：**已定**，"函数值 ↔ 程序"边界已有（`program.h:7-8` 的持有一句 + `camel_pass.cpp:215/280-289`
  一次正向一次反向），第 3 步复用它，不新写转换。
· 第 5 步那条诊断：**已定（负责人 21:00 判）：新增**。`ImportNameNotExported` 说的是名字没导出，不是调度遍只在流水线上使用；新诊断按**条件**起名、不按后端名起（`D-1128-199`：不靠名字）。
```


## 8. 今晚已经落下去的一笔（引用修正）

```
commit 8621f0b278 "docs: the citations that named `std::prl` and the JIT warning follow their symbols"
2 files changed, 5 insertions(+), 5 deletions(-)
  docs/design/effects/plan.cn.md:60/:248  kernel/execute/pass.cpp#std::prl → stdlib/std/prl/taskflow.cpp#std::prl
  test/plans/feat/linear/jit_without_codegen.plan.toml:19
      kernel/execute/pass.cpp#jitWithoutCodeGeneratorWarning
      → stdlib/std/fvm/fastvm.cpp#FastVMSchedPass::apply（并在同一段说明它现在是
        `#if !ENABLE_FASTVM_JIT` 下、每实例一次 `saidNoCodeGenerator_` 报告）
验：-Check check-doc-links  exit 0
    "no unverified citation: 1259 of 1259 symbol citation(s) verified, 2674 path citation(s) across 7298 file(s)"
    整个 run 9 s（transfer 4.3 + build 0.2 + plan 4.1）
顺带：该计划自身 2 个用例在本机 skip（本机 JIT 有代码生成器，skip_unless --absent），
     所以这一笔只动引用文字、不动任何断言。
```
