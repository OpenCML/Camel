# G1–G3 的 `modules/` 与 Tiles、CUDA 施工顺序

状态：计划，随本分支合入（the change，基线 `origin/develop` `e0446c76e`）。本文只排**施工顺序**：
改什么、改哪个文件、拿什么证据、依赖谁、哪一步今天还是红的。设计与编号（G1–G7、S1–S10、§2.6 的
`autodiff::expand`、§10 五问）仍以 [GPU 设计稿](README.cn.md) 与 [G1–G3 设计计划](plan.cn.md) 为准；本文不
改那两个文件，冲突时以它们为准。

范围：G1–G3 里落在 `modules/tiles`、`modules/autodiff`、Tiles 的 CUDA 路径与相应 `test/` 的部分。
**不在本文**（另有归属，见 §6）：图的边与 `src/runtime/**`、`modules/tensor` 的算子与 `Tensor` 写法、
`stdlib/{optim,opt,rules}.cml` 与改写模块、`showcase/**`、`test/tools/**`。

---

## 1. 已经落地（按树核对，不要重做）

| 项 | 落点 | 证据 |
|---|---|---|
| 启动设备校验（S1） | `src/execute/devices.cpp`；`c9f3b41a9`「check the devices named by --devices before any graph runs」 | `test/plans/feat/modules/tiles.plan.toml` 的 `device_startup.*` 六条 |
| 删除 `@gpu`/`run`/`run_device` 与旧分派（Q5） | `da73546d3` | 全树只有历史文档还提到这三个名字，代码、用例、stdlib 里没有 |
| 一个键、两个标签（S2） | `src/core/tags.cpp`：`tiles.placement`（Symbol），`#gpu`/`#cpu` 两个 `Decl`，`readBy = tiles::compile`；`6d2648158` | `camel inspect --tags`；`placement_query.cml` |
| 放置解析与报告（S3） | `modules/tiles/src/placement.cpp`（`placementOf`/`bodyPlacementOf`）；`91e7f4ac9`；规则见 [placement.cn.md](placement.cn.md) | `TILES_PLACEMENT`；`placement_hash_{cpu,gpu}.cml` |
| 作用域拒绝（S4） | `region_outline.cpp` 的 `refuseGpuScopes`：`GpuScopeUnsupported` / `GpuScopeNoDeviceProgram`，一条不落到主机；`5b701e746` | `test/plans/gpu/tiles.plan.toml` 的 `nofallback.*` 七条 |
| `#cpu` 逃逸 | `ecb12f82d`；`gpu_scope_cpu_escape.cml` | 同上 `nofallback.cpu_escape_is_visible_in_the_source` |
| 形状泛型调用重试 | `1b97de680`（`run_preferred_resident` 先按类型模板、失败再按实参重试） | `nofallback.shape_generic_call_still_retries` |
| 区域/树传输合批 | `cf4285443` | note ⟨cf42854⟩；`tiles_regions.plan.toml` 的 `resident_graph_transfers` |
| 循环按图内区域读 | `graph_regions.cpp::analyzeRegions` 用 `LoopView::regionNodes` 把条件与循环体归给 LOOP 节点 | `tiles.plan.toml` 的 `static_loop_device`、`multi_state_loop_device` |

看板第 229、491 行仍写「S2 放置属性在跑」「API 面仍是旧的 `@gpu` 偏好」——与树不符：
S1–S4 与 H218 的删除都已在 `origin/develop` 上。**按树，不按看板。**

### 1.1 本日实测（设备无关，探针不随分支提交）

