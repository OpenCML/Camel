# 第④件「map 是循环」：第一笔

本页是 `internal/plans/2026-11-28-refactor-not-patch.cn.md` 第 4 件的第一笔材料：**要删的清单、替换后的形状、做比对的语料**（该页要求的"三样"）。它取代 `D-1128-172` 的帧复用、`task-428` 的元素调用计数、`task-429` 的运行期造循环图——那三件都不在目标架构里。

代码一笔没改；本页只记读数与形状。行号取自 `origin/integration@d1418ea52f`。

## 0 交接：`task-448` 已经量到的读数（原样搬来）

**`map` 的元素成本**（Release、工作站、同一次构建 `d1418ea52f`、同一会话交替 3×3，只报 ns/元素）：

| 程序（元素数） | `std::nvm` | `std::fvm` | fvm/nvm |
|---|---|---|---|
| `loop12`（`[i * i for i in 0 .. 12000000]`） | 146.8 | **584.9** | **3.98×** |
| `closure8`（`[f(i) for i in 0 .. 8000000]`，`f` 捕获 `k`） | 278.7 | **1396.6** | **5.01×** |

**585 ns/元素 花在哪**（`rdtsc` 探针，未提交；cycles/元素，按同一轮有效主频 ~4.27 GHz 折成 ns）：

| 段 | ns/元素（`loop12`） | 占比 | 是什么 |
|---|---|---|---|
| site 查表 | 192 | 33% | `fastvm.cpp:732` 每元素一次 `higherOrderCallSite(func)`（`programs_.graphOf` + `graphEntryPc`） |
| 调用入口 | 357 | 61% | `fastvm.cpp:738` `callElement` → `invokeCallOrJit` → `call` → `callBorrowed` 的入口 + `LMUL; RETN` + 返回 |
| 帧获取与填充 | 15 | 2.5% | `fastvm.cpp:735-736` `framePool_.acquire` + `populateMarkedCallFrame` |
| 读元素 / 递归检查 / 写结果 | 6 / 8 / 8 | 4% | `readElement`、`requireNestedCallRoom`、`writeElement` |

`closure8` 的"调用入口"桶是 985 ns（每元素两层元素级调用），其余各段相同。

**`290 ns/元素` 这条判据在基上量到多少：没有达到。** 基是 584.9（`loop12`）/ 1396.6（`closure8`）。按上表，把 site 查表与帧获取全部拿掉，地板仍在 ~370 ns/元素——**这条判据不是靠补丁能到的量级**，它就是本件要换机制的理由。

**`fib` 现状**（同一次构建、同一会话）：`nvm 99.8 / fvm 121.4 / jit 31.8 ms`，`std::jit` 比 `std::nvm` 快 3.1×。

**顺带作废一条早前的结论**：`task-428` 加的那次 `noteIndirectCall` 并不拖慢 `std::fvm`。同一会话里两棵 binary 交替量：`loop12` fvm 比值 0.998、`closure8` 1.036——早前"fvm 因计数慢 73%"是跨会话/温度漂移，不可用。计数只在 `std::jit` 编核那条路上有意义，而本件要把那条路整个换掉。

**循环机制本身还贵多少**（对照，同一构建）：`loop acc = 0 for i in 0 .. 12000000 do acc + 1` 三后端是 `nvm 2092 / fvm 5011 / jit 3647` ns/迭代。这是"把元素循环搬到字节码里"之后要面对的同一个循环机制，先把数摆在这里。

## 1 要删的清单

