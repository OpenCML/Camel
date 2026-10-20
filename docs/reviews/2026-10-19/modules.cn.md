# 模块区设计评审（fsh-dev，2026-10-08 之后的提交）

> 本文写于设计原则按主题重写之前。文中的"2.9""2.13""第 6 节"等是当时设计原则的节号；这些条文现在收在 [设计原则裁定记录](../../design-principles-log.cn.md)，旧编号到新编号的对照见该文件开头的表。

范围：`modules/tiles`、`modules/tensor`、`modules/onnx`、训练基础设施触及模块的部分（tree、Device、Bytes、checkpoint、类型实参）。只看"长期是否该这样设计"，不看能否运行。每条都在代码里核对过；行号以 fsh-dev 当前检出为准。

判定依据：设计原则 1–2、5（尤其 2.9 标准库边界与模块接口、2.12 图与效应、2.13 数值契约与定义图、2.16 训练与后端默认放宽）；`docs/design/tiles-route/README.cn.md` 第 2、3、10 节。

---

## 高

### 1. 参数树操作在核心建图器里按 URI 硬编码展开

- 位置：`src/compile/gir/tree.cpp:51-60`，`src/compile/gir/builder.cpp:5676-5678`，`modules/tree/module.cpp`（23d97db7）
- 现状：每一次对算子组的调用，建图器都先问一句"是不是树操作"：

  ```cpp
  const std::string uri = ops->resolvers().front().first;
  if (uri != "tree:tree_map" && uri != "tree:tree_reduce" && uri != "tree:tree_unzip" &&
      uri != "tree:tree_paths" && uri != "tree:tree_leaves")
      return nullptr;
  ```

  `tree` 模块本身只是个空壳：它导出的五个算子的类型解析器一律返回 `std::nullopt`（"The compiler resolves callbacks separately for each leaf"），在 `load()` 里登记的纯度也永远用不上，因为这些节点从不出现在图上。真正的语义（叶子边界、字段顺序、路径写法 `a.b.0`、`unzip` 的元数）全部写在核心的建图器里。
- 为什么不对：和 `@numeric` 是同一类问题：核心按字符串认一个具体模块、用一张封闭名单、绕过已有机制（算子解析器、泛型实例化）。今后加 `tree_flatten`、`tree_zip`、`tree_count` 都得改核心建图器。`tree_map` 也不能当函数值传递（解析器恒失败）。2.9 说"建立在原语之上的组合逻辑用 Camel 写"，2.16 说"操作在编译期展开为逐叶的普通节点"，后者说的是结果，不是说展开逻辑要进核心。
- 优雅的形式：二选一。（甲）核心提供一个通用的"编译期展开器"注册点：模块为自己的算子登记一个展开函数（输入实参类型与节点句柄，产出普通节点），建图器只认"这个算子有展开器"，不认名字；`tree` 模块用它实现五个操作。（乙）让类型层能对结构体/元组按字段递归（类型函数），`tree_map` 等写成 Camel 的泛型库函数。
- 严重度：高
- 需要语言作者裁定：要。问题："`tree_map` 这类按类型展开的操作，是做成'C++ 模块登记编译期展开器'的通用机制，还是等类型函数能遍历结构体字段后用 Camel 写？例如 `tree_map<adam_step>(params, grads, m, v)` 将来由谁展开——核心建图器、tree 模块的 C++ 展开器，还是 `tree.cml` 里的泛型函数？"
- 归属：核心。

### 2. Tiles 直接读 tensor 模块的内部注册表，纯度与结果类型有两个来源

- 位置：`modules/tiles/src/compiler.cpp:702-738`，`modules/tiles/src/module.cpp:26,476`，`modules/tiles/CMakeLists.txt:6`（`EXTRA_LIBS tensor_cmo`）
- 现状：

  ```cpp
  const auto *def = tensor::ops::OpRegistry::instance().find(id);
  if (!def || !def->traits.pure)
      throw UnsupportedComputation("unsupported or effectful operator '" + uri() + "'");
  ...
  auto inferred = knownResult ? ... : def->infer(tensor::ops::InferContext(types, constants));
  Node n{.op = std::string(def->name), .shape = *result->shape()};
  ```

  另外 Tiles 构造函数里调用 `tensor::ops::registerTensorOps()`，替 tensor 模块初始化它的私有注册表；还用到 `tensor::ops::constArgOf`、`tensor::ops::resultAllocator`。