| 事实 | 怎么得到的 | 对下面步骤的影响 |
|---|---|---|
| 静态形状的 `#gpu` 函数里 `x * 2.0` **编译期被拒**（`GpuScopeNoDeviceProgram` `0x040001A7`，点名 `tensor:multiply`，"no device program for this shape"）；原因是区域分析给该节点的理由 `scalar_tensor_operand`（操作数是 f64 字面量）。开放形状 `Tensor<f32>` 不拒（先撞 `unresolved_type`，留给运行期按实参重试）。 | `--devices cpu` + `CUDA_VISIBLE_DEVICES=-1`，`tiles::compile` | **新增 M0**（挡在验收前面）；已定：字面量按设计原则 5.1 取另一操作数的**元素类型**，f64 操作数在图上不再存在，设备侧不必补 f64 标量 |
| 一条 `#gpu` 作用域里两个算子都留不住时，**只打出一条诊断**（只报 `tensor:tanh`，`tensor:multiply` 那条没出现），而 `plan.cn.md` §2.4 要的是"一条诊断列全部"；`NoDeviceProgram` 的消息里 "operator" 出现两次（`operator 'operator 'tensor:multiply''`）。 | 同上，一个作用域里同时写 `tanh(x)` 与 `x * 2.0` | 加一步 M10（诊断），小且设备无关 |
| 分支里的算子**不是"被混进跨分支的段"，而是根本不成为段**：`choose` 的段表是 `regions:[{members:[2]}]`，else 臂的 `add` 以 `control_edge` 排除。也就是说，`#gpu` 作用域里**写在条件里的纯张量算子在主机上静默执行**，`gpuRefusal` 对结构原因（`control_edge`）明确不报。 | `regions<choose>()` 的输出 + `TILES_PLACEMENT` | **改写 M1**：不是"禁止跨控制区域收缩"，而是"臂与循环体要各自成为段" |
| 分支程序（`x*x`，无字面量）在 `--devices cpu` 下给出正确结果（91.0），段表里身体的 `multiply` 成段、有 upload/download。 | 同上 | M1 的守卫可以设备无关：断言"臂里的算子成为自己的段" |

**主机 GPU 现状（2026-10-24，88zjwei，8×RTX 3090 共享）**：`test/plans/gpu` 全层 **134 项 10 通过 / 121 失败 / 3 跳过**，
失败都是 `tiles: retain CUDA context: CUDA_ERROR_OUT_OF_MEMORY` 或 `CUDA_ERROR_UNKNOWN`——**创建 CUDA 上下文就失败**，
与本分支的改动无关。所以在主机恢复之前：**GPU 执行类的用例不能作为落地门禁的读数**，能用的是设备无关的
编译期与段表证据。同一时刻 `smoke + feat`（fast 层）是 **5885 项 5870 通过 / 1 失败 / 14 跳过**，那 1 项失败是
runner 自测（`not_contains_fails`，`test/tools/**` 属他人），与本项无关。

---

## 2. 步骤（按依赖顺序；一步一提交，每步自己窄门禁绿）

### M0 设备上的标量操作数（常量一类）：`x * 2.0`
- **改什么**：静态形状的 `#gpu` 作用域里，张量算子的 f64 标量操作数（今天只出现在字面量）不再以
  `scalar_tensor_operand` 拒绝，而是建构成标量输入（`shape={}`、`dtype=F64`、`scalarBits`，见
  `modules/tiles/src/compiler.cpp` 里 `host()` 对 `runtimeInput && Float64` 的构造），由点式/归约核当广播标量用；
  运行期才知道的 f64 标量（`let a = 2.0; x * a`）先保持拒绝，
  并在诊断里说清是"运行期标量"而不是"形状"。
- **文件**：`modules/tiles/src/graph_regions.cpp`（`eligibility` 的算子表），`modules/tiles/src/kernel_ir.cpp` /
  `index_expr.cpp`（标量一侧的索引与广播），`modules/tiles/src/compiler.cpp`（f64 标量输入的构造）。
- **证据**：新 `test/cases/gpu/nofallback/scalar_operand.cml`：静态形状的 `#gpu` 函数里 `x * 2.0`、`y + 1.0`、
  `1.0 - y`；`tiles::compile` 退出 0，`--devices cpu` 下数值与去掉 `#gpu` 的同一程序逐位相同，且段表里该算子成段；
  设备可见时 `std::profile` 的 `operator_rows[].device` 是 `cuda`（主机恢复后补测）。守卫**先红**：今天的现象见 §1.1。