| 位置 | 行数 | 是什么 |
|---|---|---|
| `src/stdlib/std/fvm/fastvm.cpp` `evalMarkedOperator_map_arr` | `:705`–`:742`（38 行） | `map` 的 C++ 元素循环；`:729-740` 是逐元素 `for` |
| 同上 `evalMarkedOperator_filter_arr` / `_reduce_arr` / `_foreach_arr` / `_isolate` | `:744`–`:878` | 其余四个元素调用形状的同一个 `for` |
| 同上 `evalMarkedOperator` 的分派 `switch` | `:680`–`:703` | 把 `MarkOpCode::*` 送进上面五个 handler |
| 同上 `callElement` | `:497`–`:509` | 每元素一次调用入口；`task-428` 的计数就加在 `:500` |
| 同上 `populateMarkedCallFrame` | `:323`–`:360` | 每元素按"图的 norm/with/closure 端口"填帧；循环进字节码后由循环体的参数槽取代 |
| `src/stdlib/std/nvm/nodevm.cpp` `evalMarkedOperator_*` | `:1042`–`:1170` 前后 | nvm 侧同一套 C++ 元素循环（`map :1042`、`filter :1069`、`reduce :1102`、`foreach :1133`、`isolate :1158`）；nvm 是"在解释器里再解释一遍" |
| `src/stdlib/std/fvm/compile.cpp` `markOpCodeOf` | `:61`–`:80` | 把"元素调用"算子编成 `MarkOpCode`；`:587` 起是 `SCHD` 的落地 |
| `src/stdlib/std/fvm/jit/backend/x64/x64_backend.cpp` | `:471-472`、`:1698-1704` | `SCHD` 不可编：链接期遇到就整图回退；已编图里遇到 `SCHD` 也 `emitExitToDriver(Interp)` 交回解释器。**这就是"JIT 没有可编的循环"** |

`task-429`（运行期造循环图）在仓库里**没有对应代码点**——它没落过一笔，不需要删，只需要不再做。

## 2 替换后的形状

循环机制已经在仓库里，缺的是让 `map` 用它：

- 字节码：`LBEG / LTST / LEND`（`src/stdlib/std/fvm/bytecode.h:46-52`），第三条挂着 `ExecLoop` 指针。
- 循环事实：`camel::runtime::ExecLoop`（`src/kernel/include/camel/runtime/exec_plan.h:72` 起：`iterates`、`hasCond`、`collectionCount`、`stateComponents`、`stateCount`）。这套事实是 `loop` 表达今天就在用的。
- 解释器：`gotovm.cpp:416 / :426 / :439` 三个 handler 已经在跑循环；JIT：`x64_backend.cpp:257` `loopRoundsOf` 把一轮定成 `LoopRound`，`:313-336` 判"能不能原生"，能就一轮原生、不能就逐轮 helper。**两个后端消费的是同一份字节码循环。**

目标形状，按落地次序：

1. **前端**：`map` / `foreach` 不再编成"标了 `SCHD` 的算子调用"，而编成循环区间——入口 `LBEG`、条件 `LTST`、体 `LEND`，循环状态槽放下标与结果数组的写指针；结果数组仍在该 `map` 调用处分配（`execPlan` 的 `collectionCount` 已能表达"这一轮收集了一个值"）。
2. **元素核**：被调用的函数值是已知图（`FUNC` 常量）时，把该图的体**内联**进循环体，跨图帧布局合并成一张帧；内联不了时，循环体里留一条 `CALL`（此后仍是字节码循环，不再是 C++ 循环，两个后端都编/解释这一段）。
3. **两个后端同一机制**：`std::fvm` 走 `gotovm.cpp` 的 LBEG/LTST/LEND；`std::jit` 走 `loopRoundsOf → LoopRound`。**没有任何一条路径再进入 `evalMarkedOperator_map_arr`**，即第 1 节那张表整张消失。
4. **`SCHD` 的去留**：`SCHD` 若还有别的用途（设备放置、故障隔离节点），另开一格处理；本件不留"一半走循环、一半走 SCHD"的中间态。

语义由循环区间的既有事实承载：元素顺序、下标、核内的 `sync`/异常先后、结果数组的分配时机，都必须是 `ExecLoop` 已表达的那几件事，**后端不得各自补语义**。

## 3 比对语料与判据

**用例**（`test/cases`，先按目录列，逐个在实现期点名）：

- `test/cases/compile/comprehension_context/*`（`annotated.cml` 等）
- `test/cases/compile/constas/*`（`constas_column.cml`、`constas_forward_rest.cml`）
- `test/cases/compile/expected_type_loop/loop_parts.cml`
- `test/cases/compile/extents/computed_*.cml`（含 `computed_call`、`computed_runtime_stride`）
- `test/cases/compile/range_shared_names/*`、`test/cases/compile/literal_context/*`
- `foreach` 用例在实现期按 `test/cases` 里 `foreach` 的出现位置补点名（本页不预设）