- 为什么不对：纯度在核心 `OperatorTable::isPure` 有一份（`graph_regions.cpp:89` 读的就是它），这里又读 tensor 私有的 `def->traits.pure`；结果类型在核心有 `OperatorTable::resolver(id)`，这里又走 tensor 的 `def->infer`；算子的身份在核心是 `OperId`，这里又以 `def->name` 字符串进入 Tiles 私有表示。一个模块深入另一个模块的内部头文件，违背 2.9"模块接口：整理出一层公开的 C++ 接口，其余内部头不作承诺"，也违背路线第 2 节"Tiles 的输入必须完全来自图上的语义与事实"。tensor 改内部结构，Tiles 会静默失配。
- 优雅的形式：Tiles 只经核心读：纯度 `OperatorTable::isPure`，结果类型 `OperatorTable::resolver(id)->resolveWith(...)` 或图上已有的节点类型，常量实参用核心的静态实参接口；算子标识只用 `OperId`。tensor 若有确需公开的东西（张量对象布局、`asTensorType`），放进一个声明为公开的头文件。
- 严重度：高
- 需要裁定：不需要。
- 归属：Tiles（改用核心接口）；tensor 一侧需整理公开头（核心团队）。

### 3. Tiles 私有表示以字符串算子名为键，四五张封闭名单互不一致

- 位置：`modules/tiles/src/graph_regions.cpp:40-66`（区域资格表，20 个算子）；`compiler.cpp:820-821`（`"|neg|abs|exp|log|sqrt|rsqrt|relu|"` 等）；`schedule.cpp:55-62`（同样两串，再写一遍）；`scalar_expr.cpp:484-517`（名字到标量运算）；`loop_kernel.cpp:26-38`；`cuda.cpp:45`；`reduction_kernel.cpp:99-108`；`row_kernel.cpp:57-84`
- 现状：`Node.op` 是 `std::string`，运算种类靠名字判断，同一份名单散在多处。几处名单已经不一致：编译器能降低 `transpose`、`permute`、`slice`、`reshape`、`broadcast_like`、`expand_axis`、`sum_to` 的批量矩阵乘等，但 `std::tiles` / `@gpu` 静态路径的资格表 `operations()` 没有这些布局算子，矩阵乘还限定秩 2（`graph_regions.cpp:138-141`），于是区域在每个转置处被切断。循环体的资格却是"让编译器试一次"（`loopEligibility`），普通节点是另一套。归约更典型：

  ```cpp
  const bool maximum = n.op == "max_axis", mean = n.op == "mean_axis" || n.op == "mean_all";
  require(law.kind == (maximum ? sem::LawKind::Max : sem::LawKind::Add) &&
          law.accumulator == (maximum || n.op == "sum_to" ? f32 : f64) && ...,
          "unsupported numerical law");
  ```

  即先按名字决定该做什么，再要求核心声明的归约律与之完全相等。
- 为什么不对：语义的来源是核心声明（索引实例、归约律、数值类），Tiles 却以名字为准、拿核心声明来"对答案"，等于两份事实来源。加 `min_axis`（核心已有声明与律）要改 Tiles 好几处；核心改了某个律的累加类型，Tiles 不是自动跟上而是拒绝。路线 T4 要求"私有表示改为'读核心语义 + 物理计划'"。
- 优雅的形式：Tiles 节点以 `OperId` + 核心索引实例为身份；逐元素与否看实例（单一 `Unique` 写、各输入仿射访问），标量运算从一张"`OperId` → 设备标量运算"的覆盖表取（全模块唯一一张）；归约内核以归约律为参数生成（律的种类、累加类型、初值位、收尾、空域策略都从律读），名字只出现在覆盖表里。区域资格统一为"让编译器对单个节点试一次"，与循环一致。
- 严重度：高
- 需要裁定：不需要。
- 归属：Tiles。

### 4. 梯度与融合产生的算子没有语义声明与定义图，Tiles 只好照 CPU 内核复刻

