# H49 实施计划：G1–G3 —— 放置标签、设备上的数组值、驻留与异步、整段编译与缓存

状态：**计划，待协调者批复**。本分支（`handover/h49-gpu-plan`）只改文档：不含任何实现，不改变任何被观察到的行为。基线 `9e71310ab`（2026-10-22，`origin/develop`）。

依据：

- [GPU 设计稿](README.cn.md) §1（缺口表）、§2（放置与作用域）、§3（设备上的数组值）、§4（整段编译与缓存）、§6（三条决定）、§7（G0–G7 的顺序与编号）；
- [设计原则](../../design-principles.cn.md) 3.1（按依赖求值）、3.6（由声明的性质并行）、3.10（循环）、6.2（标签只声明事实）、9.4（标签的读者与无效果警告）、9.6（优化保持算子身份）、10.5（库的边界与数据块）、13.3（`Device<T>` 与异步不在语义里）、13.4（分布式与放置）；
- [H24–H26 检查意见](../../handover/reviews/H24-H25-H26.md) 对 H47/H48 的三条硬要求：动态形状必须能编译、反向图必须带同元素类型与形状、设备不可用必须启动时报错；
- [H77–H140 检查意见](../../handover/reviews/H77-H140.md)（H85 `#independent`、H102 融合记在图的分组属性上）；[H141–H213 检查意见](../../handover/reviews/H141-H213.md) 的 Q5（删除 `@gpu`/`run`/`run_device`，由 H49 执行）与 Q6（只有 `--devices`，没有 `tiles::compile(device=)`；源码里的设备是放置标签，永远不是遍参数）；
- [模块缓存与失效面](../technical/52_cmc_cache_cost.md)：源哈希不能证明接口未变，这是缓存键的界线；
- [Tiles 路线的区域与传输](../tiles-route/README.cn.md) §3.2–§3.6（区域 → `OPER :tiles/exec`、显式 upload/download、符号形状编一次、键是语义指纹、故障记在调用点）；
- [索引化计算](../indexed-computation/README.md) §6.3（异步：完成事件、依赖保证完成与可见性、没有隐式同步）；
- 在途的 H218（`handover/H218-GPU-REMOVAL`）：删除 `@gpu`/`run`/`run_device` 与旧分派，落 `tiles.gpu` 与 `RuntimeDiag::GpuScopeUnsupported = 0x000071`。本计划按它合入后的树写，第 7 节说明它未合入时的顺序。

本文的每个结论都对应到文件与类型；读它不需要读代码。第 11 节是会被触碰的文件的完整清单，第 10 节是给作者的少数几个问题。文中的 GIR 是压缩草图（节点、算子 URI、属性、区域与段），不是 `std::gir` 的原样输出。

---

## 0. 一页结论

任务六项，逐项的决定与**它在图上产生的后果**：

| 问题 | 决定 | 图上的后果 |
|---|---|---|
| 1. 标签与作用域 | `#gpu`/`#cpu` 写**同一个属性键** `tiles.placement`（Symbol 值 `gpu`/`cpu`，combine=inner）；作用域 = 区域：函数体（region 0）、带标签的块（`GCRegionKind::Block`）、Arm、LoopBody；边界 = 区域边界；一个节点恰好一个放置，内层胜出；嵌套由解析规则消掉、部分作用域结构上不存在，不是"静默地为错的设备编译"；派生图用 `autodiff::expand` 在编译期建出并继承放置（§2.6）；不支持的算子、主机效果 → `GpuScopeUnsupported 0x000071`（H218） | 被标记的节点带属性；函数体区域 0 带属性；作用域内每个段在图上是一个 `OPER :tiles/exec` 调用，跨作用域边上是显式 upload/download 节点；分支的 BRCH/JOIN 留主机侧，每个臂一个段；`grad(f)` 的调用点换成持有派生图的函数常量 |
| 2. 设备数据块 | **就是普通数组值**：`mm::Block::device_` 填 DLPack 的 device；**没有第二个类型、没有第二个用户可见的名字**："同一值两种放置"是同一个值在两个用途上的放置事实，拷贝是编译器的实现（普通的值写入），不是第二个值；`Device<T>` 若保留只是签名上的放置断言（问题 2） | 传输与拷贝由遍插成节点（`:tiles/upload`/`:tiles/download`），不是隐藏同步；块的引用计数与原地写不变；优化器每步 0 分配 |
| 3. 驻留与异步提交 | 驻留**推断**：整图（含循环、调用、with 参数）上的 owner 表；传输只在放置变化处由遍插成节点；调度器只决定**就绪**，提交发生在 `tiles:exec` 算子内部；异步 = 调用返回带完成令牌的设备值，主机观察时等待；无新语言原语 | 段的 exec 节点返回 pending 设备值；完成令牌记在设备块的 owner 记录上；数据边 + 资源边给出顺序，没有隐藏同步；故障在 exec 节点按 source origin 报出 |
| 4. 整段编译与缓存 | 编译单元 = **（作用域图, 放置）**，含传递闭包里同一放置的被调图；一个作用域一次 `compile`，段是入口；符号维（`constas n`）编一次，形状签名只决定特化参数；`?` 按具体形状入有界队列；缓存键 = 图规范编码 + 声明键与语义版本 + 目标身份（arch/NVRTC/代码生成 schema/策略）+ 放置，**不用源哈希**；stamp = 图修订 + 声明纪元 + 目标哈希 | 作用域图带目标事实 `tiles.compiled_scope`（`tiles.compiled_region` 的扩展）；段的 exec 节点各带自己的函数常量，共享同一份编译产物；第二次调用命中，不再编译 |
| 5. 返回图带类型与形状；设备启动校验 | 端口类型带 extent form（`constas n`），调用点绑定实际 extents 并把输出类型/形状写回节点；反向图的节点用**算子声明的类型推断**而不是 `TensorType::Default()`；`--devices` 在启动时对**每个**点名序号校验，缺失 → `DeviceUnavailable 0x000070` 原文 | 输出值构造时不需要再推断类型；反向图的节点带同元素类型与形状（也就能被区域分析看见）；启动校验失败时没有任何图被执行 |
| 6. 验收 | C2 与 `transformer_step`：f32 两边同精度，PyTorch 在 CUDA 上作参照；C2 用 `compare.py` 的四条容差，transformer 用 `train.py --check/--check-ksteps`（1e-4）与 `grad_referee.py` 分诊；放置断言用 `std::profile` 的 JSON（kernels/toDevice/toHost/syncs/hints）与 `tiles::inspect` 的段/传输表；支持面缺口逐条列成 H50 的清单 | 验收脚本现在就固定下来；H49 的每步只对自己的窄门禁负责，程序级闸门在支持面补齐后零缺口 |

十个步骤，按顺序：