**计划**（与上面目录对齐，`test/plans/feat` 下）：`compile/comprehension_context`、`compile/generic_join`、`compile/higher_order_clause`、`compile/prelude_signatures`、`compile/range_shared_names`、`compile/literal_context`；外加**一条计划之外的回归**：`fib` 三后端同表（`std::jit` 对 `std::nvm` 的 3.1× 不退）。

**"三后端输出逐字节相同"怎么取**：每个语料程序独占一个目录（同目录多 `.cml` 会让报错指到别的文件——已作为工具缺陷入账）；分别跑 `std::nvm`、`std::fvm`、`std::jit` 各一次，stdout 落三个文件，比较 SHA-256 全等。红表沿用 `internal/reports/2026-11-28-promotion-81-reds.tsv`，按 `plan_id + 用例名` 逐名比，新增必须为 0。

**`290 ns/元素` 在新机制上预计怎么变**：不预测数，只定测什么。同一 `loop12` / `closure8` 程序、同一次构建、同一会话、交替 3×3，报 `[load]` 与 ns/元素；观测点是"元素成本里不再有 site 查表与每元素调用入口这两段"（今天合计 549/585 ns），以及循环本身的开销——后者由第 0 节那三格 `loop` 对照（今天 nvm/fvm/jit = 2092/5011/3647 ns/迭代）单独记。判据仍是 `fvm ≤ 2 × nvm` 的同形状口径，`closure` 用同形状的 nvm 基线现量。

## 4 状态

- 枝：`lane/fvm-element-frame`，停在 `d1418ea52f`，本页是它上面唯一一笔（文档），`dirty 0`，不 push。
- 帧复用（`task-448` 方向）不继续做；它的读数在本页第 0 节，作为本件第一笔的材料。
## 5 借自被取代补丁枝的两条读数（④ 要覆盖的证据）

**（a）元素路径今天是一次一次的原生入口，不是循环。** 被取代的 `lane/fvm-element-call-count`（`task-428`，`f70f269cc8`，7+/1-）唯一一笔是把 `callElement` 里的 `invokeCallOrJit` 补上一次 `noteIndirectCall(site.runtimeGraph)`。它记的是一条证据而不是解：元素路径（`fastvm.cpp:497` `callElement`，`:500` 的 `invokeCallOrJit`）每元素都是一次入口，而 `CALL` 字节码那条路才计数（`gotovm.cpp:553`；JIT 侧的同一处是 `fastvm.cpp:1200`）。元素路径原来不计数 ⇒ tier policy 永远看到默认 0，**阈值再低也跨不过一个不动的数**，kernel 在 `std::jit` 下整轮保持解释。④ 要覆盖的正是这一点：今天两个后端在同一个 `map` 形状上是"每元素一次入口"的两套路径，而不是同一段循环。

**（b）源级路线量过了，关掉：`map` 不能在 `.cml` 里改写成收集循环。** 把 `map` 写成 Camel 收集循环的三种形状，`std::fvm`、同一构建：

| 形状 | 30k | 60k | 120k |
|---|---|---|---|
| `loop out = [] as int[] for x in xs do out->append<x>` | 4667 ms | 20297 ms | 79196 ms |
| `var out`，循环体 `{ out = out->append<x>; acc + 1 }` | — | — | 83278 ms |
| `loop out = [], n = 0 ... do (out->append<x>, n + 1)` | — | — | 95952 ms |

30k→60k 4.3×、60k→120k 3.9×：**二次**。12M 元素上第一条 >600 s（同形状的内建 `map` 是 ~7 s）。原因是 `append` 是 `#op(':struct/append_arr')`（`prelude.cml:1318`，表达式位置每轮造一个新数组），而 `builder_control.cpp:4657` 的收集槽要求：空数组初值、该状态的下一值本身就是 `append` 算子、`append` 的唯一普通输入是循环体的那个端口、端口只有这一个用处、且循环体不直接返回状态值（`:4661`、`:4712-4718`）。源级能写出的形状都不满足它。