- 位置：`modules/tiles/src/compiler.cpp:705-727`（`permute_inverse`）、`773-781`（`ones_like`/`zeros_like`）、`783-818`（`matmul_grad_lhs/rhs`）；`modules/tensor/passes/fuse.cpp:133,177-198`；`modules/tensor/ops/semantics.cpp`、`indexing.cpp:799-889`；`stdlib/tensor_definitions.cml`（只有 5 个定义）（d14707d6、fc8fbd7b）
- 现状：Tiles 自己算逆置换、自己把矩阵乘梯度拆成 reshape/transpose/matmul/sum_to，并注明：

  ```cpp
  // Match the Tensor CPU gradient's canonical GEMM: a shared RHS
  // accumulates over batch and row in one ascending FMA chain.
  ```

  ```cpp
  // These source operators do not yet declare index instances.
  ```

  tensor 一侧：`permute_inverse`、`matmul_grad_lhs/rhs`、`ones_like`、`zeros_like`、`cast_like` 既无索引声明也无定义图；`tensor::fuse` 造出的 `tensor:matmul_add`、`tensor:matmul_add_relu`、`tensor:matmul_blas` 也没有任何语义记录（`semantics.cpp`、`indexing.cpp` 均无）。核心早已提供定义图与 `expandDefinition`（b0c0bf8d），但全仓库没有任何后端或遍调用它；Tiles 的覆盖表里也没有 `softmax`、`sigmoid`，写 `tensor::softmax` 的程序在 `std::tiles` 下整段留在 CPU。
- 为什么不对：2.13 说"定义图是规范：融合或手写的实现必须与算子的定义图一致"。现在梯度算子的规范只存在于 CPU 内核的实现里，Tiles 照着内核反推，是第二份来源；`tensor::fuse` 之后图上出现对所有遍都不透明的新算子，等于在图上丢语义（`matmul_blas` 只是"换个实现"，却换了算子身份）。
- 优雅的形式：（a）tensor 为梯度算子与融合算子写定义图（如 `matmul_grad_rhs = sum_to(transpose(flatten_rows(x)) @ flatten_rows(dy), w)`、`matmul_add = add(matmul(a, b), c)`）并补索引声明；（b）Tiles 对没有覆盖的算子先 `expandDefinition` 再编译，覆盖面随定义图自动增长；（c）"用库 GEMM"这类实现选择不换算子身份，作为节点上的物理属性或由执行时按许可选实现。
- 严重度：高
- 需要裁定：（c）要问："`tensor::fuse` 把 `matmul` 换成 `matmul_blas`/`matmul_add` 时，图上应保留原算子（实现选择是物理层的），还是允许出现新算子、但新算子必须带定义图？"
- 归属：tensor 定义图与声明是核心团队的；Tiles 改用定义图是 Tiles 的。

### 5. `std::tiles` 插入的区域与搬运节点被声明为外部效应

- 位置：`modules/tiles/src/module.cpp:622-688`；`src/core/operator_table.cpp:146-148`
- 现状：`tiles:retain_preferred`、`tiles:run_preferred_resident`、`tiles:download` 等都是 `{.pure = false}`，`run`/`upload`/`download` 等的资源声明里带 `.externalEffect = true`；`retain_preferred`、`run_preferred_resident`、`run_host_resident` 没有资源声明，按 2.9 也按"有副作用"对待。核心 `hasExternalEffect` 对它们返回真，`std::opt` 的折叠、CSE、代数遍都以 `isPureOper` 为前提跳过它们（`src/passes/opt/generic/rewrite.cpp:58-64`、`generic.cpp:721,845`），`LOOP` 的效应摘要也会把含区域的循环体记为外部效应（`src/runtime/loop.cpp:77`）。
- 为什么不对：路线 3.3 明定 `:tiles/exec` "是一个带 `callsGraphs` 声明的普通算子，语义就是调用 R……这使 `std::tiles` 成为一个纯粹的性能遍"。现在经过 `std::tiles` 的纯计算在图上变成了效应，后续遍对它束手无策，图的效应摘要也随之改变；`@gpu` 的函数同理。
- 优雅的形式：区域调用声明为纯 + `callsGraphs`；设备值按资源协议声明为"分配一个外部资源、只读输入的设备资源"，不是外部效应；驱动初始化这类环境状态若必须声明，用不影响纯度的环境读声明。
- 严重度：高
- 需要裁定：要。问题："`std::tiles` 改写后，`let a = f(x)` 与 `let b = f(x)` 两次区域调用，`std::opt` 能否合并为一次？即上传、区域执行、下载是否算纯计算（设备内存是不可观察的分配），还是外部效应？"
- 归属：Tiles（声明）；若裁定为纯，核心资源协议可能要补"分配但无效应"的写法。