| 步骤 | 内容 | 能否单独落地 | 门禁 |
|---|---|---|---|
| **S1** | 启动设备校验（核心的校验注册 + 驱动调用 + Tiles 的校验器） | 能，不改语言与图 | `smoke`、`feat/modules/tiles` 的错误路径用例 |
| **S2** | 放置键、两个标签、body region、全节点盖章（无消费者） | 能，不改行为 | `feat/compile` 的 GIR 快照、`inspect --tags` |
| **S3** | 放置解析、作用域边界与 `inspect` 输出 | 能（S2 之后） | 新增 `placement_*` 用例，`--devices cpu` |
| **S4** | 拒绝：不支持的算子、主机效果、无法确定放置的节点 | 能（S3 之后），此时 `#gpu` 首次有语义 | 负向用例的退出码与诊断文本 |
| **S5** | 图内区域（按控制区域切段，分支、循环、调用）与派生图展开（`autodiff::expand`） | 能（S4 + H218 之后），本项第一步真正改变执行 | `tiles` 全套 + 分支/循环用例 + 展开前后逐位相同 |
| **S6** | 设备块与值模型（`device_`、owner 记录、原地写、DLPack） | 能（S5 之后） | `feat/modules/tiles` 的设备路径 |
| **S7** | 驻留推断与传输节点（owner 表、循环状态、调用边界、合批） | 能（S6 之后） | 传输计数用例、`inspect` 的 transfers |
| **S8** | 异步提交与就绪（pending 值、停等点、故障位置） | 能（S6 之后） | `syncs`/`kernels` 计数、`async_execution.cml` 的既有断言、`plan_check` 的传输边界 |
| **S9** | 整段编译：单元 = (作用域, 放置) 的闭包；段入口；一个作用域一次编译 | 能（S5+S8 之后） | `inspect` 一个 scope 一份产物；编译计数 = 1 |
| **S10** | 形状签名字典与持久缓存（磁盘、重新校验、失效） | 能（S9 之后） | 符号维只编译一次；两次运行第二次 0 次编译；失效矩阵 |

**不改变行为**：未带标签的程序的行为、区域抽取对未带标签图形的现有结果、`--devices cpu` 的现有机型、公共 `upload`/`download`/`run_device` 的同步语义（H218 保留它们作为显式路径）、`std::profile` 的既有字段。

**不在本项**：支持面（bf16/Tensor Core、张量核、融合、优化器多张量融合、CUDA 图）——那是 H50；多卡 `#shard`——G7；分支与循环**在设备上执行**——G4（本计划只把它们的**段**放到设备上，BRCH/JOIN/LOOP 留主机）；`std::prl` 的并行放置。

---

## 1. 基线：今天有什么

### 1.1 图、区域与属性

- 节点属性（`include/camel/core/node_attrs.h`）：一个节点一个 interned 属性集 `AttrSetId`；键由库注册，带 `combine`（内层胜出是默认）与 `merge` 规则；**重写复制节点时保留属性**是文档写明的不变式。`attr::unite(outer, inner)` 按每个键的规则给内层值。
- 区域（`include/camel/runtime/graph.h`）：`GCRegion{kind, parent, owner, index, result, attrs, …}`，`GCRegionKind::{Root, Arm, LoopCond, LoopBody, Inline, Block, Dead}`；region 0 是函数体，`nodeRegion()` 对体里的节点返回 0；`Block` 的注释是"带标签或装饰器的块"，但**今天没有 builder 创建它**（`src/compile/gir/builder_control.cpp::enterBlockScope` 只管名字作用域）。
- `src/compile/gir/builder.cpp::buildFuncBody`（约 702–724 行）把声明与体的 `TagFacts.nodes` 通过 `AttrScope` 变成体内节点的属性；`stampPermissions`（695 行）在**算子节点**创建时写 `currentAttrs_`——其它节点种类不写。
- `ControlRegions`（`include/camel/runtime/control_regions.h`）：`controlOf`/`within`/`parentControl`/`armsOf`/`loopPartsOf`，把 `Block`/`Inline` 区域"看穿"。这是判断"哪个控制区域"的现成工具。
- 标签（`include/camel/core/tags.h`）：`Decl{name, on, readBy, key, value, …}`；`declareNodeTag(qualifiedName, positions, NodeTagSpec)` **用限定名当属性键**，`NodeTagSpec` 没有覆盖键的入口。

### 1.2 Tiles 今天的区域

- `tiles::compile`（`modules/tiles/src/region_pass.cpp`）是 `RuntimeGraphRewritePass`；`outlineRegions`（`region_outline.cpp`）对一个图做贪心最大合法收缩（`regions.cpp`：`partitionRegions` 保持商图无环），把区域节点换成一个 `OPER tiles:exec`（With 0 = 区域图的函数常量），在入口插 `tiles:upload`、出口按主机消费者插 `tiles:download`，并把区域图当作模板 `compileTypes` 缓存。
- 合法性（`graph_regions.cpp`）：节点形状必须是**静态 f32**（`TensorType::isStaticShape()`；`?` 一律拒绝，原因是 `unresolved_type`），算子必须在一张固定的表里（`operations()`），节点不能有控制边、不能有外部效果、不能有上下文参数；循环整体看 `loopEligibility`。
- `tiles.compiled_region`（`compiled_region.cpp`）是图作用域**目标事实**，键是常量目标哈希 `0x74696c6573000004ULL`；`compileTypes` 拒绝闭包/驻留/调用数据。
- 公共调用是同步的：`preferGpuKernel`/`runDeviceKernel` 在返回前等设备。

### 1.3 设备值与异步

- `DeviceType`（`modules/tiles/include/tiles/device.h`）包住宿主类型；`DeviceValue{shape, dtype, address, owner, completion}`，`DeviceCompletion{wait, ready}`。
- `mm::Block` 已有 `int device_ = 0`，注释写明"设备块由 Device lowering 填这个字段"——**今天没有地方填**。
- `submitProgram` 返回 `SubmittedProgram{outputs, completion}`，但注释写明"完成令牌从不跨 Camel 算子边界"：公共同步、内部异步。

### 1.4 动态形状

- `TensorType` 的 extents 支持 `kUnknownDim = -1` 与命名维（`ExtentForm`：Unknown/Constant/Linear，`bind`/`evaluate`），`const [constas n, 3]` 可写。
- 区域分析只看 `isStaticShape()`，所以带 `?` 或 `constas n` 的区域今天**编不了**——硬要求 1 的现状。
- 反向图：`modules/tensor/ops/vjp.cpp::addTensorOper` 给每个 VJP 节点 `TensorType::Default()`（完全未知的 `Tensor`），所以反向节点既没有形状也没有精确元素类型——硬要求 2 的现状。

### 1.5 `--devices`

`parseDeviceSelection`（`src/execute/devices.cpp`）解析 `cpu` 与序号列表（`cpu` 必须独占），`selectDevices` 只把结果存起来；**没有任何启动时的可用性校验**：缺失的序号在 Tiles 第一次要用设备时才变成 `DeviceUnavailable`。

### 1.6 缓存

- `codegen_cache.cpp`：对降级后的 `Program`/contracts/语义做**结构键**，schema 号 8，进程内 LRU；注释写明"进程内键 schema，不是持久语义指纹"。
- Tiles 路线的 §3.4 已经给出正确的口径：键 = 区域语义指纹 + arch + 编译器 + 调度策略，**不是生成的源**。
- 技术 52：源哈希不能证明接口未变；能廉价重算的只是"录下来的接口指纹"。

### 1.7 支持面缺口（为什么验收要分两段）

C2（`showcase/c2-mlp-mnist/train.cml`）与 `transformer_step`（`benchmarks/transformer/model.cml` 的 `train_step`/`step`）用到的算子远多于 `operations()` 里的那张表：`permute`/`reshape`/`transpose`（视图）、`softmax`、`gelu`、`layer_norm`、`mean`、`cross_entropy`、`sgd`/`adam`、`cast`。按板上的分工，**支持面是 H50**，H49 只做 G1–G3。因此本计划的验收分两段：H49 每步的窄门禁，与程序级闸门（脚本现在就固定，缺口清单就是 H50 的输入），见第 6 节。

---

## 2. G1：放置标签与作用域

### 2.1 两个名字，一个键