- **依赖**：张量算子位置的字面量按设计原则 5.1 取另一操作数的元素类型（`x * 2.0` 的 `2.0` 是 f32），图上因此没有 f64 操作数，`scalar_tensor_operand` 的拒绝自然消失；已取型的 `float`（f64）标量与张量相乘是类型错误，提示写 `f32(a)`，与标量 `f32 * f64` 同一条规则。

### M1 段边界：臂与循环体各自成为段，`BRCH`/`JOIN`/`LOOP` 留主机
- **改什么**（按 §1.1 的实测改写）：臂与循环体里的纯张量算子今天被 `control_edge` 排除，**既不成段也不报**——
  `#gpu` 里写在条件里的计算在主机上静默执行，正是设计稿第 1 节要消灭的那类回退。修法是让"控制区域内部的算子"
  按它所在的区域自成一段（收缩只在同一控制区域内进行，`rt::ControlRegions::controlOf`），控制端点
  （`BRCH`/`JOIN`/`LOOP` 自身）继续留主机；段的调用节点必须插在**成员所在的控制区域**（`draft.setInsertRegion`），
  不能一律插到函数体（否则臂里的计算被提到分支之外，未选中臂也会执行）。
- **文件**：`modules/tiles/src/graph_regions.cpp`（`RegionNode` 带控制区域；`eligibility` 不再把区域内部的
  `control_edge` 当排除理由），`modules/tiles/include/tiles/internal/regions.h`，`modules/tiles/src/regions.cpp`
  （收缩限同一控制区域），`modules/tiles/src/region_outline.cpp`（按成员区域插入调用与传输）。
- **证据**：新 `test/cases/gpu/nofallback/branch.cml` 与 `loop.cml` + `test/plans/gpu/tiles.plan.toml` 两条：
  （a）段表里 else 臂的算子是**自己的段**（今天 `regions:[]`/排除理由是 `control_edge`，先红）；（b）未选中臂
  不被计算——未选中臂里放一次除零，程序不得报 `DivisionByZero`；（c）结果与去掉 `#gpu` 的同一程序逐位相同；
  （d）循环体每轮一次传输。设备可见时再断言 `operator_rows[].device` 全是 `cuda`（主机恢复后补）。
- **依赖**：S1–S4（已落地）。

### M2 形状签名：整段编译按实际形状特化、按签名缓存
- **改什么**：`#gpu` 作用域里静态类型没定住 extent 的算子今天不进任何段（`eligibility` 的
  `unresolved_type`），运行期由调用点按实参重试（`1b97de680`）；重试成功即执行，但**每次调用都走一遍
  建程序**，没有"同签名只编一次"的账。M2 把编译缓存的键补成**形状签名**（各实参的 dtype + extents 的
  规范形式）+ 已有结构键，并让 `?` 走有界队列；同一签名第二次命中，不再建程序。H47/H48 复核的第一条硬要求
  落在这里。
- **文件**：`modules/tiles/src/codegen_cache.cpp`（键与命中计数），`modules/tiles/include/tiles/internal/codegen_cache.h`，
  `modules/tiles/src/compiled_region.cpp` / `compiler.cpp`（按实参特化的入口），`modules/tiles/src/module.cpp`
  （运行期重试的调用点）。
- **证据**：新 `test/cases/gpu/nofallback/shape_signature.cml`：同一函数用同一形状调两次、用另一个形状调一次；
  `std::profile json=` 的编译/程序计数断言"同签名 1 次、两签名 2 次"，并对三次结果各取校验和与主机路径对照。
  **不用墙钟**（主机是共享的 8×3090，见 §4）。守卫先红：今天没有这个计数，且第二次调用仍会建程序。
- **依赖**：M1（段是形状特化的单位）。