---

## 中

### 6. "哪些许可允许哪种实现"分散在各模块各写一份，2.16 的后端默认放宽与安全模式都没有落点

- 位置：`modules/tensor/passes/fuse.cpp:177-178`（fc8fbd7b）；`modules/tiles/src/schedule.cpp:411-415,523-528`（d14707d6）；`src/execute/pass.cpp:632-668`（294fe990）；`modules/tiles/src/capability.cpp:17`
- 现状：

  ```cpp
  constexpr uint8_t kLibraryGemmPermissions = kReassociate | kChangePrecision;   // tensor::fuse
  constexpr auto mixed = kReassociate | kChangePrecision | kFiniteMath;          // Tiles cublas_bf16
  ```

  两个模块各自决定"库 GEMM / BF16 GEMM 需要哪些许可"，理由各写在注释里。2.16（10-18）规定后端默认可以改变归约与 f32 库矩阵乘的累加顺序，并"提供安全模式参数"；目前 CPU 侧仍要求显式 `reassociate|change_precision`，Tiles 没有任何遍参数（`RegionPass` 不收参数），设备号靠环境变量 `CAMEL_TILES_DEVICE`。核心的遍参数解析只认 `std::opt` 与 `std::opt::algebra` 的 `safe_fp` 一个键（`pass.cpp:644`），模块遍没有接参数的路。
- 为什么不对：许可的含义只能有一份解释，否则同一个 `@numeric(...)` 在 CPU 与 GPU 上放开的东西不同；2.16 的默认放宽与安全模式需要一个统一的判断点。2.7 已说"给遍传参数是迟早要实现的通用机制"，现在是为 `std::opt` 单开的硬编码解析。
- 优雅的形式：核心提供 `sem::permits(declaration, nodePermissions, deviation, policy)`：实现声明它偏离规范算法的方式（改合并顺序、输入降精度、乘加分开舍入、特殊值假设），`policy` 是后端默认放宽或安全模式；`reorderFreedom` 已是这个方向的先例。遍参数做成通用机制（`registerModulePass` 的工厂接收解析好的参数表），`std::tiles(safe=true, device=1)`。
- 严重度：中
- 需要裁定：要，两问。①"按 2.16，CPU 上的 f32 CBLAS（不改精度、只改累加顺序与乘加的舍入方式）默认可用吗？乘加分开舍入（相对规范的 FMA 链）算'累加顺序'还是要 `change_precision`？"②"BF16 输入的 GEMM 是否必须同时有 `finite_math`？例如 `@numeric(reassociate, change_precision)` 下的 `x @ w` 能否用 BF16？"
- 归属：核心（判断函数、遍参数）；两个模块改为调用它。

### 7. `@gpu` 在运行时做整图改写，并与 `std::tiles` 是两条路径

- 位置：`modules/tiles/src/dispatch.cpp:14-77`；`region_outline.cpp:84-91`；`module.cpp:376-389,656-659`；README"Partitioning happens once when the decorated function value is built"
- 现状：`@gpu` 走通用装饰器机制（核心没有特例，这一点是对的），但 `tiles:gpu` 声明为非纯，`std::macro` 不会提前求值；装饰在运行时发生，`gpuFunction` 当场调用 `outlineRegions` 改写整张可达图、发布新图、编出模板。为此加了进程级锁：

  ```cpp
  // Serialize Tiles rewrites (not kernel execution) until that registry supports
  // concurrent publication.
  static std::mutex rewriteMutex;
  ```

  抽不出静态区域时又退回另一条路径：包一层 `tiles:prefer_gpu`，在调用时按实际值整函数特化，失败则整函数回 CPU（路线 10.2 第 1 条要求退回的粒度是区域）。README 也承认局部函数反复装饰会让事实库不断累积模板。