`#gpu` 与 `#cpu` 都必须写进**同一个单值键** `tiles.placement`（值 `Symbol("gpu")` / `Symbol("cpu")`），combine 用默认的"内层胜出"，merge 用 `equalOnly`。理由不是风格：

```
#cpu {                      // 合法：在一个 #gpu 函数里刻意留主机
    #gpu { ... }            // 合法：再进去
}
```

如果用两个 Unit 键（`tiles.gpu`、`tiles.cpu`），内层节点会**同时**带两个键，`attr::unite` 对不同的键没有"谁胜出"可言，解析只能靠遍历区域顺序猜；单值键让 `unite` 直接给出答案，而且让"一个节点恰好一个放置"成为结构事实。

机制（不改语言、不改 tag 的身份）：

- `NodeTagSpec` 加一个可选字段 `attrKey`（空 = 用限定名），`declareNodeTag` 用它作属性键；
- Tiles 声明 `tiles::gpu` 与 `tiles::cpu` 两个 tag，`positions` 取 `Func | Nodes`（模块位置见第 10 节问题 1），`kind = Symbol`，`value` 回调对空参数返回各自的 `Symbol` 值，`readBy = {"tiles::compile"}`；
- H218 的 `tiles.gpu`（Unit）被这条取代：键名从 `tiles.gpu` 变成 `tiles.placement`，因此**读键的地方只有一处**（Tiles 自己），H218 的 `refuseGpuScopes` 改成读这个键。
- 源码**不点名具体的卡**（Q6）：标签只说"设备"，卡由 `--devices` 定；一次运行里所有 `#gpu` 用列表里的第一张卡（多卡是 G7 的 `#shard`）。这也让"源码里的设备是放置标签，不是遍参数"成立。

```gir
graph train_step                    body region 0: attrs={tiles.placement=gpu}
  PORT m : Block                    [placement=gpu]
  PORT x : Tensor<f32, [constas n, 4]>   [placement=gpu]
  N0  :tensor/matmul  (x, m.q_w)    [placement=gpu]
  …
  N9  :tensor/sum_to  (N8, [constas n, 4])   [placement=gpu]
  EXIT N9
```

节点的属性是**记录下来的事实**（能被 dump、被缓存键读到、被重写复制）；区域上的属性是**作用域的范围**（§2.2）。两者由同一处写入，不允许分叉。

### 2.2 作用域、边界与解析

放置解析的顺序（`placementOf(graph, node)`，唯一入口）：

1. 节点自己的 `tiles.placement` 属性；
2. `nodeRegion` 向外的区域链上**最近的带该键的区域**；
3. 所在图的 **body 区域 0** 的属性（函数/块作用域）；
4. 无 → 主机（`--devices` 不给设备时就是主机后端）。

节点属性优先，原因是重写：内联、抽取把节点搬出原区域后，放置要跟着节点走（`node_attrs.h` 的"重写保留属性"不变式）；区域链只服务于 lowering 新建、还来不及盖章的节点。

为此 G1 要做三件小事：

- `stampPermissions` 扩成 `stampNode`，**每种节点**创建时都写 `currentAttrs_`（今天只写算子）；
- `buildFuncBody` 在体建完后把声明与体的 `TagFacts.nodes` 写到 **body region 0 的 attrs**（`GraphDraft::setRegionAttrs` 允许 id 0：首次写入时建 `regionTable[0]`，kind `Root`）；
- `visitExecNode` 在块带标签时建 `GCRegionKind::Block` 区域并写 attrs（`enterBlockScope` 旁边），块里写/建的所有节点落在这个区域里。`Block` 区域被 `ControlRegions` 看穿，所以它只表达作用域，不表达控制。

`graph_encode.cpp` 现在从 `r = 1` 开始抄区域（约 616 行），要改成含 r=0 的 attrs，否则 body 放置进不了运行图、也进不了缓存指纹。

边界就是区域边界：函数体、带标签的块、每个 Arm、每个 LoopBody。跨边界的**边**就是传输点（§3.3）。判断"在哪个控制区域"用现成的 `ControlRegions`。

```gir
graph f                             body region 0: attrs={tiles.placement=gpu}
  N0  :tensor/add (x, y)            [placement=gpu]
  region #4 Block  attrs={tiles.placement=cpu}      // `#cpu { … }`
    N1  :io/println (s)             [placement=cpu]
    N2  :tensor/sum (N0)            [placement=cpu]   // 入口处一个 download
  region #5 Block  attrs={tiles.placement=gpu}      // 里面再写 `#gpu { … }`
    N3  :tensor/mul (N2, N2)        [placement=gpu]  // 内层胜出
```

"嵌套作用域"因此是**幂等的**（同值 unite 成同值）；"部分作用域"在结构上不存在：一个节点恰好一个放置，作用域外的节点不可能被当成作用域内，反之亦然。剩下的问题只是**传播**（§2.3）与**拒绝**（§2.4）。

### 2.3 传播：作用域覆盖它调用的函数与派生图

放置是"这一段计算在哪跑"，所以一个被调函数没有自己的标签时**跟着调用点走**：

| 被调函数 | 从 `#gpu` 作用域调用 | 从主机调用 |
|---|---|---|
| 无标签 | 设备（同一个编译单元） | 主机 |
| `#gpu` | 设备 | 设备（显式要求） |
| `#cpu` | 主机（显式逃生口，边界处传输） | 主机 |

同一个函数被两处调用、放置不同 → 它被编译**两份**，单元是（图, 放置），不是图。这不是特例，是规则的结果。

梯度：派生图由自动微分在**新图**里建出来（`modules/autodiff/pullback.cpp`、`modules/tensor/ops/vjp.cpp`）。今天它是运行期建的（§2.6 的展开遍把它移到编译期），无论哪条路，它都必须继承 primal 的放置，否则 `#gpu func train_step` 的反向会静默落回主机——"作用域覆盖它的梯度"的机械含义就是两条：

1. 派生图创建时，把 primal 图 **body 区域 0 的 attrs** 抄到派生图的 body 区域；
2. VJP 新建的节点从它的 primal 节点抄 `tiles.placement`（和抄类型是同一处）。

优化器同理：`sgd`/`adam` 是普通被调函数，随调用点进入作用域；它们对参数块**原地写**，所以 G2 的原地规则是优化器能在设备上的前提，不是性能选项。

```gir
graph train_step [body placement=gpu]
  N0  :tensor/…            [placement=gpu]
  N1  :autodiff/with_grad  (loss)         [placement=gpu]
      → 派生图 d(loss)  body: attrs={tiles.placement=gpu}
        N0' :tensor/exp (…)  [placement=gpu]   // 与 primal 同放置
  N2  :nn/sgd (m, N1.1, 0.01)  [placement=gpu]  // 原地写 m 的设备块
```

### 2.4 拒绝：不支持的算子与主机效果

`deviceCapable(oper)` = 在 `operations()` 表里 ∧ pure ∧ 无外部效果。H218 已经用它拒绝 `#gpu` 作用域并报 `RuntimeDiag::GpuScopeUnsupported = 0x000071`；G1 把判定扩到：

- 传递闭包里的**每个**被调图（不只是作用域自己的图）；
- 主机效果算子（`print`/`println`、文件写、`save_npy`）——它们不是"设备做不到的算子"，但同样不能在设备段里执行；
- 作用域里**无法确定放置**的节点（老代码/第三方建图没有盖章）。