### M3 派生图在编译期：`autodiff::expand`
- **改什么**：`grad`/`with_grad` 是运行期算子，派生图在解释器里执行，反向永远在主机上。新增模块遍
  `autodiff::expand`：对**作用域内**、函数操作数静态可知（命名函数或静态闭包常量）的调用点，在编译期调用
  `buildGradientGraph` 建出派生图，把调用点换成持有该图的函数常量；派生图继承 primal 的放置。复核第 1 条的
 两条硬要求：该遍**幂等**；**无 `#gpu` 的图上逐字节不变**（`std::jgir` 比，不是比结果）。
- **文件**：新 `modules/autodiff/src/expand.cpp`、`modules/autodiff/CMakeLists.txt`、
  `modules/autodiff/module.cpp`（登记遍）、`modules/tiles/src/region_pass.cpp`（作用域图先展开）。
  放置事实由 `attr::key("tiles.placement")` + `rt::ControlRegions` 读，规则与 `placement.cpp` 的
  `placementOf` **同一份 4 步顺序**（见 §5 决定 1）。
- **证据**：`test/cases/gpu/nofallback/grad_expand.cml`（有/无该遍数值逐位相同；作用域内派生图 dump 出
  `placement=gpu`），`grad_dynamic.cml`（negative：运行期才知道的函数 → `GpuScopeUnsupported`，点名 `grad`
  与调用点），以及"展开前后 `std::jgir` 逐字节相同"的 jgir 用例。
- **依赖**：M1；`modules/tensor/ops/vjp.cpp` 的 `TensorType::Default()` 属他人（§6），本步只要求调用点替换，
  不要求反向节点带类型。

### M4 设备上的数据块就是数组值
- **改什么**：`mm::Block` 的 `device_`/`device()`（`include/camel/core/mm/block.h`）今天没有任何写入者；设备值
  走 Tiles 自己的存储。按设计稿 §3：设备块是同一个值的另一个块，`device_` 填 DLPack 的设备，引用计数、原地写
  （计数为 1）、DLPack 交换沿用；传输是普通的值写入节点；"同一个值两种放置"不产生第二个类型或第二个名字。
- **文件**：`include/camel/core/mm/block.h`（写入者与访问器的边界），`modules/tiles/src/device.cpp`、
  `resident_storage.cpp`、`access.cpp`、`module.cpp`（设备块的 owner 记录：流、事件、大小、释放回调）。
- **证据**：`device_block_identity.cml`（上传后原值仍可读、类型与名字不变、两个块各自的 `device()`）、
  `device_inplace.cml`（计数为 1 原地写；优化器每步 0 分配，用 `tiles::inspect` 的分配计数），
  `device_tree.cml` 的 DLPack 段加设备字段断言。
- **依赖**：M1、M2（段是传输插入的单位）。**今天的状态**：`device_` 恒为 0，守卫先红。

### M5 驻留由图推出、传输由遍插成节点
- **改什么**：新 `modules/tiles/src/residency.cpp`：每个块的 owner 由产生它的节点决定；调用的形参跟随实参的块；
  循环状态跨轮驻留；`#cpu` 块的入口一个 download、出口一个 upload（不是每算子一次）；传输沿用 H49a 的合批。
  `region_outline.cpp` 按驻留结论在放置变化处插节点，`schedule.cpp` 按放置做存储计划。
- **证据**：`resident_loop.cml`（状态跨轮驻留 + 每轮传输计数）、`resident_cpu_block.cml`（每轮恰好两次），
  两条都钉住数字而不是"没有崩"；`tiles::inspect` 的 transfers 表与 `std::profile` 的 `toDevice`/`toHost` 对照。
- **依赖**：M4。

### M6 异步提交
- **改什么**：提交点是段算子内部：`submitProgram` 的 outputs + completion 记在输出块的 owner 上，算子立即返回
  pending 设备值；主机观察（`download`、`println`/`save_npy`、主机算子读设备值、图出口）才等；失败记进 fault
  word，在段节点按 source origin 报出。**公共 `upload`/`download`/`run_preferred_resident` 的同步语义不变。**