- 为什么不对：图改写是遍的职责（第 4 节），在算子里改图需要绕过核心图注册表的并发限制；同一意图（"这段希望上 GPU"）有装饰时改写、调用时特化、`std::tiles` 遍三条实现。这正是 `@numeric` 正在改正的形态：标注应当只是给节点或函数加属性，由遍读取。
- 优雅的形式：`@gpu` 做成库装饰器，只给函数（或其节点）打上通用属性"偏好设备"；`std::tiles` 读这个属性（没写 `std::tiles` 时由执行入口自动加一次同样的遍），区域切分只在遍里做一次。`prefer_gpu` 整函数路径并入区域路径。
- 严重度：中
- 需要裁定：要。问题："`@gpu` 的含义是'调用这个函数时希望在 GPU 上'（运行时行为），还是'编译时给这个函数的节点标上设备偏好、由 `std::tiles` 处理'？例如没有在命令行写 `std::tiles` 时，`@gpu` 函数是否仍应走 GPU？"
- 归属：Tiles 实现；属性机制与 `@numeric` 重做共用，是核心的。

### 8. 参数树的"叶子"定义散在七处，规则已经不一致

- 位置：`src/compile/gir/tree.cpp:26-49`；`modules/nn/src/checkpoint.cpp:57-78`（13d4152a）；`modules/tiles/src/device.cpp:71-105`（1b724238）；`tree_transfer.cpp:27-60`；`graph_values.cpp:28-55`；`module.cpp:401-450`；`compiler.cpp:139-157`
- 现状：`tree.cpp` 与 `checkpoint.cpp` 的 `internal / sizeOf / fieldType / fieldName / pathAt` 是逐行复制（只改了名字），路径写法 `a.b.0` 在两处各定义一次。`device.cpp`、`tree_transfer.cpp` 排除 `T?`、`T!` 与单元格（`isTagged() || isCell()`），而 `graph_values.cpp` 的 `buildResidentType`、`module.cpp` 的 `supportedLeaves` / `acceptsInput`、`compiler.cpp` 的 `host()` 只看 `TypeCode::Tuple`，会把 `Tensor<f32>?`（一元素的带标记元组）当作普通元组树接受。
- 为什么不对：2.16 把叶子的顺序与边界定为语言层的约定，现在每个使用者自己实现，已出现分歧；`@gpu func f(x: Tensor<f32>?)` 能通过 `supportedLeaves` 的签名检查，而 `T?` 为 null 时是空槽，按元组去取字段没有依据。
- 优雅的形式：核心提供一个 C++ 的参数树视图（判断内部节点、按字段声明顺序遍历、给出路径），建图器的树展开、`Device<T>` 的分配、Tiles 的搬运、检查点都用它。
- 严重度：中
- 需要裁定：不需要（2.16 已定）。
- 归属：核心提供视图；Tiles 与 nn 改用。

### 9. 设备故障按名字判断、报告时没有故障种类与源码位置

- 位置：`modules/tiles/src/cuda.cpp:43-46,311-318`；`cuda_submission.cpp:174-176`；`compiler.cpp:837-838`；`reduction_kernel.cpp:109`；`module.cpp:50-57`
- 现状：

  ```cpp
  return node.op == "scalar_div" || (node.loop && programMayFault(node.loop->body));
  ...
  throw std::runtime_error("division by zero at Tiles node " + std::to_string(node));
  ```

  再经 `checked()` 统一包成 `RuntimeDiag::RuntimeError`，消息是 Tiles 私有节点号。空轴求最大、空域求均值在编计划时抛 `std::runtime_error`（"max over an empty axis"、"empty reduction domain"），也成了通用运行错误。`Program::Node` 没有保存源节点的位置。
- 为什么不对：2.9 要求故障带位置终止；路线 3.5 要求"位置指向区域里出故障的那个源节点"；路线 10.4 已点名 Tiles 的空张量诊断要与核心统一。核心有 `FaultSpec`（故障种类、触发的操作数）与归约律的 `LawEmpty::Fault`，Tiles 没有读。同一程序有没有 GPU，故障的种类与位置不同。
- 优雅的形式：节点是否可能故障、故障种类读核心声明（`FaultSpec`、`law.empty`）；`Program::Node` 带源节点的来源（`graph->nodeOrigin`）；设备故障字写入后按该来源报核心的故障种类。
- 严重度：中
- 需要裁定：不需要。
- 归属：Tiles。