诊断形状：一条诊断列出作用域里**所有**不合格算子（URI + origin + 原因），给出两条出路（写 `#cpu` 块，或 `--devices cpu`），不让用户一条一条试。`--devices cpu` **不改变可编译集合**：支持面在编译期定死，`--devices cpu` 只决定"编出来的段在哪执行"（主机后端执行同一段图）。否则同一个程序在有无设备时编译结果不同，这就是"静默地为错的设备编译"。

`#gpu` 写了而流水线里没有 `tiles::compile`：沿用 9.4 的 `SemanticDiag::TagHasNoReader = 0x100160` 警告。两边合起来，任何"标签被忽略"的情形都至少有一条诊断。

### 2.5 图内区域：分支、循环、调用的切分

今天的 `analyzeRegions` 把所有节点的候选放一个池子里，靠"有控制边即不合格"避免跨臂；在 `#gpu` 作用域里这等于把臂里的节点全判死（臂节点有 ctrl 边），作用域于是被整体拒绝。G1 改分区：

- 分区在**控制区域内**进行：一个区域不跨 Arm/LoopBody；`BRCH`/`JOIN`/`SYNC` 留主机（G4 才上设备），每个 Arm 出一个段；
- `LoopBody` 出一个段：状态是设备值，轮与轮之间不落地；`LoopCond` 留主机——条件只读标量（轮次、界）时不产生传输；条件读设备张量时（收敛循环）编译器插一个显式 `download`（每轮一次主机观察，`std::profile` 的 `syncs` 会把它数出来），G4 之后才改为设备上判条件；
- 段 = 一个控制区域里、同一放置、互相依赖连通的节点集合，商图仍要求无环（保守，和今天一样）；
- 段的 exec 节点写在**原位置**（臂里、体里），所以控制流仍在主机，段在设备。

```gir
graph step [body placement=gpu]
  N0 :tensor/matmul (x, w)        [placement=gpu]
  N1 :tensor/relu   (N0)          [placement=gpu]
  BRCH (n1)                        // 主机
    region Arm#1:  N2 :tensor/exp (N1) [gpu] → EXEC#1   // 一个段
    region Arm#2:  N3 :tensor/add (N1, one) [gpu] → EXEC#1
  JOIN
  LOOP (state, k)                  // 主机；LoopBody 是一个段
```

### 2.6 派生图在编译期：一个必需的展开遍

上面"作用域覆盖它的梯度"在今天的机制下**不成立**，这是写这份计划时最重要的发现：`grad`/`with_grad` 是**运行期**算子，`modules/autodiff/operators.cpp::gradFunction` 在算子执行时才调用 `buildGradientGraph` 建出派生图。流水线遍在那之前就跑完了，所以派生图：

- 不会被任何遍（包括 `tiles::compile`）看见，因此不会被编译；
- 在解释器里执行，于是**反向永远在主机上**——验收第六项"含反向"直接失败。

决定（G1 的一部分，见 §7 的 S5b）：

1. 新增模块遍 `autodiff::expand`。它在 `tiles::compile` **之前**运行：对每个 `grad(f)` / `with_grad(f)` 调用点，若 `f` 静态可知（命名函数或静态闭包常量），就在编译期调用 `buildGradientGraph` 建出派生图（需要时给 draft 组加一个 draft 版本，把派生图连同它的 program 挂到同一 session），把调用点换成"持有该图的函数常量"——和运行期算子产生的值形状相同：一个 `Function` 值，`with` 参数在调用点绑定，而不是在图里固化。
2. 派生图继承 primal 的放置（§2.3 的两条），因此和 primal 一起进同一个单元（§4.1）。
3. 作用域里 `f` **不**静态可知的 `grad`（函数值来自参数、返回值或运行期计算）→ 编译期报错，`GpuScopeUnsupported 0x000071`，消息点名 `grad`/`with_grad` 与该调用点，说清"`#gpu` 作用域里的梯度需要编译期可知的函数"。
4. `#gpu` 之外完全不变：没有 `autodiff::expand` 时 `grad` 仍按运行期构建，解释执行。
5. `tiles::compile` 在 `#gpu` 作用域里看到未展开的 `grad`/`with_grad`（遍没有跑）→ 报"缺少前置遍"，消息里**点名 `autodiff::expand`**（本仓库对前置遍的既有做法：库的诊断点名缺少的遍，流水线永远是显式的）。这条诊断**先于** §2.4 的"不支持的算子"：前者有明确的修法（加一个遍），后者只是"这个算子放不上去"。

```gir
# 展开前
N30 :autodiff/with_grad  (DATA(Function loss))   [placement=gpu]
N31 :call  (N30, m) (x)                          [placement=gpu]
# 展开后
graph d(loss)          body attrs={tiles.placement=gpu}     // primal 的放置抄过来
  PORT m, x
  … 反向节点，类型由 §5.2 的规则给出 …
N30 :DATA(Function(d(loss)))                     [placement=gpu]   // 函数常量，不再有 with_grad 算子
N31 :call  (N30, m) (x)                          [placement=gpu]
```

这个遍是 H49 里**唯一**新增的遍；它不改变语言，也不改变 `grad` 的语义（值相同），只是把"什么时候建派生图"从运行期移到编译期，让"编译整段"这件事对反向也成立。

### 2.7 G1 的文件、测试与门禁

文件：`include/camel/core/tags.h`、`src/core/tags.cpp`、`modules/tiles/src/module.cpp`（两个标签）、`src/compile/gir/builder.cpp`（`stampNode` + body attrs）、`src/compile/gir/builder_control.cpp`（`visitExecNode` 建 Block 区域）、`src/runtime/draft_regions.cpp`（`setRegionAttrs(0, …)`）、`src/compile/gir/graph_encode.cpp`（含 r=0 attrs）、`modules/tiles/src/{regions.cpp,graph_regions.cpp,regions.h}`（按控制区域的段）、`modules/tiles/src/region_outline.cpp`（段调用）、`modules/tiles/src/module.cpp`（`deviceCapable` 与 0x71 的判定）、`modules/autodiff/{expand.cpp,pullback.cpp,pullback.h,operators.cpp,module.cpp}`、`modules/tensor/ops/vjp.cpp`。

测试（`test/cases/modules/tiles/`，计划 `test/plans/feat/modules/tiles.plan.toml`，`--devices cpu` 在 CI 上就能跑）：

- `placement_tags.cml`：`#gpu`/`#cpu`/嵌套/同值嵌套；`std::gir` 快照显示 `attrs=`；`inspect --tags` 显示两个 tag 与它们的读者；
- `placement_branch.cml` / `placement_loop.cml`：`#gpu` 函数里的 if/else 与循环，`tiles::inspect` 的段表是 2 个臂段 + 1 个循环体段，传输只在边界；
- `placement_callee.cml`：无标签的 helper 被 `#gpu` 与主机两处调用 → 两份单元（段表里两份 helper 图）；
- `placement_grad.cml`：`#gpu` 里的 `with_grad`，展开遍之后派生图 dump 出 `placement=gpu`，且 `inspect` 的 entries 里有它；
- `placement_grad_expand.cml`：同一个 `with_grad(loss)` 在展开前/后跑出**逐位相同**的值（展开不改变语义）；
- `placement_grad_dynamic.cml`（negative）：`#gpu` 里对运行期才知道的函数调用 `grad` → 0x71，消息点名 `grad` 与该调用点；以及"忘了 `autodiff::expand`"→ 报缺少前置遍、点名它；
- `placement_reject.cml`（negative）：`print`、`sort`、无法盖章的节点 → 退出码 1、`GpuScopeUnsupported`、一条诊断列全部算子；`#gpu` + `--devices cpu` 给出**同样**的错误；
- `placement_no_reader.cml`（negative）：只有 `#gpu` 没有 `tiles::compile` → `TagHasNoReader` 警告。