⇒ 第一刀因此落在 **GIR 的循环构造处**（`builder_control.cpp:3942-3956` 的 `addRegionLoopNode` + `attachLoopParts`，收集槽由 `collectLoopAppends` 在同一处命中），不是改 `prelude.cml`。第二刀（核内联）之外，这是唯一还缺的东西。
**更正：源级收集循环只在 `Range` 上不是候选，在数组上是线性的。** 上表三组与 `var` 状态形（120k 83.3 s）、元组状态形（120k 96.0 s）**都不是数组迭代**：`for x in 0 .. N` 走的是流循环（`buildStreamLoop`，GIR 里 `value_type=(int.., bool, int[])`），收集槽不在那条路上，所以每轮 `append` 造新数组、二次。换成数组迭代 `loop out = [] for x in xs do out->append<x * k>`（`xs: int[]`）就是线性的：**120k 198 ms、1.2M 1477 ms**（10× 元素 7.5× 时间）。⇒ 第一刀若走 `visitComprehension`，`Range` 源必须先 `to_array`，数组源直接迭代。
## 6 `D-1128-194`：语义已定，`map` 的 lambda 定型按位置走

决策者判：**不在 unifier 里记"变量→变量"，也不建带未解类型变量的实例**。依据是设计原则里已经写下的这条（原文，`docs/design-principles.cn.md` 5.1 末、第 375 行逐字）：

> **期望类型沿类型变量传进调用**：先把被调函数的返回类型与期望类型对齐，解出类型变量；实参里提到这些类型变量的位置代入后成为该实参的期望类型，字面量据此取类型；已经取好类型的值不转换。字面量一次舍入到目标类型。泛型函数体里的字面量在实例化时取类型，放不下就在实例化处报 `LiteralNotRepresentable`。

以及 lambda 是什么（同文件 4.4、第 335 行逐字）：

> **lambda 写成 `func (参数) { … }`**（负责人 2026-11-23）：lambda 就是去掉名字的函数声明，`func (x) { x * x }`、`func (acc: int, x: int) => int { acc + x }`，与 `func 名字(…) { … }`、`rule 名字(…) { … }`、`edit d = f { … }` 同一个样子，关键字后留空格。花括号永远是函数体，最后一条语句就是它的值；要交出结构体就再套一层（`func (x) { {a: x} }`）。

⇒ `map(xs, f)` 里 `xs` 先定型、`T` 解成 `int`，lambda 再按 `(item: int) => U` 定型：既不需要延迟实例化，也不需要环区构造。（"显式类型实参"是 5.9，本件不涉及。）

### 6.1 量到的那一件（`internal/tools/probe-map-lambda.ps1`，探针程序在 `D:\Temp\bench6`）

把 `prelude.cml` 的 `map` 从 `#op(':mark/map_arr')` 声明改成带函数体的收集循环（`loop out = [] for x in xs do out->append<f(x)>`），`BUILD_EXIT=0`，然后跑三个程序（`std::fvm`，load 23%；驱动第一行自报三个探针的 SHA-256-16）：

| 程序 | SHA-256-16 | 结果 |
|---|---|---|
| `xs->map<func (x) { x * 2 }>`（不写类型，`xs: int[]`） | `456813FC8E985387` | **不能**：`SemanticDiag::ImportNameNotExported` —— `Reference 'map' is declared as imported but is not exported by its module.`，位置 `main.cml:3:18` |
| `xs->map<func (x: int) { x * 2 }>`（写了类型） | `1AF62945E8EA24E4` | **同上**（同一条诊断、同一位置） |
| `[x * 2 for x in 0 .. 3]`（推导式对照） | `073B0F9BA4FC148F` | **不能**：`SemanticDiag::GenericFunctionAsValue`，`type variable(s) $element0 cannot be determined`，位置 `main.cml:2:19`，签名 `(x: $element0) => <null>` |

读法：**去掉 `#op` 之后 `map` 连导出都没了**（两条手写调用在同一条诊断上失败，与 lambda 写不写类型无关），所以"手写不写类型 lambda 能不能定型"这一问在这棵树上还问不到——前置问题变成"`map` 去掉算子声明后为什么不导出"。推导式那条则仍然报 `$element0`：**推导式的降低确实给 lambda 造了自己的 `$element0` 类型变量**（`builder_data.cpp:382-401`），与手写调用无关。