### 10. BF16 cuBLAS 路径自己推导广播的批偏移

- 位置：`modules/tiles/src/mixed_gemm.cpp:33-46,146-188`（d14707d6）；`docs/technical/ddpm-primitives.md`
- 现状：严格矩阵乘内核按核心访问映射寻址（`matmul_kernel.cpp:143-174` 用 `affineAddress(*n.indexing, ...)`），BF16 路径却按连续行优先布局直接给 cuBLAS 传指针，非等批广播用手写的 `batchOffset`（"Map an output batch ordinal to the broadcast operand's matrix ordinal"）。文档写"other broadcast patterns use the core batch mapping"，与代码不符。
- 为什么不对：路线第 2 节与 4ea9b8cd、0754afb4 的方向是"访问映射只来自核心"；这里重新实现了一遍广播规则，核心的矩阵乘索引实例改了（比如允许带步长的视图进入）BF16 路径不会跟上。
- 优雅的形式：从索引实例算每个批的基址与行步长（或用 `rowMajorView` 求出视图后判断能否交给 cuBLAS，不能就不选这个实现）。
- 严重度：中
- 需要裁定：不需要。
- 归属：Tiles。

---

## 低

### 11. 核心的遍别名表里写死了 Tiles

- 位置：`src/execute/pass.cpp:430-431`（849103e9）
- 现状：`{"std::tiles", "tiles::compile"},  // The implementation and executor remain in the bundled Tiles module.`
- 为什么不对：核心按名字认一个模块。别的模块（将来的 CPU 向量化后端、`onnx::export`）想要 `std::` 下的名字也得改核心。
- 优雅的形式：模块在注册遍时可以声明一个 `std::` 别名（受核心审核的名单可以放在配置里），或者干脆保留 `tiles::compile` 的写法，由 2.7"命令行上写模块的遍时自动加载"兜住。
- 严重度：低
- 需要裁定：要，轻量。"`std::` 前缀是否只给核心自带的遍？模块的遍要进 `std::` 时由谁登记？"
- 归属：核心。

### 12. 原生算子的静态结果类型靠每个后端各自塞进 `ArgsView`

- 位置：`include/camel/core/operator.h:54-62`（13d4152a）；设置处 `nodevm.cpp:1142`、`fastvm/native_call.cpp:23`、`taskflow.cpp:1939`、`opt/generic/generic.cpp:745`、`onnx/exporter.cpp:2508`；读取处 `modules/nn/src/checkpoint.cpp:332`
- 现状：`ArgsView` 新增可变字段 `resultType_`，每个调用原生算子的地方都要记得 `setResultType`；`modules/tensor/passes/index_eval.cpp:419` 的 `ArgList` 就没有设。`load_tree` 找不到时报"backend did not supply the static checkpoint result layout"。
- 为什么不对：这是一条"每个后端都必须遵守、漏了只在运行时发现"的隐式协议；类型实参在编译期已经由 `resolveForCall` 消化，结果布局本可以作为节点的静态实参在建图时固定下来。
- 优雅的形式：解析器在建图时把需要的类型信息变成该节点的静态 with 实参（或由核心的统一原生调用入口从节点类型填写，后端不再各自负责）。
- 严重度：低
- 需要裁定：不需要。
- 归属：核心。

### 13. Tiles 的编译缓存键手工序列化核心的索引实例

- 位置：`modules/tiles/src/codegen_cache.cpp:28-120`
- 现状：`Key::indexed()` 逐字段写出 `IndexedInstance` 的表达式、维、参数、谓词、访问、律序号；核心头文件里没有这类结构的规范哈希。
- 为什么不对：核心给 `IndexedInstance` 加字段，Tiles 的键会静默漏掉它，造成错误命中；路线 3.4 说缓存键应是"区域的语义指纹"，指纹应由核心给出。
- 优雅的形式：核心为 `IndexedInstance`、`ReductionLaw` 提供规范的哈希/序列化（定义图已有 `contentHash` 的先例），Tiles 只拼接物理层的部分。
- 严重度：低
- 需要裁定：不需要。
- 归属：核心提供，Tiles 改用。

### 14. ONNX 导出器自行重述切片越界的规则