门禁：`feat/compile`（GIR 快照）、`feat/modules/tiles`、`feat/modules/autodiff`、`smoke`。

---

## 3. G2：设备数据块、驻留、传输与异步

### 3.1 一个值、一个类型，放置是块上的事实

设备上的张量就是**普通的数组值**：`TensorType` 不变（`f32[n,m]` 还是它），`mm::Block` 的 `device_` 填 DLPack 的 device（`device_type`/`device_id`），引用计数、原地写、DLPack 互操作全部沿用（`docs/design/arrays-memory/README.cn.md` §3.2/3.3）。没有第二个值类型、没有新的类型身份、"放置"不是类型的一部分。

**"同一个值两种放置"不是两个值**，这是本节的硬约束，落实为三条：

1. 语言层与类型层只有**一个**值：源码里的名字、`TensorType`、可赋值性、打印都不因放置而变；不需要 `Device<T>` 转换，也没有第二个名字。
2. 图里的传输或拷贝是**普通的一次值写入**（和每个 `let` 产生的节点同一种东西），不是新的用户可见值、不是新的类型；放在哪个设备是块上的事实，编译器按用途选块。
3. 只有同一时刻两边都要读时，才真的同时存在两个块（一次拷贝）；这是实现，不是模型。若把"同一值多种写法"提升成图里的写法等价（`writings.h`），图会进入非可执行状态并需要抽取，代价大于收益——放置需要的是**顺序**（资源边），不是等价推理，所以不这么做。

- `Device<T>` 保留为**同一表示的细化**（`DeviceType::get(T)` 现在就是包住宿主类型）；源码是否还能写它，是第 10 节问题 2。
- 设备块的 owner 记录在 Tiles 的设备层：流、事件、大小、释放回调。`mm::Block` 用 `adoptExternal(..., release)` 的释放回调释放 owner，块的析构即设备内存的析构。
- 原地写：`refCount() == 1`、输出类型与形状与输入相同、算子声明允许原地（`inplace` 契约）→ 写同一块；否则新块。**优化器每步 0 分配**是这条规则的可测后果（`tiles::inspect` 的 `allocations` 与 `materialized_values`）。

```gir
let d = upload(x)          // N1 :tiles/upload  (N0)      → 块 B1: device=0，同一个值 V
let y = d * 2.0            // N2 :tensor/multiply (B1)    → 块 B2: device=0，新值
save_npy(download(y), p)   // N3 :tiles/download (B2)     → 块 B3: device=-1
```

`x` 是值 `V`，`upload` 为 `V` 产生一个**设备块**（不是第二个值，不是第二个类型）；`x` 仍可读，它读的是主机块。块各自带 `device_`；"把 host 块的 device 字段改成 0"是不允许的——那会让别名分析、原地写和 DLPack 的归属同时失去意义。

### 3.2 驻留推断

驻留不是注解，是对整图的推断（`modules/tiles/src/residency.cpp`，新文件）：

- 每个块一个 owner：由产生它的节点决定（`upload`/设备算子/`exec` 输出 → 设备；主机算子/常量 → 主机）；
- 调用：callee 的形参跟随实参的块（同一块，不拷贝）；callee 的返回值按它自己图里的放置；
- 循环：`LoopBody` 的形参是设备值，状态跨轮保持；只在轮内用的中间值随轮回收；
- `#cpu` 块：入口一个 `download`、出口一个 `upload`（显式节点），每轮两次传输——测试**钉住这个数**，不让它悄悄变成每算子一次；
- 传输合批：沿用 H49a 的聚合（元组/结构体按叶子 `ACCS` 拆、再装回），一次调用最多一对整块传输。

### 3.3 传输节点与调度

传输是普通 `OPER` 节点（`:tiles/upload`、`:tiles/download`），带数据边；`upload`/`download` 已经用 `declareResources` 声明了资源（`module.cpp` 约 769–806 行），所以顺序由资源边保证，**不需要**新的调度语义。放置是节点/值的事实，调度器不推断放置，只按边决定就绪；**调度器也不决定提交**——它把段算子排到就绪，提交发生在段算子内部（下一个"立即返回"，见 §3.4）。

### 3.4 异步提交

- 提交点是 `tiles:exec` 算子内部：`submitProgram` 返回 outputs + completion；算子把 completion 记在输出块的 owner 上，**立即返回**设备值（pending）。
- 就绪：同一流上顺序即依赖；跨流（G7）用 event。调度器不等待。
- 主机观察才等：`download`、`println`/`save_npy`、主机算子读设备值、图的出口写回、`sync` 块结束。
- 失败：设备故障记进 fault word，在 `tiles:exec` 节点按 source origin 报出（tiles-route §3.6 已定），与今天的 `raiseFault(fault, graph, nodeRef)` 同一路径。
- 公共 API 不变：`run_device` 仍同步（H218 保留它作为显式路径）；"内部异步"只发生在编译段内部与段之间。

```gir
N10 :tiles/exec        [placement=gpu]   → 块 B4，owner{stream 0, event 17}，pending
N11 :tensor/mean (B4)  [placement=gpu]   → 同一段里，不等待
N12 :tiles/download (B4) [host]          → 等 event 17，拷贝到主机块
```

### 3.5 G2 的文件、测试与门禁

文件：`include/camel/core/mm/block.h`（`device_` 的文档与访问器）、`modules/tiles/src/resident_storage.cpp`、`device.cpp`、`module.cpp`（owner 记录与完成令牌）、新 `modules/tiles/src/residency.cpp`、`modules/tiles/src/region_outline.cpp`（段级传输）、`modules/tiles/src/schedule.cpp`（存储计划按放置）。

测试：`device_block_identity.cml`（上传后原值仍可读、放置不改变类型与名字、同一值的两个块各自带 device）、`device_inplace.cml`（`refcount==1` 原地、优化器 0 分配）、`resident_loop.cml`（状态跨轮驻留、每轮传输数）、`resident_cpu_block.cml`（每轮两次传输）、`async_scope.cml`（提交后主机先做事、`std::profile` 的 `syncs` 每段 1 次）、`device_fault_site.cml`（故障在 exec 节点报、origin 正确）。

门禁：`feat/modules/tiles`、`feat/modules/tensor`、`smoke`；GPU 用例用 `skip_unless = ["node", "test/tools/have-cuda-device.mjs", "0"]`，在 88 主机上由 `camel-remote.ps1` 跑。

---

## 4. G3：整段编译与缓存

### 4.1 编译单元

单元是**（作用域图, 放置）**：一个 `#gpu` 函数（或块）在它的传递闭包里、按 §2.3 的规则属于同一放置的所有图，一起编译成**一个** `Program`；段是它的入口。不是每个区域一次编译，也不是每个算子一次。

```gir
graph step                          body placement=gpu
  N20 :tiles/exec  With0=DATA(Function(step/seg#1))  Norm=(m, x)   [gpu]
  N21 :tiles/exec  With0=DATA(Function(step/seg#2))  Norm=(N20, …) [gpu]
  N22 :tiles/download (N21.0)                                     [host]
graph step/seg#1                    // 单元的入口 #0（段是入口）
  PORT m, x                         // 无标签的 helper 图也在单元里，成为另一个入口
  … :tensor/matmul / :tensor/relu …
graph step/seg#2                    // 单元的入口 #1
tiles.compiled_scope(step) = { fingerprint, entries: [seg#1, seg#2], shapes: {…} }
```