- **证据**：`async_scope.cml` 新建 + 既有 `async_execution.cml`、`async_submission*` 保持绿；`std::profile` 的
  `syncs`/`kernels` 计数（每段恰好一次观察点）；`device_fault_site.cml`（origin 正确）。
- **依赖**：M4、M5。

### M7 整段编译：单元 =（作用域图, 放置）
- **改什么**：一个 `#gpu` 函数/块 + 它传递闭包里同放置的被调图，一起编成**一个** `Program`，段是入口；作用域图
  上记目标事实 `tiles.compiled_scope`（`tiles.compiled_region` 的扩展）。不是每段一次编译，也不是每算子一次。
- **证据**：`tiles::inspect` 里一个作用域一份编译产物；同一作用域两次调用编译计数为 1；`placement_callee.cml`
  （无标签 helper 被 `#gpu` 与主机两处调用 → 两份单元）。
- **依赖**：M1、M6。

### M8 持久缓存与失效
- **改什么**：编译产物按内容哈希写磁盘缓存，第二次运行不再编译；键 = 图规范编码 + 声明键与语义版本 +
  目标身份（arch / NVRTC / 代码生成 schema / 策略）+ **编译器的构建身份**（`build-info.json` 指纹，复核第 3 条）
  + 形状签名；**不用源哈希**。失效矩阵要能说清"改了什么会读不到旧内核"。
- **证据**：两次运行第二次编译计数为 0；改代码生成器指纹后必须重新编译（用旧的伪造指纹断言命中为 0）；
  失效矩阵逐条有用例。缓存目录在共享主机上默认放每次运行的临时目录，见 §4。
- **依赖**：M2、M7。

### M9 回传图带类型与形状
- **改什么**：端口类型带 extent form（`constas n`），调用点绑定实际 extents 并把输出类型/形状写回节点；反向图
  的节点用算子声明的类型推断，不用 `TensorType::Default()`（`modules/tensor/ops/vjp.cpp`，属他人，见 §6）。
- **证据**：一个 `#gpu` 作用域函数的返回值在图上带元素类型与形状（`std::jgir` 断言），反向节点同样；今天两边
  都是未知。
- **依赖**：M3、M7；`vjp.cpp` 的一半需要 §6 的协调。

### M10 拒绝的诊断：一条列全部，消息不重词
- **改什么**：`region_outline.cpp` 的拒绝走查已经为每个留不住的算子 commit 一条诊断，但实测只打出一条（§1.1）。
  按 `plan.cn.md` §2.4 改成**一个作用域一条诊断、列出全部留不住的算子及其原因**（按错误码分组，避免改动既有
  用例对码的期望），并把出路写成 `#cpu`（复核 2026-10-22 第 3 条）；同时去掉 `NoDeviceProgram` 消息里重复的
  "operator"。
- **文件**：`modules/tiles/src/region_outline.cpp`。
- **证据**：新 `test/cases/gpu/nofallback/two_refusals.cml`（一个作用域里 `tanh(x)` 与 `x * 2.0`）+ plan 用例断言
  两条算子名与两条原因都在输出里；既有 `nofallback.*` 的期望不变。守卫先红：今天只出现 `tensor:tanh` 一条。
- **依赖**：无（设备无关）。M0 落地后第二个算子的原因会变，届时同步该用例。

---

## 3. 验收

- **本项窄门禁（每步）**：`smoke` + `test/plans/feat` 的 fast 层，加该步新建的 GPU 用例。
- **程序级**：C2（`showcase/c2-mlp-mnist/train.cml`）与 `transformer_step`（`benchmarks/transformer/model.cml`）的
  **全部**算子（含反向与优化器）在设备上，与 PyTorch（CUDA、f32）同精度对照；容差按 plan.cn.md §6.3。
  支持面缺口（bf16/Tensor Core、`take`/散射、规范化、随机数）是 H50 的输入，按 plan.cn.md §6.5 列成清单，
  不在本项补齐。