- 位置：`modules/onnx/lowering.cpp:383-460`（363096eb）
- 现状：`-n <= b <= n` 的合法范围与"负数从末尾数"的规则写在导出器里；核心的切片索引声明（`indexing.cpp:359-406`）只能表达常量边界，运行时边界的故障规则只在内核 `normalizeSliceBounds` 里。
- 为什么不对：故障条件有两份（内核与导出器）。思路本身（推测执行的臂里拒绝、用 Gather 守卫让 ONNX 同样失败）是好的。
- 优雅的形式：切片的故障条件作为声明的一部分（`FaultSpec` 加上以操作数表示的合法域），导出器按声明生成守卫，对 `gather`、下标等同类算子通用。
- 严重度：低
- 需要裁定：不需要。
- 归属：核心（声明能力）；ONNX 随后改用。

### 15. math 模块的算子没有数值声明

- 位置：`modules/math/module.cpp`（d14707d6 为 `math:sqrt` 加了 VJP）；全仓库只有 `modules/tensor/ops/registry.cpp` 调用 `declareSemantics`
- 现状：`math:exp`、`math:log`、`math:sqrt` 等没有数值类与 ulp 上界声明。
- 为什么不对：2.13 要求超越函数逐个声明误差上界、开方声明为正确舍入；Tiles 将来降低标量 math 调用、或 `std::opt::algebra` 判断能否改写时，没有依据。
- 优雅的形式：与 tensor 一样，在 math 模块里集中写一张声明表。
- 严重度：低
- 需要裁定：不需要。
- 归属：核心团队（math 模块）。

---

## 检查过、设计上没有问题的地方

- `@gpu` 通过核心通用的装饰器机制接入，核心建图器里没有 `gpu`/`tiles` 的特例（全 `src`、`include` 只有第 11 条的别名）。
- Tiles 读节点上的数值许可（`graph.nodePermissions`），只在 `contract` 下收缩乘加；NVRTC 的 `fmad/prec-div/prec-sqrt/ftz` 固定并进入缓存键。
- 索引实例经核心事实（`NodeInstance`）或 `core::sem::instantiate` 取得；视图地址经 `rowMajorView` / `affineAddress`；严格矩阵乘按核心访问映射与 k 递增 FMA 链。
- 并行求最大值经核心的 `reorderFreedom(law, permissions)` 判定，没有自行假设可重排。
- 循环经 `LoopView` 与核心的 `LoopDomain` 事实读取，没有自行识别 `range`（f0f3353e、0c21cd58）。
- 编译模板存在核心事实库（`registerKind`，`readsDeclarations = true`，按图修订与声明纪元失效），不是按源码文本缓存。
- 区域外提用 `RuntimeGraphDraftSession` 生成新图，图之间只有调用关系，没有跨图节点引用；遍经 `registerModulePass` 注册。
- Tiles 的设备资源（`tiles:device`、`tiles:driver`）用核心的资源声明协议登记（但纯度声明见第 5 条）。
- 异步提交、跨流依赖、租借内存池（370431f4、ca973d58）只在 Tiles 物理层内，不进入图，常量都是物理参数。
- `Device<T>` 按字段分配经核心通用的 `cloneWithParams` 返回 `Type*` 的钩子实现，核心没有认 `Device` 的名字。
- `Bytes` 是核心类型，`fs` 文本接口校验 UTF-8、二进制另走 `read_bytes`/`write_bytes`（dd0d6176），符合 2.16。
- `tensor::to_bytes` / `from_bytes::<T>` 走通用的显式类型实参（`resolveForCall`），没有另造 dtype 字符串接口（6e934aa3、1a5517ad）。
- tensor 的数值声明集中在一张表里，`argmax`/`argmin`、`exp` 次正规、int64 精确求和与声明一致；`tensor::fuse` 要求模式中各节点许可相同才融合。
- `math:sqrt` 的 VJP 走已有的 `DerivativeRegistry`，没有新机制。
- ONNX 的切片守卫跟踪"两臂都算再选择"的推测执行并在那里拒绝，没有用截断冒充故障（规则所在位置见第 14 条）。
- ONNX 对 `LOOP` 暂不导出并给出带位置的诊断，没有偷偷降为递归再恢复（已知的下一步）。