图上的后果：作用域图带目标事实 `tiles.compiled_scope`（`tiles.compiled_region` 的扩展：键沿用 `tiles.*` 的常量前缀，schema 号加一，现在是 `0x74696c6573000004`），事实里放整个单元的产物与各段入口；段的 exec 节点各带自己的函数常量，共享同一份产物。没有新增节点种类、没有新的调用机制。

迭代边界（段与段之间、单元与主机之间）：`#cpu` 块、主机效果、显式 `download`、多设备（G7）。边界上仍然是 `tiles:download`/`tiles:upload` 节点。

### 4.2 一次编译 vs 每形状

| 形状 | 编译期做什么 | 运行期（每次调用）做什么 |
|---|---|---|
| 符号维（`constas n`） | 一次源码编译（`n` 是内核参数），一次 NVRTC | 绑定期算 launch 几何、尾掩码、存储计划、按形状选的低层实现 |
| 具体常量形状 | 一次源码编译 | 同上，参数全是常量，可以做常量折叠 |
| `?`（Unknown） | **不**编译 | 按**形状签名**特化，入有界队列（LRU），跨调用复用 |
| 标量参数 | 不进签名 | 绑定期绑定（只有影响形状的标量才进签名） |

形状签名 = 每个入口张量 extents 值的向量 + 每个入口的秩；它只是特化表的键，**不是**编译单元的键。这样"符号维编一次"与"`?` 每形状一次"是同一条规则的两端，不是两套机制。

### 4.3 缓存

- 进程内：目标事实的 stamp 已经是图修订 + 声明纪元 + 目标哈希（`include/camel/runtime/facts.h`），这就是"编译一次"。
- 持久指纹（磁盘）由四段拼成：① 单元的规范化编码（`graph_encode.cpp` 的编码，含区域 attrs 与 body attrs 与节点 attrs）；② 用到的每个算子的**声明键 + 语义版本**；③ 目标身份（arch、NVRTC 版本、代码生成 schema 号、调度策略参数）；④ 放置。**不用源哈希**——技术 52 的界线：源哈希不能证明接口没变，声明键与版本可以。
- 存什么：`Program` 计划（不含 GC 指针、不含函数常量）、生成的 CUDA 源、特化表、记录的输出类型与形状。加载时**重新校验**：指纹（图与声明）+ 记录的类型/形状 vs 当前推断，任何一项不一致 → miss 并重编。
- 失效：图修订变（编辑）、声明纪元变（模块重编译）、目标变（arch/NVRTC/schema/策略）、放置变。形状签名变化只增一条特化，不失效。
- 磁盘位置与布局沿用构建产物的约定（`CAMEL_HOME`/缓存目录），带版本目录；命中时 `std::profile` 的 `deviceCompiles` 为 0——这是验收可以断言的。

### 4.4 显存池

Draft §1 记下的缺口是"设备 64 MiB / 32 项"：训练规模下每步的中间值直接 `cudaMalloc`/`cudaFree`。G3 把它换成按大小分级的池（`tiles` 的设备层，块 owner 从池里取、释放回池）：

- 分级（小/中/大 + 精确匹配的大块），每级保留高水位，跨步复用；
- 一步结束时只回收不被设备块引用的块，池的高水位进 `std::profile`（新字段，与 `deviceCompiles` 并列）；
- 与驻留（§3.2）正交：驻留决定"哪块还活着"，池决定"活着的块从哪来"；
- 验收断言：训练步稳态下每步 `cudaMalloc` 次数为 0（profile 的池统计），显存峰值不随步数增长。

### 4.5 G3 的文件、测试与门禁

文件：`modules/tiles/src/{compiled_region.cpp → compiled_scope.cpp}`、`codegen_cache.cpp`（持久键）、新 `modules/tiles/src/cache_store.cpp`（磁盘）、`modules/tiles/src/schedule.cpp`（特化参数）、`modules/tiles/src/device.cpp`（池）、`src/compile/gir/graph_encode.cpp`（规范化编码作为指纹输入）、`modules/tiles/src/module.cpp`（会话缓存目录）、`docs/cli.md`（缓存参数，若加）。

测试：`scope_one_compilation.cml`（一个作用域一次编译：`inspect` 一份产物、`deviceCompiles==1`）、`scope_symbolic_shapes.cml`（`constas n` 两个不同 n → 一次编译、两次 launch）、`scope_unknown_shapes.cml`（`?` 两个形状 → 两个特化、有界）、`scope_cache_disk.cml`（两次进程运行，第二次 0 次编译）、`scope_cache_invalidate.cml`（改 helper 源、改声明、换 arch 各 miss 一次）、`memory_pool.cml`（稳态每步 0 次直接分配、峰值不涨）。

门禁：`feat/modules/tiles`、`smoke`；磁盘缓存用例只在 GPU 主机上跑（第二次运行要新进程）。

---

## 5. 返回图带类型与形状；启动校验

### 5.1 输出类型与形状

- 端口类型保留 extent form；调用点绑定实际 extents 后，静态已知的部分写进 exec 节点的 `dataType()`，运行期构造返回值时用绑定期算出的 extents，不再重新推断。符号维（`constas n`）在调用点代换成具体值。
- 形状不符（调用点给的 extents 与静态结果类型矛盾）→ `RuntimeDiag::TensorDimensionMismatch = 0x000009`，消息含参数序号与两边的形状。
- `tiles::inspect` 的 JSON 增加每个段的入口/出口类型与形状（测试与缓存校验都读它）。

### 5.2 反向图的类型

`vjp.cpp::addTensorOper` 不再用 `TensorType::Default()`，改用**算子自己的结果类型解析**（`OperatorResolver::resolveForCall`，与编译器解析同一个算子调用时走同一条路，取 `OperatorIndex::resultType()`）：VJP 节点的类型 = 该算子对给定输入类型的推断结果，切线与 primal 同元素类型与形状（`gradient_ops.cpp::tangentType` 已经是"同形，整数/布尔除外"）。这样反向图的节点能被区域分析看见，动态形状也能走通。

### 5.3 启动设备校验

- 核心加一个校验注册：`execute::registerDeviceValidator(module, fn)`，与 `registerModulePass` 同一注册表风格；驱动在设备选择之后、**任何图被执行之前**调用全部已加载模块的校验器。
- Tiles 注册的校验器问 CUDA 的设备数，逐个检查 `--devices` 的序号；缺失 → `RuntimeDiag::DeviceUnavailable = 0x000070`，原文已写好（"CUDA device {0} was named by --devices but is not available: {1}"），一次列出所有缺失项，退出码 1。
- `--devices cpu` 永远通过；没有 `--devices` 时不校验（设备是偏好，主机是退路）。
- 校验只在模块已加载时发生：流水线里有 `tiles::compile` 就一定会加载 Tiles；只写 `--devices 0` 而没有 Tiles 遍时，设备列表无人使用，不校验（不假装）。

### 5.4 文件、测试与门禁

文件：`include/camel/execute/devices.h`（注册）、`src/execute/devices.cpp`、`tools/camel-cli/config.cpp`（调用点）、`modules/tiles/src/module.cpp`（校验器）、`modules/tensor/ops/vjp.cpp`、`modules/tensor/ops/gradient_ops.cpp`。