- **"全部算子在设备上"怎么测**：不看源码意图，看两张表——`std::profile json` 的
  `operator_rows[].device`（每一行都必须是 `cuda`）、`device_work.{kernels,toDevice,toHost,syncs}`，
  以及 `tiles::inspect` 的段/传输表。缺读数即失败，不是跳过。

## 4. 测量纪律（共享主机）

主机是共享的 8×RTX 3090，负载会动。所以：**断言只用精确计数、校验和与后端/主机一致性**，不写墙钟阈值；
确实要报时间时，写清设备序号、同一时刻的负载（`nvidia-smi` 的利用率与占用进程数）、以及重复次数与离散度，
并说明这个数不足以作为断言。编译/运行只走
`pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h365-h49-gpu-plan -Plan ...`；
`-Plan <某个 plan.toml>` 按文件全跑。磁盘缓存的用例写在每次运行的临时目录，避免共享主机上的残留让第二次命中
变成假绿。

## 5. 本文自己做的结构性决定（随计划合入，按惯例不等批复）

1. **谁读放置事实**：`autodiff::expand` 不链接 `tiles`；它用 `attr::key("tiles.placement")` 加
   `ControlRegions` 自己走一遍 `placementOf` 的 4 步顺序（节点属性 → 区域链 → 函数体 → 主机）。代价是这份顺序
   有两个读者；**要求**：改动 `placement.cpp` 的顺序必须同时改 `expand.cpp`，并在两处都指向 `placement.cn.md`。
   另一种选择（把查询上移到核心）会让核心依赖图区域的语义，代价更大。可推翻。
2. **`#gpu` 的展开由 Tiles 触发**：`tiles::compile` 在作用域图上先应用 `autodiff::expand`（复核 2026-10-22
   追加条：用户不写这个遍，流水线写法不变）；`autodiff::expand` 仍作为普通遍可用，单独跑在无 `#gpu` 的图上
   逐字节不变。
3. **段的落地位置**：段的调用节点插在**成员所在的控制区域**（`draft.setInsertRegion`），不是一律插到函数体；
   否则臂里的计算会被提到分支之外，未选中臂也会执行。

## 6. 归属与不重叠

- 本计划**只改** `modules/tiles/**`、`modules/autodiff/**`、`test/cases/gpu/**`、`test/cases/modules/tiles/**`、
  `test/plans/gpu/**`、`test/plans/feat/modules/tiles.plan.toml`，以及 `docs/design/gpu/` 下的新文件。
- 别人的区域（读，不改）：图的边与 `src/runtime/**`；类型系统与 `Tensor` 写法；`modules/tensor` 的算子
  （`ops/vjp.cpp` 是 M9 的一半）；`stdlib/{optim,opt,rules}.cml` 与改写模块；`showcase/**`；`test/tools/**`。
- 因此 **M4（`mm::Block::device_`）与 M9（vjp 的类型）** 落地前需要和这些区域的所有者对齐接口；M1–M3、M5–M8
  不需要。

## 7. 风险

- **M1 会动区域分段的既有期望**：`tiles.plan.toml` 的 `regions.*`、`tiles_regions.plan.toml` 断言成员数与段数；
  让控制区域内部的算子成段之后，这些用例里若有期望变化，先查是"段被正确地切开"还是"少了融合"，
  期望只在能引用设计稿条款时改，否则当作回归。另外段的调用节点落在臂/循环体内是这一步最危险的一处
  （插错位置等于把条件里的计算提到外面），必须有"未选中臂不计算"的守卫同时落地。
- **M2 的缓存命中容易假绿**：计数必须来自编译器，不是用例自己数的调用次数。
- **M3 的"逐字节不变"要用 jgir golden**，用结果比会漏掉类型与形状的变化。