测试：`devices_missing_at_startup`（negative：`--devices 0` 在没有设备 0 的机器上 → 退出码 1、0x70、点名 0；用环境变量把序号指向不存在的一张）、`devices_all_named`（列出两个，缺第二个也一次报出）、`device_unavailable_no_side_effects`（程序没有产生任何输出）、`return_types_and_shapes.cml`（`inspect` 的输出类型/形状；动态形状调用）、`backward_typed.cml`（反向图 dump 里节点有 `Tensor<f32, [n,4]>` 而不是 `Tensor`）。

门禁：`feat/modules/tensor`、`feat/modules/autodiff`、`smoke`、`feat/modules/tiles`。

---

## 6. 验收

### 6.1 程序、数据与机器

- C2：`showcase/c2-mlp-mnist/`（`train.cml`，`compare.py` 跑 Camel 与 `baseline/train_torch.py`），数据由 `make_data.py --seed` 生成，`--profile small` 与 `standard` 两档。
- `transformer_step`：`benchmarks/transformer/model.cml` 的 `train_step`/`step`，权重与输入由 `benchmarks/common/{models,weights}.py` 按 seed 1004 生成。
- 机器：88 主机（8×3090）；构建与运行只走 `camel-remote.ps1`；GPU 用例用 `have-cuda-device.mjs` 守门，CI 上跳过而不是失败。

### 6.2 精度与参照

- 两边都是 f32；**参照在同一种设备上**：PyTorch 的 `model_torch.py`/`train_torch.py` 增加 `--device cuda`，Camel 用 `--devices 0`；CPU 的 PyTorch 结果单列一行（它说明"设备差异有多大"，不作为闸门）。
- 相同的数据、相同的初始权重、相同的步数；Camel 的随机性只来自数据与权重（算子本身确定），所以不需要额外的种子控制。

### 6.3 容差

| 检查 | 工具 | 容差 |
|---|---|---|
| C2 初始验证损失 | `compare.py` | 相对 `1e-5` |
| C2 每个记录步的训练损失 | 同上 | 缩放的相对 `1e-2`（单位下界） |
| C2 最终验证损失 / 准确率 | 同上 | 相对 `1e-2` / 绝对 `1e-2` |
| transformer 梯度（逐权重） | `benchmarks/train.py --check` | 逐权重 `max abs diff / max(abs ref, floor) <= 1e-4`，floor = 模型梯度尺度的 `1e-3` |
| transformer K 步轨迹（损失与最终参数） | `train.py --check-ksteps` | `1e-4` |
| 前向输出 vs NumPy 参照 | `benchmarks/common/check.py` | `allclose(rtol=1e-4, atol=1e-5)` |

出现超界时先用 `benchmarks/tools/grad_referee.py` 分诊（float32 与 float64 三方对照、relu 拐点、有限差分），再决定是改实现还是改口径——不直接放宽容差。

### 6.4 放置断言（"每个算子都在 GPU 上"怎么测）

`std::profile(json=…)` 已经记录每个设备内核、每次传输、每次同步与每次 NVRTC 编译（`include/camel/profile/profile.h`），`hints` 列出留在主机上的逐元素循环及原因。验收断言：

- 训练步的循环里 `kernels > 0`，且 `toDevice`/`toHost` 每步是**常数**（不随层数增长）；
- 每步 `syncs` 是常数（段数 + 出口的 1 次），不随算子数增长；
- 算子表里**没有**张量算子（被编译进段的算子不经过 `profiledOperator`）；`hints` 为空；
- `tiles::inspect` 的段表：每个 `#gpu` 作用域里的主机段数 = 0（除非显式 `#cpu`），传输节点只在边界；
- 反向与优化器：`with_grad`/`sgd` 的段在同一份产物里（`inspect` 的 entries 包含它们），优化器每步 `allocations == 0`。

### 6.5 支持面的缺口清单（H50 的输入）

| 程序用到的算子 | 今天 | 谁补 |
|---|---|---|
| `matmul`、`add`、`multiply`、`sum_axis`/`mean`、`relu`、`exp`、`log`、`maximum` | 在表里 | — |
| `permute`/`transpose`/`reshape`、`softmax`、`gelu`、`layer_norm`、`mean`、`cross_entropy` | 不在表里 | H50（视图与逐元素/归约组合） |
| `cast`（f32↔f32 以外）、`sgd`/`adam` 的逐元素更新 | 部分 | H50（元素类型与原地契约） |
| `with_grad`/`grad` | 构图机制，不进设备表；今天在运行期建派生图 | G1（§2.6 的 `autodiff::expand`） |

H49 交付时，"每个算子都在 GPU 上"的闸门按支持面内的算子全绿；表外算子逐条列在上表里，H50 完成后同一脚本零缺口。这就是 H49 与 H50 的分界：**H49 让"放上去"成为可测的、正确的事，H50 让"放上去的算子足够多"**。

### 6.6 门禁与命令

```powershell
# 构建与窄门禁（远端）
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h49-gpu-plan -Plan smoke -Jobs 8

# C2（GPU）
python showcase\c2-mlp-mnist\compare.py --profile standard --torch-device cuda --camel-devices 0

# transformer（GPU）
python benchmarks\train.py --check --models transformer --device cuda --camel-devices 0
python benchmarks\train.py --check-ksteps --models transformer --device cuda --camel-devices 0
```

（`--torch-device`/`--camel-devices` 是本计划要加的两个参数：`compare.py` 的 `camel_arg_command` 把 `--devices 0` 放在目标文件之前；`train.py` 的 `CAMEL_CONFIGS` 加一条 GPU 配置，`run_camel_full` 把 `--devices` 传下去，`model_torch` 按 `--device` 把权重与输入搬到 CUDA。GPU 配置的遍序是 `std::macro autodiff::expand tiles::compile std::nvm`：`std::macro` 先展开宏，`autodiff::expand` 必须在 `tiles::compile` **之前**——派生图只有在遍里建出来，才会被当成同一个单元的图（§2.6）；顺序反了 `tiles::compile` 会报缺少前置遍并点名 `autodiff::expand`。）

---

## 7. 提交顺序与可独立落地

| 提交 | 内容 | 依赖 | 独立落地 |
|---|---|---|---|
| 1 | S1 启动校验（0x70 在启动时） | 无 | 是 |
| 2 | S2 放置键与两个标签（无消费者） | 无 | 是（H218 已合则改它声明的键，未合则先落自己的声明） |
| 3 | S3 放置解析与作用域边界 | 2 | 是 |
| 4 | S4 拒绝与 0x71（接管 H218 的判定） | 3 | 是 |
| 5 | S5 图内区域（段）与派生图展开（`autodiff::expand`） | 3、4 | 是（两个提交：5a 段，5b 展开遍） |
| 6 | S6 设备块与值模型 | 无（可与 1–5 并行） | 是 |
| 7 | S7 驻留与传输 | 6 | 是 |
| 8 | S8 异步提交与就绪 | 6 | 是 |
| 9 | S9 整段编译 | 5、7、8 | 是 |
| 10 | S10 形状签名与持久缓存 | 9 | 是 |

H218 在途：它删的是旧 API、加的是 `tiles.gpu` 与 0x71。若它先合入，本计划的 2/4 是在它上面改键与扩展；若它后合入，先落 2/4 会与它冲突，所以**顺序上 H218 先合**，否则本计划的 2/4 等它。

每个提交都能单独回退：S1 只加校验；S2 只加事实；S3/S4 只在 Tiles 内部生效；S5 之后 `#gpu` 才有语义；S6–S10 各自带开关（`#gpu` 之外的图不受影响）。

---

## 8. 破坏面

- **语言**：新增 `#gpu`/`#cpu` 的语义（此前只有 H218 的 `#gpu` 单标签且立即拒绝）；删除已在 H218 里做的 `@gpu`/`run`/`run_device` 不动。`Device<T>` 的写法见问题 2。
- **标准库**：`stdlib/tiles.cml` 的导出面（H218 已改）不变；若问题 2 决定保留 `Device<T>`，签名保持不变。
- **运行图**：region 0 可以带 attrs（`regionCount` 对只带标签、无控制流的图从 0 变 1）；`ControlRegions`/dump/encode/validator 都容忍（§2.2 已逐个看过调用点）。
- **测试**：`gpu_regions.cml`、`gpu_fallback.cml`、`async_execution.cml` 等 H218 已在重写，本计划在其上再加段/放置断言；`std::gir` 的 golden 会因 `attrs=` 与 body 区域行而变，属于预期。
- **内部接口**：`RegionAnalysis`/`Region` 改成"段"、`outlineRegions` 的转移位置、`tiles.compiled_region` → `tiles.compiled_scope`、`compileTypes` 的调用点。
- **明确不破坏**：无标签程序的行为；`--devices cpu` 的现有机型；`upload`/`download`/`run_device` 的同步语义；`std::profile` 的既有字段。

---

## 9. 风险

1. **形状特化爆炸**：`?` 的程序每个形状一份特化。缓解：有界队列 + LRU + 磁盘缓存；验收里断言"符号维只编一次"。
2. **首次调用的 NVRTC 编译时间**：Draft §4 已定"自适应池"；G3 的验收把 `deviceCompiles` 记录在 profile 里，首次与稳态分开报，不让冷启动混进每步时间。
3. **磁盘缓存的错误命中**：指纹漏掉某个输入（新加的节点种类、新加的属性）就会命中旧产物。缓解：schema 号进指纹；加载时用记录的类型/形状重新校验；失效矩阵测试。
4. **H218 冲突**：见第 7 节；顺序上先合它。
5. **内联与 `#gpu`**：`std::opt::inline` 把 callee 的节点搬进调用者时，节点属性随节点走（文档不变式），所以放置不丢；但要有一个用例钉住（`placement_callee` 的 inline 变体）。
6. **异步的错误时机**：设备错误在 exec 节点报，而不是在真正出错的主机行；这是设计选择（tiles-route §3.6），测试钉住 origin。
7. **展开遍的图引用**：`buildGradientGraph` 现在按 program 建图（`program` + `graph` 参数），在遍里要把它接到 draft 组（挂同一 session、必要时新建 program）；这是 S5b 里最需要先做原型的一处，失败的话退回"派生图在首次调用时编译"（同一套缓存键，但首步有编译延迟，验收的 `deviceCompiles` 会把首步与稳态分开）。
8. **展开的代码体积**：每个调用点一份派生图（`grad(f)` 出现两次就有两份）。先按调用点展开，若体积成为问题再按图共享（同一图 + 同一放置只建一份）。

---

## 10. 给作者的问题

1. **模块位置的标签**：`Positions` 现在只有 `Func|Type|Field|Nodes|Param`，源码无法给 `module` 写标签，而设计原则 13.4 与 Draft §2 都写了"函数、块、模块"。G1 是否必须先加"模块位置"（`on module` + 语法 + 传播到模块的每个顶层函数），还是本轮只到函数/块，模块作为后续条目？
2. **`Device<T>` 是否仍可在源码里写**：作为签名上的放置断言（`func f(x: Device<Tensor<f32>>)` 表示"必须在设备上"，传普通 `Tensor` 时自动 `upload`），还是收成内部表示、源码只写 `Tensor`？
3. **`#gpu` 作用域里的主机效果**：`print`/`save_npy`/文件写这类效果，是编译期报错（我建议，0x71，一条诊断列全部），还是允许并在边界自动 `download`？
4. **`--devices cpu` 下的 `#gpu` 作用域**：按同一支持面编译、由主机后端执行同一段图（我建议），还是报"设备不可用"？这决定同一个程序在有无设备时是否编译结果一致。
5. **空作用域**：`#gpu` 块里没有设备算子（全常量折叠、空块）时，是否沿用 9.4 的"标签没有读者"警告口径，还是再加一条"标签没有效果"的警告？

---

## 11. 文件清单

### 新增

- `docs/design/gpu/plan.cn.md`（本文）
- `modules/tiles/src/residency.cpp`（驻留推断）、`modules/tiles/src/cache_store.cpp`（磁盘缓存）、`modules/autodiff/expand.cpp`（编译期派生图展开遍）
- `test/cases/modules/tiles/placement_tags.cml`、`placement_branch.cml`、`placement_loop.cml`、`placement_callee.cml`、`placement_grad.cml`、`placement_grad_expand.cml`、`placement_grad_dynamic.cml`、`placement_reject.cml`、`placement_no_reader.cml`
- `test/cases/modules/tiles/device_block_identity.cml`、`device_inplace.cml`、`resident_loop.cml`、`resident_cpu_block.cml`、`async_scope.cml`、`device_fault_site.cml`
- `test/cases/modules/tiles/scope_one_compilation.cml`、`scope_symbolic_shapes.cml`、`scope_unknown_shapes.cml`、`scope_cache_disk.cml`、`scope_cache_invalidate.cml`、`memory_pool.cml`
- `test/cases/modules/tiles/devices_missing_at_startup.cml`（negative）、`return_types_and_shapes.cml`、`backward_typed.cml`
- `test/plans/feat/modules/tiles_gpu.plan.toml`（GPU 守门的用例）

### 修改（主要）

- `include/camel/core/tags.h`、`src/core/tags.cpp`（`NodeTagSpec.attrKey`）
- `src/compile/gir/builder.cpp`（`stampNode`、body region attrs）、`src/compile/gir/builder_control.cpp`（Block 区域）
- `src/runtime/draft_regions.cpp`（`setRegionAttrs(0, …)`）、`include/camel/runtime/graph.h`（region 0 的 attrs 访问）
- `src/compile/gir/graph_encode.cpp`（编码含 r=0 attrs）
- `modules/tiles/src/{regions.cpp,graph_regions.cpp,region_outline.cpp,region_pass.cpp}`、`modules/tiles/include/tiles/internal/regions.h`
- `modules/tiles/src/{module.cpp,device.cpp,resident_storage.cpp,schedule.cpp,compiled_region.cpp}`（→ `compiled_scope.cpp`）、`codegen_cache.cpp`
- `modules/tensor/ops/vjp.cpp`、`modules/tensor/ops/gradient_ops.cpp`、`modules/autodiff/{expand.cpp,module.cpp,pullback.cpp,pullback.h,operators.cpp}`
- `include/camel/execute/devices.h`、`src/execute/devices.cpp`、`tools/camel-cli/config.cpp`
- `include/camel/profile/profile.h`、`src/profile/profile.cpp`（池的高水位与直接分配计数）
- `showcase/c2-mlp-mnist/compare.py`、`benchmarks/train.py`、`benchmarks/common/camel.py`（`--devices`/`--device`）
- `docs/cli.md`（缓存与池的参数，若加）、`test/plans/feat/modules/tiles.plan.toml`、若干 `std::gir` golden

### 删除

- `modules/tiles/src/compiled_region.cpp`（被 `compiled_scope.cpp` 取代）
- `tiles.compiled_region` 目标事实键 `0x74696c6573000004ULL`（被 `tiles.compiled_scope` 取代）