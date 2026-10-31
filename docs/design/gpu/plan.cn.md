# GPU 执行计划：G1–G3 —— 放置标签、设备上的数组值、驻留与异步、整段编译与缓存

状态：**计划，待协调者批复**。本分支只改文档：不含任何实现，不改变任何被观察到的行为。基线 `9e71310ab`（2026-10-22，`origin/develop`）。

依据：

- [GPU 设计稿](README.cn.md) §1（缺口表）、§2（放置与作用域）、§3（设备上的数组值）、§4（整段编译与缓存）、§6（三条决定）、§7（G0–G7 的顺序与编号）；
- [设计原则](../../design-principles.cn.md) 3.1（按依赖求值）、3.6（由声明的性质并行）、3.10（循环）、6.2（标签只声明事实）、9.4（标签的读者与无效果警告）、9.6（优化保持算子身份）、10.5（库的边界与数据块）、13.3（`Device<T>` 与异步不在语义里）、13.4（分布式与放置）；
- [检查意见] 对放置工作的三条硬要求：动态形状必须能编译、反向图必须带同元素类型与形状、设备不可用必须启动时报错；
- [一轮检查意见]（`#independent`、融合记在图的分组属性上）；[后一轮检查意见] 的 Q5（删除 `@gpu`/`run`/`run_device`，由本计划执行）与 Q6（只有 `--devices`，没有 `tiles::compile(device=)`；源码里的设备是放置标签，永远不是遍参数）；
- [模块缓存与失效面](模块缓存的成本账（原技术页 52，已删除）)：源哈希不能证明接口未变，这是缓存键的界线；
- [Tiles 路线的区域与传输](../tiles-route/README.cn.md) §3.2–§3.6（区域 → `OPER :tiles/exec`、显式 upload/download、符号形状编一次、键是语义指纹、故障记在调用点）；
- [索引化计算](../indexed-computation/README.md) §6.3（异步：完成事件、依赖保证完成与可见性、没有隐式同步）；
- 在途的 GPU 拆除工作：删除 `@gpu`/`run`/`run_device` 与旧分派，落 `tiles.gpu` 与 `RuntimeDiag::GpuScopeUnsupported = 0x000071`。本计划按它合入后的树写，第 7 节说明它未合入时的顺序。

本文的每个结论都对应到文件与类型；读它不需要读代码。第 11 节是会被触碰的文件的完整清单，第 10 节是给作者的少数几个问题。文中的 GIR 是压缩草图（节点、算子 URI、属性、区域与段），不是 `std::gir` 的原样输出。

---

## 0. 一页结论

任务六项，逐项的决定与**它在图上产生的后果**：

| 问题 | 决定 | 图上的后果 |
|---|---|---|
| 1. 标签与作用域 | `#gpu`/`#cpu` 写**同一个属性键** `tiles.placement`（Symbol 值 `gpu`/`cpu`，combine=inner）；作用域 = 区域：函数体（region 0）、带标签的块（`GCRegionKind::Block`）、Arm、LoopBody；边界 = 区域边界；一个节点恰好一个放置，内层胜出；嵌套由解析规则消掉、部分作用域结构上不存在，不是"静默地为错的设备编译"；派生图由 `std::macro` 在编译期展开建出并继承放置（§2.6）；不支持的算子、主机效果 → `GpuScopeUnsupported 0x000071` | 被标记的节点带属性；函数体区域 0 带属性；作用域内每个段在图上是一个 `OPER :tiles/exec` 调用，跨作用域边上是显式 upload/download 节点；分支的 BRCH/JOIN 留主机侧，每个臂一个段；`grad(f)` 的调用点换成持有派生图的函数常量 |
| 2. 设备数据块 | **就是普通数组值**：`mm::Block::device_` 填 DLPack 的 device；**没有第二个类型、没有第二个用户可见的名字**："同一值两种放置"是同一个值在两个用途上的放置事实，拷贝是编译器的实现（普通的值写入），不是第二个值；`Device<T>` 若保留只是签名上的放置断言（问题 2） | 传输与拷贝由遍插成节点（`:tiles/upload`/`:tiles/download`），不是隐藏同步；块的引用计数与原地写不变；优化器每步 0 分配 |
| 3. 驻留与异步提交 | 驻留**推断**：整图（含循环、调用、with 参数）上的 owner 表；传输只在放置变化处由遍插成节点；调度器只决定**就绪**，提交发生在 `tiles:exec` 算子内部；异步 = 调用返回带完成令牌的设备值，主机观察时等待；无新语言原语 | 段的 exec 节点返回 pending 设备值；完成令牌记在设备块的 owner 记录上；数据边 + 资源边给出顺序，没有隐藏同步；故障在 exec 节点按 source origin 报出 |
| 4. 整段编译与缓存 | 编译单元 = **（作用域图, 放置）**，含传递闭包里同一放置的被调图；一个作用域一次 `compile`，段是入口；符号维（`constas n`）编一次，形状签名只决定特化参数；`?` 按具体形状入有界队列；缓存键 = 图规范编码 + 声明键与语义版本 + 目标身份（arch/NVRTC/代码生成 schema/策略）+ 放置，**不用源哈希**；stamp = 图修订 + 声明纪元 + 目标哈希 | 作用域图带目标事实 `tiles.compiled_scope`（`tiles.compiled_region` 的扩展）；段的 exec 节点各带自己的函数常量，共享同一份编译产物；第二次调用命中，不再编译 |
| 5. 返回图带类型与形状；设备启动校验 | 端口类型带 extent form（`constas n`），调用点绑定实际 extents 并把输出类型/形状写回节点；反向图的节点用**算子声明的类型推断**而不是 `TensorType::Default()`；`--devices` 在启动时对**每个**点名序号校验，缺失 → `DeviceUnavailable 0x000070` 原文 | 输出值构造时不需要再推断类型；反向图的节点带同元素类型与形状（也就能被区域分析看见）；启动校验失败时没有任何图被执行 |
| 6. 验收 | C2 与 `transformer_step`：f32 两边同精度，PyTorch 在 CUDA 上作参照；C2 用 `compare.py` 的四条容差，transformer 用 `train.py --check/--check-ksteps`（1e-4）与 `grad_referee.py` 分诊；放置断言用 `std::profile` 的 JSON（kernels/toDevice/toHost/syncs/hints）与 `tiles::inspect` 的段/传输表；支持面缺口逐条列成支持面清单 | 验收脚本现在就固定下来；本计划的每步只对自己的窄门禁负责，程序级闸门在支持面补齐后零缺口 |

十个步骤，按顺序：

| 步骤 | 内容 | 能否单独落地 | 门禁 |
|---|---|---|---|
| **S1** | 启动设备校验（核心的校验注册 + 驱动调用 + Tiles 的校验器） | 能，不改语言与图 | `smoke`、`feat/modules/tiles` 的错误路径用例 |
| **S2** | 放置键、两个标签、body region、全节点盖章（无消费者） | 能，不改行为 | `feat/compile` 的 GIR 快照、`inspect --tags` |
| **S3** | 放置解析、作用域边界与 `inspect` 输出 | 能（S2 之后） | 新增 `placement_*` 用例，`--devices cpu` |
| **S4** | 拒绝：不支持的算子、主机效果、无法确定放置的节点 | 能（S3 之后），此时 `#gpu` 首次有语义 | 负向用例的退出码与诊断文本 |
| **S5** | 图内区域（按控制区域切段，分支、循环、调用）与派生图展开（`std::macro`） | 能（S4 之后），本项第一步真正改变执行 | `tiles` 全套 + 分支/循环用例 + 展开前后逐位相同 |
| **S6** | 设备块与值模型（`device_`、owner 记录、原地写、DLPack） | 能（S5 之后） | `feat/modules/tiles` 的设备路径 |
| **S7** | 驻留推断与传输节点（owner 表、循环状态、调用边界、合批） | 能（S6 之后） | 传输计数用例、`inspect` 的 transfers |
| **S8** | 异步提交与就绪（pending 值、停等点、故障位置） | 能（S6 之后） | `syncs`/`kernels` 计数、`async_execution.cml` 的既有断言、`plan_check` 的传输边界 |
| **S9** | 整段编译：单元 = (作用域, 放置) 的闭包；段入口；一个作用域一次编译 | 能（S5+S8 之后） | `inspect` 一个 scope 一份产物；编译计数 = 1 |
| **S10** | 形状签名字典与持久缓存（磁盘、重新校验、失效） | 能（S9 之后） | 符号维只编译一次；两次运行第二次 0 次编译；失效矩阵 |

**不改变行为**：未带标签的程序的行为、区域抽取对未带标签图形的现有结果、`--devices cpu` 的现有机型、公共 `upload`/`download`/`run_device` 的同步语义（保留它们作为显式路径）、`std::profile` 的既有字段。

**不在本项**：支持面（bf16/Tensor Core、张量核、融合、优化器多张量融合、CUDA 图）——那是支持面那一项；多卡 `#shard`——G7；分支与循环**在设备上执行**——G4（本计划只把它们的**段**放到设备上，BRCH/JOIN/LOOP 留主机）；`std::prl` 的并行放置。

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

C2（`showcase/c2-mlp-mnist/train.cml`）与 `transformer_step`（`benchmarks/transformer/model.cml` 的 `train_step`/`step`）用到的算子远多于 `operations()` 里的那张表：`permute`/`reshape`/`transpose`（视图）、`softmax`、`gelu`、`layer_norm`、`mean`、`cross_entropy`、`sgd`/`adam`、`cast`。按板上的分工，**支持面是另一项工作**，本计划只做 G1–G3。因此本计划的验收分两段：本计划每步的窄门禁，与程序级闸门（脚本现在就固定，缺口清单就是支持面那一项的输入），见第 6 节。

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
- 上一版的 `tiles.gpu`（Unit）被这条取代：键名从 `tiles.gpu` 变成 `tiles.placement`，因此**读键的地方只有一处**（Tiles 自己），上一版的 `refuseGpuScopes` 改成读这个键。
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

### 2.4 拒绝：不支持的算子与无法确定放置的节点

`deviceCapable(oper)` = 在 `operations()` 表里 ∧ pure ∧ 无外部效果。现有实现已经用它拒绝 `#gpu` 作用域并报 `RuntimeDiag::GpuScopeUnsupported = 0x000071`；G1 把判定扩到：

- 传递闭包里的**每个**被调图（不只是作用域自己的图）；
- 作用域里**无法确定放置**的节点（老代码/第三方建图没有盖章）。落地上这一条的形态要说清：`stampNode` 让**每一种**节点创建时都写 `currentAttrs_`，作用域对被调图又是传递闭包（§2.3），所以"节点没有自己的事实"一律由作用域回答（设备），不存在第三种"不知道"的状态；一个作用域**给不出去处**的节点，就是既没有放置事实、也没有设备程序的那一个，它以 `GpuScopeNoDeviceProgram`（闭包里的模块不提供设备程序）报出。

**主机效果不是拒绝，是作用域边界**（取代本文初稿的"一律拒绝"）：`print`/`println`、文件写、`save_npy` 这类效果算子**不参与设备段**，它们放在**主机**上，观察点由遍插一个**显式的 `download`** 节点，`std::profile` 的 `syncs` 把它数出来。理由：作用域承诺的是"这一段计算在哪跑"，不是"这一段程序没有主机观察"；效果算子没有设备实现，但它也不需要设备实现——它是段与主机之间的那一次传输。所以：

| 作用域里的节点 | 处置 |
|---|---|
| 有设备实现的纯算子 | 进段，在设备上跑 |
| 主机效果算子（`print`、文件写、`save_npy`） | 留主机，边界处一个显式 `download`（`syncs` 计数） |
| 没有设备实现、也不是主机效果的算子 | 拒绝：`GpuScopeUnsupported`；闭包里的模块没有设备程序时是 `GpuScopeNoDeviceProgram` |

落地状态要说清：**两半都已落地**（`#gpu` 里写 `println` 不再报 `GpuScopeUnsupported`，作用域照旧是一个设备单元；效果写在作用域体内时照常执行，观察点有一次显式传输）。修的是**切分**，执行计划不动：

- 执行计划（`src/execute/graph_runtime_support.cpp` 的 `computeExecutionOrder`）从图的出口往回走，只认数据边、同步边与顺序边，"哪些节点要跑"不额外认别的根：同步块之外、结果没人用的带效应调用**本来就不执行**（设计原则 3.1），把它当根会改变整门语言的语义。所以段被抽出去之后，宿主语句到出口的那条链必须不断在段调用这里。
- 断链的地方是切分：段把成员语句换成段调用，就要把被替换语句的**同步输入、顺序输入**原样接到替换它的节点上（`docs/design/effects/edges.cn.md` §4），并且它**排序过的**那些节点改由段调用排序。顺序在今天的图里由两种边并行携带到控制边退场为止（控制边与同步边同写一条顺序，`src/runtime/draft.cpp` 的 `closeChainOver`），所以两边都要接。实现在 `modules/tiles/src/region_outline.cpp`：段成员的控制前驱接到调用上，成员控制过的节点把该成员换成调用——只动这一份顺序引用，图的出口、区域的值、分支的 JOIN 这些引用留给遍下面按值替换，否则出口会被提前改成调用而不是它该有的值。
- 切分不必另加"切点"：`partitionRegions` 的合并只沿**数据**输入进行，商图的无环判定按输入 + 顺序做（`RegionNode::order` 来自 `graph.ctrlInputsOf`），效果横在两个成员之间时那次合并被拒，段于是在效果处**自然分开**——`observed` 落地成两段（这一形状的读数：`device kernels` 3、`compiles` 2、`run_preferred_resident` 2 次）。效果读的那个值的 `tiles:download` 在那之前就已经在图上（`std::jgir` 里 `observed` 有它，`modules/tiles/src/graph_regions.cpp` 的 `analyzeRegions` 与 `regions.cpp` 的 `planRegionTransfers` 都不认识执行计划），缺的一直只是那条顺序边。§3.6 的第 6 条要把这张表换成驻留结论，那时"效果是观察点"要在新口径里重新落。

改之前：`#gpu` 函数体里的 `println` **不执行**——效果节点和它读的那次 `download` 都不在执行计划里，所以那一次传输也没发生（有卡时 `to_host_bytes` 只有 12、`syncs` 2），`--devices cpu` 与有卡时都不打印；改之后：两种运行都打印。有卡时（一块卡、一条流，公共同步路径）`syncs` 从 2 变 3，**多出来的那一次就是观察点**：一次上传（`tiles:retain_preferred` 把实参送上卡）加两次下载（效果读的值、`main` 读回的返回值），`to_host_bytes` 从 12 变 20。`syncs` 数的是真实的等待（按值计的数是 `downloads`/`transfers`），所以钉数要在固定的流数下取；整程序最少 2 次——设备段的输入张量必须上传，体内**没有**效果的对照程序也是 2。`TILES_RESIDENCY` 读的是放置事实而不是遍插入的节点，所以它对 `observed` 照旧记 `downloads: 0`（效果节点带的是作用域的 `gpu` 事实）。传输节点在两种运行下都写出来，有没有设备是**运行时**决定：`--devices cpu` 下一次传输都不写、`syncs` 记 0，效果照样打印。

诊断形状：一条诊断列出作用域里**所有**不合格算子（URI + origin + 原因），给出两条出路（写 `#cpu` 块，或 `--devices cpu`），不让用户一条一条试。`--devices cpu` **不改变可编译集合**：支持面在编译期定死，`--devices cpu` 只决定"编出来的段在哪执行"（主机后端执行同一段图）。否则同一个程序在有无设备时编译结果不同，这就是"静默地为错的设备编译"。

**拒绝理由的主语就是这条边界的名字**：能算的一半要接上，不能算的一半要说出是**谁**缺。`bf16`/`f16` 的逐元素内核宿主已经有一份（`modules/tiles/src/serial_execute.cpp` 按元素走内核体，算出的位与手推一致），设备还没有一条读两字节元素的内核，所以它的理由叫 `device_element_kernel_missing`，句子是 `the device has no kernel for a bf16 element`——主语是设备；有设备时作用域按它拒绝，没有设备时（`--devices cpu`）宿主照常算出这些值，理由只留在计划里。写"内核还没写"是把我们自己的待办说成用户的错误，说"设备没有这条内核"才是这条边界的名字。它随目标而变，与上一段不冲突：定死的是**支持面**（哪些算子有设备实现），而 `multiply` 的支持面在着，缺的是它的一种元素类型在设备那一半的内核——能算的一半接上，不能算的一半按目标拒绝。

`#gpu` 写了而流水线里没有 `tiles::compile`：沿用 9.4 的 `SemanticDiag::TagHasNoReader = 0x100160` 警告。两边合起来，任何"标签被忽略"的情形都至少有一条诊断。

空作用域（`#gpu` 块里没有设备算子、全被常量折叠掉）**不加警告**（第 5 问）：那不是写错，`tiles::inspect` 显示 0 个段就够了；"标签没有读者"仍按上面那一条报。

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

### 2.6 派生图在编译期：`std::macro`，不新增遍

本文初稿这里写着"派生图只在运行期建、遍看不见，所以要新增一个展开遍 `autodiff::expand`"。**那个读数不成立，撤回**：`grad`、`with_grad`、`vjp` 在 `modules/autodiff/operators.cpp` 里登记为 **macro 算子**（`.macro = true`），`f` 编译期可知时，流水线里的 **`std::macro` 已经在编译期把它展开成导数图**（`pullback.cpp` 的 `macroCallee`），后面的遍都看得见它。教程第 16 节写的也是这个。

所以"作用域覆盖它的梯度"是这样成立的：

1. **不新增任何遍。** `#gpu` 作用域里的梯度，靠流水线里 `std::macro` 在 `tiles::compile` **之前**展开。`tiles::compile` 是这件事的**消费者，不是作者**：它不调用任何展开遍，也不改变流水线的写法。
2. **派生图继承 primal 的放置**（§2.3 的两条）：派生图创建时抄 primal 图 body 区域 0 的 attrs，VJP 新建的节点从它的 primal 节点抄 `tiles.placement`。因此派生图与 primal 一起进同一个单元（§4.1），`TILES_PLACEMENT` 里能看到它 `body=gpu-required`。
3. `tiles::compile` 在作用域里看到**未展开**的 `grad`/`with_grad`（函数操作数是编译期可知的函数常量，说明缺的是遍）→ 报"缺少前置遍"，消息里**点名 `std::macro`**（`RuntimeDiag::GpuScopeMacroNotExpanded`；本仓库对前置遍的既有做法：库的诊断点名缺少的遍，流水线永远是显式的）。这条诊断**先于** §2.4 的"不支持的算子"：前者有明确的修法（加一个遍），后者只是"这个算子放不上去"。
4. 作用域里 `f` **编译期不可知**的 `grad`（函数值来自参数、返回值或运行期计算）→ 编译期报错，`GpuScopeUnsupported 0x000071`，消息点名 `grad`/`with_grad` 与该调用点，说清"`#gpu` 作用域里的梯度需要编译期可知的函数"。这一条不是缺遍：任何遍都展开不了它。
5. `#gpu` 之外完全不变：不在作用域里的 `grad` 仍按运行期构建、解释执行。

```gir
# 展开前（流水线里没有 std::macro）
N30 :autodiff/with_grad  (DATA(Function loss))   [placement=gpu]   → 报缺前置遍，点名 std::macro
# 展开后（流水线：std::macro tiles::compile）
graph d(loss)          body attrs={tiles.placement=gpu}     // primal 的放置抄过来
  PORT m, x
  … 反向节点，类型由 §5.2 的规则给出 …
N30 :DATA(Function(d(loss)))                     [placement=gpu]   // 函数常量，不再有 with_grad 算子
N31 :call  (N30, m) (x)                          [placement=gpu]
```

这条撤回**不改变**本文其余部分：作用域、传播、段落、单元的定义都不动，S5b 因此只剩"放置的继承"与"缺少前置遍"的诊断，不依赖张量并进数组。

### 2.7 G1 的文件、测试与门禁

文件：`include/camel/core/tags.h`、`src/core/tags.cpp`、`modules/tiles/src/module.cpp`（两个标签）、`src/compile/gir/builder.cpp`（`stampNode` + body attrs）、`src/compile/gir/builder_control.cpp`（`visitExecNode` 建 Block 区域）、`src/runtime/draft_regions.cpp`（`setRegionAttrs(0, …)`）、`src/compile/gir/graph_encode.cpp`（含 r=0 attrs）、`modules/tiles/src/{regions.cpp,graph_regions.cpp,regions.h}`（按控制区域的段）、`modules/tiles/src/region_outline.cpp`（段调用）、`modules/tiles/src/module.cpp`（`deviceCapable` 与 0x71 的判定）、`modules/autodiff/{expand.cpp,pullback.cpp,pullback.h,operators.cpp,module.cpp}`、`modules/tensor/ops/vjp.cpp`。

测试（`test/cases/modules/tiles/`，计划 `test/plans/feat/modules/tiles.plan.toml`，`--devices cpu` 在 CI 上就能跑）：

- `placement_tags.cml`：`#gpu`/`#cpu`/嵌套/同值嵌套；`std::gir` 快照显示 `attrs=`；`inspect --tags` 显示两个 tag 与它们的读者；
- `placement_branch.cml` / `placement_loop.cml`：`#gpu` 函数里的 if/else 与循环，`tiles::inspect` 的段表是 2 个臂段 + 1 个循环体段，传输只在边界；
- `placement_callee.cml`：无标签的 helper 被 `#gpu` 与主机两处调用 → 两份单元（段表里两份 helper 图）；
- `placement_grad.cml`：`#gpu` 里的 `with_grad`，`std::macro` 展开之后派生图 dump 出 `placement=gpu`，且 `inspect` 的 entries 里有它；
- `placement_grad_expand.cml`：同一个 `with_grad(loss)` 在展开前/后跑出**逐位相同**的值（展开不改变语义）；
- `placement_grad_dynamic.cml`（negative）：`#gpu` 里对运行期才知道的函数调用 `grad` → 0x71，消息点名 `grad` 与该调用点；以及"流水线里没有 `std::macro`"→ 报缺少前置遍、点名它；
- `placement_reject.cml`（negative）：`print`、`sort`、无法盖章的节点 → 退出码 1、`GpuScopeUnsupported`、一条诊断列全部算子；`#gpu` + `--devices cpu` 给出**同样**的错误；
- `placement_no_reader.cml`（negative）：只有 `#gpu` 没有 `tiles::compile` → `TagHasNoReader` 警告。

门禁：`feat/compile`（GIR 快照）、`feat/modules/tiles`、`feat/modules/autodiff`、`smoke`。

### 2.8 G1 的施工步骤

编号仍按 §0 的 S1–S10。**S1–S5b 已落地，不重做**（判据见下表）；本节把 G1 剩下的工作排成可施工的两步，每步给目标、边界、依赖、验收读数与未决点。`modules/` 侧的施工顺序见 [plan-modules.cn.md](plan-modules.cn.md)（M0–M10），但它的 M3 已被复核撤回——派生图由 `std::macro` 在编译期展开，**本项不新增遍**，按 §2.6 写。

**已落地（判据是文件与符号，不是看板）**

| 项 | 判据 | 未落地的部分 |
|---|---|---|
| 设备选择与启动校验（S1） | `include/camel/execute/devices.h` 的 `DeviceSelection`/`DeviceValidator`；`src/execute/devices.cpp` 的 `parseDeviceSelection`、`selectDevices`、`registerDeviceValidator`、`validateSelectedDevices`（`--devices cpu` 与不给 `--devices` 都在这里直接通过）；`modules/tiles/include/tiles/internal/capability.h` 的 `CudaDeviceAvailability`、`DeviceUnavailable`、`configuredCudaOrdinal`；`RuntimeDiag::DeviceUnavailable = 0x04000070`。计划条目 `test/plans/feat/modules/tiles.plan.toml` 的 `device_startup.*` 六条 | 无 |
| 一个键、两个标签（S2） | `src/core/tags.cpp` 里 `attr::registerKey("tiles.placement", {Symbol, inner(), equalOnly()})`，`#gpu` 与 `#cpu` 两个 `Decl` 用**同一个** `key`、`readBy = {"tiles::compile"}`；`src/compile/gir/builder.cpp` 的 `setRegionAttrs(0, bodyTags.nodes)`（函数体）与 `enterBlockScope` 一侧的 `GCRegionKind::Block` + `setRegionAttrs(block, blockTags.nodes)`；`src/compile/gir/graph_encode.cpp` 编码 region 0 的 attrs。用例 `test/cases/modules/tiles/gpu_scope_segments.cml`、`placement_query.cml` | 标签的落点仍含模块位置（G1-1） |
| 放置解析与报告（S3） | `modules/tiles/include/tiles/internal/placement.h` 的 `Placement`/`PlacementScope`/`PlacementDecision`、`placementOf`、`bodyPlacementOf`、`GpuScopes`；`modules/tiles/src/placement.cpp` 的四步顺序与 `placementReport`（打 `TILES_PLACEMENT`）；`modules/tiles/src/region_pass.cpp` 在改写前打印它。计划条目 `placement.query`、`gpu_scope.placement_facts`、`gpu_scope.placement_without_a_device` / `_with_a_device` | 无 |
| 拒绝（S4） | `RuntimeDiag::GpuScopeUnsupported = 0x04000071`、`GpuScopeNoDeviceProgram = 0x040001A7`；`modules/tiles/include/tiles/internal/regions.h` 的 `gpuRefusal` 的三条原因串（`no device program for this operator` / `for this shape` / `the module provides none`）。用例 `test/cases/gpu/nofallback/refused_operator.cml`、`refused_shape.cml`、`two_refusals.cml`，计划条目 `nofallback.*` 与 `nofallback.one_diagnostic_lists_every_refused_operator` | 无 |
| 图内区域：臂与循环体各自成段（S5a） | `modules/tiles/include/tiles/internal/regions.h` 的 `RegionNode::control` 与 `analyzeRegions(..., const GpuScopes *)`；`modules/tiles/src/region_outline.cpp` 用 `draft.setInsertRegion` 把段的调用写在成员所在的控制区域，`BRCH`/`JOIN`/`LOOP` 留主机。用例 `nofallback/branch.cml`、`loop.cml`，计划条目 `nofallback.branch_arms_are_segments`、`branch_segment_is_written_in_the_arm`、`branch_scope_returns_what_the_host_returns`、`loop_is_a_segment` | 循环条件读设备张量时每轮一次 `download`（G2-2） |
| 派生图继承放置（S5b） | `modules/tiles/src/placement.cpp` 的 `calleeGraphOf` 把持有函数常量的 `Call` 也算作 callee；`RuntimeDiag::GpuScopeMacroNotExpanded = 0x040001A9`；`modules/tiles/src/region_pass.cpp` 的注释写明展开者是 `std::macro`。用例 `test/cases/gpu/nofallback/grad_expand.cml`、`grad_dynamic.cml`，计划条目 `grad_expand.*` 六条 | 无 |
| 主机效果不是拒绝 | `modules/tiles/include/tiles/internal/regions.h` 的 `gpuRefusal` 注释"Unresolved types and index instances are not refusals … and neither are host effects nor the structural host code"；`modules/tiles/src/region_outline.cpp` 插传输那一圈以 `device` 非空为条件。计划条目 `gpu_scope.host_effect_is_the_boundary`、`gpu_scope.host_effect_synchronises` | 无（体内效果的执行与那一次 `download` 已落地，见 G1-2） |

#### G1-1 标签的落点收口：撤掉模块位置，命名空间块顶上

- **目标**：`#gpu`/`#cpu` 的合法落点是**函数、块与命名空间块**；命名空间块里的每个函数都带上这个事实，函数自己写的是内层、胜出。`Positions` 的 `Module` 位、解析器与 AST/GCT/GIR 里读 `module` 前标签的部分、格式化器的对应处理、`gpu_scope_module.cml` 一并删掉，用例改成命名空间块。
- **边界**：不动"一个键、两个标签"与放置解析的四步顺序；不动 Tiles 的读写点；本条**不在本车道重复施工**。
- **依赖**：无模块声明分支（工作树 `D:\Projects\Camel-wt\h554-no-module-decl` 已从 `origin/develop` 开出，改动未提交，尚未落到分支）——G1-1 是它的交付，本计划只引用它的状态。G1-2 与它无依赖关系。
- **验收读数**：新用例（命名空间块的标签传播，替换今天的 `test/cases/modules/tiles/gpu_scope_module.cml`；计划条目沿用 `gpu_scope.*` 前缀，名字由 G1-1 的车道定）：命令 `camel --devices cpu <用例> tiles::compile std::nvm`，`TILES_PLACEMENT` 里命名空间块里的函数 `body_fact` 是 `gpu`，块里自己写 `#cpu` 的那个函数是 `host`；`camel inspect --tags <用例>` 里 `#gpu`/`#cpu` 的 `on` 不再列模块位置；负例：标签写在 `module` 前报解析/语义诊断（该用例随 G1-1 一起删）。
- **未决点**：问题页第 8 条（命名空间块用现成的 `Nodes` 位，还是新增 `Namespace` 位）。

#### G1-2 作用域体内的主机效果：执行，并插一次显式 `download`

- **目标**：`#gpu` 函数体（或块）里写的 `println`/文件写/`save_npy` 照常执行，位置在主机；遍在值的边界处插一个显式 `tiles:download`，`std::profile` 把那次等待数进 `device_work.syncs`。一个观察点一次：`#gpu` 体里一次 `println` 的读数是 `syncs: 1`。
- **边界**：不改"效果不是拒绝"这一半（已落地）；不改 `--devices cpu` 下"不写任何传输"的行为；不做设备上的效果（没有这种算子）。
- **依赖**：无模块声明分支与它的返工已落地——修在**切分**（`modules/tiles/src/region_outline.cpp`：段的成员语句换成段调用时，把被替换语句的同步输入、顺序输入与它排序过的节点原样交给调用），执行计划不把效果当根（见 §2.4 的落地状态）；机制上依赖 §3.6 的 G2-2 的观察点定义（本条是那一步的第一个用例）。
- **验收读数**：`test/cases/modules/tiles/gpu_scope_host_effect.cml` 在 `tiles::compile` 下体内 `println` 有输出（改之前没有输出、`TILES_RESIDENCY` 记 `"downloads":0`）；有卡时计划条目 `gpu_scope.host_effect_synchronises` 读 `std::profile json=` 的 `device_work.syncs`（一块卡一条流：3，改之前 2）与 `to_host_bytes`（20，改之前 12）。设备无关的那一半：`camel --devices cpu <用例> tiles::compile std::nvm` 的 stdout 里有 `println` 的内容，计划条目 `gpu_scope.host_effect_is_the_boundary` 钉住它。防回退两条（计划条目 `gpu_scope.unused_effect_outside_a_sync_block_does_not_run`、`gpu_scope.unused_effect_stays_dropped_through_the_pass`、`gpu_scope.arm_effect_runs_only_in_the_arm_taken`）：同步块之外没人用的 `println` 加不加 `tiles::compile` 都不执行、都报 `EffectNotExecuted`；分支没选中的臂里的 `println` 不执行。
- **未决点**：问题页第 2 条（观察点与 `syncs` 的计数口径）。

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
- 传输合批：沿用已有的聚合（元组/结构体按叶子 `ACCS` 拆、再装回），一次调用最多一对整块传输。

**落地状态**：驻留结论就是唯一的传输表。`modules/tiles/src/residency.cpp` 的 `planResidency` 读放置事实后逐段回答入口上传、出口下载与段间驻留交接，`modules/tiles/src/region_outline.cpp` 只按这张结论插节点，区域分析原先自己算的那张并存的表（`planRegionTransfers`，按"是不是段成员"作答）已删除。新口径只在块**改变**的地方写传输：段成员按段算设备值，其余值按产生它的节点算，已经落在设备上的值不再被上传（旧表把段外读到的每个值都上传一次，即使它本来就在设备上），段与段之间是驻留交接（同一块，不拷贝）。签名的声明也参与判块：`Device<T>` 形参/返回类型断言这个值本来就在设备上，因此不产生入口上传、出口也不下载；普通声明断言宿主值，调用点与 callee 入口各写一次。`inspectRegions` 的 JSON 把三个答案放进每个 `region`（`uploads`/`downloads`/`resident`；`schema` 仍是 1——报告的号只在**已有字段改了含义或被删掉**时升，只加字段不升号），图级汇总仍写在 `transfers`。

**还没落地的观察点**：`#cpu` 块（入口一次 `download`、出口一次 `upload`）与 `LoopCond` 读设备张量（每轮一次主机观察）。两条都要求宿主块、循环条件成为**独立的段**，而今天的段划分把 `#cpu` 块和循环的条件、体一并放进它们所在的那个段（`graph_regions.cpp` 的 `insideLoop`），边界不存在，因此没有可钉的读数；它们随"把宿主块与条件拆成段"的那一步落地。

**还没落地的调用**：`#gpu` 图之间的调用今天仍各写一次：callee 入口一次上传、出口一次写回。原因不是驻留结论，而是**签名的类型**：`#gpu` 体的算子声明的是普通值，所以它的形参只能声明普通类型，返回类型也只能是普通类型；而驻留 kern 是**按拿到的类型**读值的（`module.cpp` 的 `hostArgument`/`prepareProgram`：设备类型当设备值用，普通类型当宿主值上传）。callee 出口若留在设备上，调用点拿到的就是"类型写普通值、实际是设备值"的值，段算子会把它当宿主张量再上传一次并失败（实测报 `upload currently requires f32 tensor leaves`）；单输入的段连可改的类型位置都没有，调用点形参的类型由被调者签名钉住。等"设备块不再是第二个类型身份"（第 10 节问题 2）落地、签名不必写 `Device<T>` 之后，callee 出口才能留在设备上，这一次上传与一次写回才随之消失。

### 3.3 传输节点与调度

传输是普通 `OPER` 节点（`:tiles/upload`、`:tiles/download`），带数据边；`upload`/`download` 已经用 `declareResources` 声明了资源（`module.cpp` 约 769–806 行），所以顺序由资源边保证，**不需要**新的调度语义。放置是节点/值的事实，调度器不推断放置，只按边决定就绪；**调度器也不决定提交**——它把段算子排到就绪，提交发生在段算子内部（下一个"立即返回"，见 §3.4）。

### 3.4 异步提交

- 提交点是 `tiles:exec` 算子内部：`submitProgram` 返回 outputs + completion；算子把 completion 记在输出块的 owner 上，**立即返回**设备值（pending）。
- 就绪：同一流上顺序即依赖；跨流（G7）用 event。调度器不等待。
- 主机观察才等：`download`、`println`/`save_npy`、主机算子读设备值、图的出口写回、`sync` 块结束。
- 失败：设备故障记进 fault word，在 `tiles:exec` 节点按 source origin 报出（tiles-route §3.6 已定），与今天的 `raiseFault(fault, graph, nodeRef)` 同一路径。
- 公共 API 不变：`run_device` 仍同步（保留它作为显式路径）；"内部异步"只发生在编译段内部与段之间。

```gir
N10 :tiles/exec        [placement=gpu]   → 块 B4，owner{stream 0, event 17}，pending
N11 :tensor/mean (B4)  [placement=gpu]   → 同一段里，不等待
N12 :tiles/download (B4) [host]          → 等 event 17，拷贝到主机块
```

### 3.5 G2 的文件、测试与门禁

文件：`include/camel/core/mm/block.h`（`device_` 的文档与访问器）、`modules/tiles/src/resident_storage.cpp`、`device.cpp`、`module.cpp`（owner 记录与完成令牌）、新 `modules/tiles/src/residency.cpp`、`modules/tiles/src/region_outline.cpp`（段级传输）、`modules/tiles/src/schedule.cpp`（存储计划按放置）。

测试：`device_block_identity.cml`（上传后原值仍可读、放置不改变类型与名字、同一值的两个块各自带 device）、`device_inplace.cml`（`refcount==1` 原地、优化器 0 分配）、`resident_loop.cml`（状态跨轮驻留、每轮传输数）、`resident_cpu_block.cml`（每轮两次传输）、`async_scope.cml`（提交后主机先做事、`std::profile` 的 `syncs` 每段 1 次）、`device_fault_site.cml`（故障在 exec 节点报、origin 正确）。

门禁：`feat/modules/tiles`、`feat/modules/tensor`、`smoke`；GPU 用例用 `skip_unless = ["node", "test/tools/have-cuda-device.mjs", "0"]`，在 88 主机上由 `camel-remote.ps1` 跑。

### 3.6 G2 的施工步骤

**已落地（判据是文件与符号）**

| 项 | 判据 | 未落地的部分 |
|---|---|---|
| 驻留结论是一份可读的答案（S7 的一半） | `modules/tiles/src/residency.cpp` 的 `residencyReport`：每个块的类（`blocks.device`/`blocks.host`）、跨界的值（`crossings`，一个值一次，带 `kind`/`region`/`where`）、调用的 `transfers`、`per_round`；`modules/tiles/src/region_pass.cpp` 在改写前打印 `TILES_RESIDENCY`。用例 `test/cases/modules/tiles/resident_loop.mjs`（计划条目 `resident_loop_declares_its_blocks`）钉住"结论说 device→device 调用 `transfers: 0`、body `crossings: []`、`per_round: 0`，而今天的轮廓仍在 callee 入口留 1 个 `tiles:upload`/`tiles:download`" | 传输由这份结论驱动（今天由区域分析自己算）、`mm::Block::device_` 的写入者、异步跨算子边界 |
| 传输是普通节点 | `modules/tiles/src/module.cpp` 的 `tiles:upload`/`tiles:download` 与 `declareResources`（资源 `tiles:device`），顺序由资源边给；`modules/tiles/src/region_outline.cpp` 的 `transferAggregate`、`modules/tiles/src/tree_transfer.cpp` 的整树合批 | 无 |
| 设备值的内部表示与完成令牌 | `modules/tiles/include/tiles/device.h` 的 `DeviceValue`/`DeviceStorage`/`DeviceCompletion`/`HostLeaf`/`uploadValues`/`downloadValues`/`placeOnDevice`/`retainHostValue`/`executeResident`；`modules/tiles/include/tiles/internal/submission.h` 的 `SubmittedProgram`/`submitProgram`；`modules/tiles/include/tiles/internal/cuda_submission.h` 的 `Submission` | 完成令牌跨算子边界（G2-3） |
| 公共路径仍是同步的 | `modules/tiles/src/module.cpp` 的 `upload`/`download`/`run_preferred_resident`；`modules/tiles/src/cuda.cpp` 在返回前取 `downloadValues` 的结果。用例 `async_execution.cml`、`async_submission.py` | 这是本项**要保留**的行为，不是待修项 |

#### G2-1 设备块就是数组值

- **目标**：设备上的张量就是普通数组值——`TensorType` 与源码里的名字都不变，`mm::Block::device_` 填 DLPack 的 `device_type`/`device_id`（`include/camel/core/array/dlpack.h` 已带这两个字段）；设备块的 owner（流、事件、大小、释放回调）由 Tiles 的设备层持有，块用 `adoptExternal(..., release)` 释放它；`refCount() == 1` ∧ 输出类型与形状同输入 ∧ 算子允许原地 → 写同一块，否则新块。"同一个值两种放置"是同一个值在两个用途上的放置事实，**不是第二个值、不是第二个类型身份**。
- **与 P4 的接口（元素类型、秩、驻留）**：元素类型与秩来自值自己的类型（今天的 `TensorType` 的 extents 与元素类型；P4 把 `Tensor` 并进数组之后是数组值的 dtype 与 shape），块只加 `device_` 这一处事实；驻留**不是类型的一部分**。`Device<T>` 保留为签名上的放置断言（`modules/tiles/include/tiles/device.h` 的 `DeviceType`），普通值传给 `Device<T>` 形参时由遍插 `tiles:upload` 节点，图上看得见；P4 合入前不引入第二套 dtype/秩，也不用 `TensorType::Default()` 兜底。
- **边界**：不改公共 `upload`/`download`/`run_preferred_resident` 的同步语义；不把"同一值多种写法"提升成图上的写法等价（`writings.h` 不进这一条）；不做 DLPack 零拷贝导出（留给支持面）。
- **依赖**：G1（标签与段）；`include/camel/core/mm/block.h` 与数组值的所有者（`src/runtime/**`）要对齐接口；P4 合入后重跑本步的接口读数（元素类型与秩）。
- **验收读数**：新用例 `test/cases/modules/tiles/device_block_identity.cml`——`upload` 之后原值仍可读、类型与名字不变、两个块各自的 `device()`（主机块 0；设备块的字段取值与"0 是主机"这条现有语义怎么对齐，见问题页第 7 条）；`device_inplace.cml`——计数为 1 时原地写，读数取 `std::profile json=` 的 `allocations.calls`（优化器多轮下不随步数增长）与 `inplace_updates.*` 两个计数，二者都要有；`device_tree.cml` 的 DLPack 段加设备字段断言。今天的读数是 `mm::Block::device()` 恒为 0（守卫先红，见上表）。设备无关的那一半现在就能取：`camel --devices cpu <用例> std::gir` 里 `tiles:upload`/`tiles:download` 是普通 `OPER` 节点，`TILES_RESIDENCY` 的 `blocks` 已经把两类值分开数。
- **未决点**：问题页第 1 条（owner 与释放归属）、第 7 条（P4 合入后元素类型与秩的接口）。

#### G2-2 驻留由图推出、主机观察时下载

- **目标**：`modules/tiles/src/residency.cpp` 的结论成为插传输的依据，`region_outline.cpp` 按它在放置变化处插节点。观察点定在**值跨边界的那一次写入**：设备值被主机消费者读（`download` 或主机算子）、主机效果算子（`println`/文件写/`save_npy`）、图的出口写回、`LoopCond` 读设备张量（每轮一次主机观察）。一个值在边界上只插一次传输，不是每个读者一次；`#cpu` 块的入口一次 `download`、出口一次 `upload`（每轮两次）。
- **`download` 插在哪、`syncs` 怎么数**：传输节点插在边界那一侧的值写入处（数据边 + 资源边给顺序，见 §3.3）；每一次主机等待记一次 `synchronise(what)`，读数就是 `std::profile` 的 `device_work.syncs`。G2 的口径是"一个观察点一次"，所以 `#gpu` 体里一次 `println` 的读数是 1（今天没有传输、也不打印，见 G1-2）。
- **边界**：`--devices cpu` 下一次传输都不写（`region_outline.cpp` 插传输那一圈以 `device` 非空为条件），所以观察点的读数只在有卡时取；不改无标签程序的轮廓结果；循环条件那一半与 G4 分界——G2 每轮主机观察一次，G4 才改成设备上判条件。
- **依赖**：G2-1；G1-2（体内效果是一次观察点）；P4（值的元素类型与秩决定传输的叶子形状）。
- **验收读数**：`test/cases/modules/tiles/resident_loop.mjs` 的既有关系翻转——`TILES_RESIDENCY` 说 device→device 调用 `transfers: 0`、body `crossings: []`、`per_round: 0`，而今天同一份 jgir 的 callee 入口还有 1 个 `tiles:upload`/`tiles:download` 节点；G2-2 之后这个节点数必须是 0（`resident_loop_declares_its_blocks` 的注释里那句"the number the insertion changes"随之改成 0）。新用例 `resident_cpu_block.cml`（`#cpu` 块每轮恰好两次传输）与 `resident_loop.cml`（状态跨轮、每轮 0 次）逐字钉数字；`std::profile json=` 的 `device_work.transfers`/`to_device_bytes`/`to_host_bytes` 与 `regions<f>()` 的分析（`modules/tiles/src/graph_regions.cpp::inspectRegions` 的 JSON 键 `uploads`/`downloads`/`resident`）互相对照，两个读数都要有。
- **未决点**：问题页第 2 条（观察点与 `syncs` 的计数口径）、第 6 条（两张传输表合一还是并存）。

#### G2-3 异步提交

- **目标**：提交发生在段的 exec 算子内部——`submitProgram` 的 outputs + completion 记在输出块的 owner 上，算子**立即返回** pending 设备值；主机观察时才等；故障记进 fault word，在 exec 节点按 source origin 报出（与今天的 `raiseFault(fault, graph, nodeRef)` 同一路径）。
- **与"按依赖求值"的关系**：**不引入新的顺序语义**。调度器仍只按数据边与资源边决定"就绪"，提交顺序就是依赖顺序；异步不改变任何可观察的值，只改变"谁在什么时候等"，而等待点就是 G2-2 定下的观察点。
- **边界**：公共 `upload`/`download`/`run_preferred_resident` 保持同步（保留它们作为显式路径）；不加语言原语、不加新算子；不做跨流的多设备（G7）、不做 CUDA Graph 捕获（留给支持面那一项）。
- **依赖**：G2-1、G2-2；`DeviceCompletion`/`Submission`/`SubmittedProgram` 已存在，本步是把完成令牌从后端内部带出到块的 owner。
- **验收读数**：新用例 `test/cases/modules/tiles/async_scope.cml`（提交后主机先做自己的事，观察点只有一次等待：`device_work.syncs` 每段 1）；既有 `async_execution.cml`、`test/cases/modules/tiles/async_submission.py` 与 `async_submission_*.cpp`、计划条目 `gpu_scope.host_effect_synchronises` 保持绿；`device_fault_site.cml`（故障在 exec 节点报、origin 正确）。设备无关的那一半今天就能取：`camel --devices cpu <用例> tiles::compile std::nvm` 的数值读数与无 `tiles::compile` 时逐位相同。
- **未决点**：问题页第 3 条（pending 设备值能否穿过不观察它的主机算子）。

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

### 4.6 G3 的施工步骤

**已落地（判据是文件与符号）**

| 项 | 判据 | 未落地的部分 |
|---|---|---|
| 一个作用域一份编译产物的进程内那一半 | `modules/tiles/src/compiled_region.cpp` 的 `compileTypes`/`prepareProgram`/`hasCompiledRegion` 与目标事实 `tiles.compiled_region`（`rt::fact::registerKind` 的 `FactClass::Target`、`Scope::Graph`，目标哈希 `0x74696c6573000004`；stamp 由 `FactStore` 给图修订 + 声明纪元） | 单元是（作用域图, 放置）的传递闭包、`tiles.compiled_scope`（G3-1） |
| 进程内的代码生成缓存 | `modules/tiles/src/codegen_cache.cpp` 的 `planKey`（`key.number(8); // Process-local key schema; not a persistent semantic fingerprint.`）与 `modules/tiles/include/tiles/internal/codegen_cache.h` 的 `GenerationCacheStats{hits, misses, entries, retainedBytes}` | 形状签名、落盘与失效矩阵（G3-2） |
| 显存池已有实现与读数 | `modules/tiles/include/tiles/internal/cuda_backend.h` 的 `MemoryPool`/`PoolPolicy`/`MemoryPolicy`/`PoolStats`/`AllocationLease`；`modules/tiles/src/cuda_pool.cpp`（`capacityFor` 的 size class、OOM 时回收本类空闲块重试一次）；native 检查 `test/cases/modules/tiles/memory_pool_check.cpp`、`memory_pool_failures.cpp`，以及 `async_submission.py` 的 `tiles pool: …` 断言 | 上界自适应（`MemoryPolicy::device{64 MiB, 32}` 今天是固定值，G3-3） |

#### G3-1 整段编译：单元 =（作用域图, 放置）

- **目标**：一个 `#gpu` 函数/块，加上它传递闭包里**同放置**的被调图，一起编成**一个** `Program`，段是它的入口；作用域图上记目标事实 `tiles.compiled_scope`（`tiles.compiled_region` 的扩展）。同一作用域两次调用只编译一次；同一个函数被两处调用而放置不同 → 两份单元。
- **边界**：不改 `tiles::compile` 的入口写法（仍是 `RuntimeGraphRewritePass`）；本条不做落盘；支持面（bf16/Tensor Core、融合、优化器多张量融合）是另一项工作。
- **依赖**：G1（段）、G2-3（提交在段算子内部）；P4 决定单元入口的值类型与形状从哪里读（问题页第 7 条）。
- **验收读数**：`inspect<f>(x)` 的计划 JSON（段表 `stages[].members[]`，既有计划条目 `gpu_scope.inspect_segments`）里一个作用域一份产物、段是入口；`std::profile json=` 的 `device_work.compiles` 在同一作用域被调两次之后仍是 1；新用例 `test/cases/modules/tiles/placement_callee.cml`（无标签 helper 从 `#gpu` 与主机两处调用 → 两份单元，段表里两份 helper 图）。
- **未决点**：问题页第 6 条（`tiles.compiled_scope` 与 `tiles.compiled_region` 的键怎么接）。

#### G3-2 按形状签名缓存（内存与磁盘）

- **目标**：编译单元的持久指纹由五段拼成——① 单元的规范化图编码（`src/compile/gir/graph_encode.cpp`，含 body、区域与节点 attrs）；② 用到的每个算子的**声明键 + 语义版本**；③ 目标身份（arch、NVRTC 版本、代码生成 schema 号、调度策略参数）；④ 放置；⑤ 编译器的构建身份（`scripts/build-info.js` 写出的构建指纹，一次构建内不变）。**不用源哈希**。**形状签名**（每个入口张量 extents 的规范形式 + 秩）是**特化表**的键、不是单元的键（§4.2）：符号维（`constas n`）编一次；`?` 按形状签名特化，入有界 LRU；形状签名变化只增一条特化，单元不重编。
- **失效条件逐条**：图修订变（编辑）、声明纪元变（模块重编译）、目标身份变、放置变、编译器构建身份变 → miss 并重编；形状签名变 → 不失效，只增一条特化。加载时**重新校验**（指纹 + 记录的输出类型与形状 vs 当前推断），任何一项不一致 → miss。
- **磁盘位置与清理**：沿用 §4.3 的约定——`CAMEL_HOME` 下的版本目录，带上限与 LRU，清理由该目录的显式入口负责；磁盘缓存用例把目录指到每次运行的临时目录，共享主机上的残留不得把"第二次命中"变成假绿。不写墙钟阈值作为断言。
- **边界**：不改 `std::profile` 的既有字段（池与缓存的计数是新增字段）；不做跨用户共享、不做网络缓存；不改 `MemoryPolicy::uncached()`。
- **依赖**：G3-1、G2-2（形状决定传输与存储计划）、P4（extents 的规范形式）。
- **验收读数**：§4.5 已排的用例逐条：`scope_one_compilation.cml`（`inspect<f>(x)` 一份产物、`device_work.compiles == 1`）、`scope_symbolic_shapes.cml`（`constas n` 两个不同的 `n` → 一次编译、两次 launch）、`scope_unknown_shapes.cml`（`?` 两个形状 → 两条特化、有界）、`scope_cache_disk.cml`（两次进程运行，第二次 `device_work.compiles == 0`）、`scope_cache_invalidate.cml`（改 helper 源、改声明、换 arch、伪造旧构建指纹各 miss 一次）。读数形态：`std::profile json=` 的 `device_work.compiles`/`compile_ns` 与 `GenerationCacheStats` 两个计数都要读，二者不一致即失败。守卫先红：今天没有形状签名这个键，也没有落盘。
- **未决点**：问题页第 4 条（默认落盘与否、目录、清理与版本目录）。

#### G3-3 自适应显存池

- **目标**：池的上界不再固定（今天 `MemoryPolicy::device{64 MiB, 32}`、`host{16 MiB, 32}`）：按 size class 分级（`MemoryPool::capacityFor` 已有）、按工作集高水位自适应、空闲显存不足时先 trim 再分配，并保留"OOM 时回收本类空闲块重试一次"。驻留决定"哪块还活着"，池决定"活着的块从哪来"，两者正交。
- **测什么**：`PoolStats` 的 `checkouts`/`reuses`/`allocations`/`cached`/`freed`/`trims`/`oomRetries`/`abandoned`/`idleBytes`/`disabled`；`std::profile` 的池高水位（新字段，与 `device_work` 并列）；稳态断言"每步 `allocations` 不增长"与"峰值不随步数增长"——步数用同一程序多轮，读数只用计数，不用时间。
- **上界怎么定**（选一条写进实现，另一条留在问题页第 5 条）：(a) 设备总显存的比例 + 分配前的空闲显存探测，比例是硬上限；(b) 工作集高水位——池只保留测出的稳态高水位，超出即释放，冷启动与稳态分开报。两条都只用池与 profile 的计数，不读墙钟时间。
- **边界**：不改 `MemoryPolicy::uncached()` 与 `PoolPolicy` 的 native 语义；不改公共 API；不做多设备多池（G7）。
- **依赖**：G2-1（owner 从池里取、释放回池）；P4（工作集由数组值的生存期决定）。
- **验收读数**：`memory_pool_check.cpp`、`memory_pool_failures.cpp` 与 `async_submission.py` 的 pool 断言保持绿（逐字断言形如 `tiles pool: rounding, best fit, caps and trim counters passed`、`tiles pool: OOM trim-and-retry-once and non-OOM rejection passed`）；§4.5 已排的 `memory_pool.cml`：稳态下每步的直接分配（`PoolStats.allocations` 的增量）为 0、`idleBytes` 不随轮数增长、峰值不涨。设备无关的那一半（native 检查 + `PoolStats`）今天就能取。
- **未决点**：问题页第 5 条（上界依据）。

#### G3-4 删除清单与依赖顺序

**`@gpu`、`run`、`run_device`：已删除，不重做。** 判据：`git grep -n "@gpu\|run_device\|tiles:run\b" -- src modules stdlib test docs/language docs/reference` 无匹配；只有历史文档与 `run_preferred_resident` 这个名字还在。删除后谁接管：放置只写 `#gpu`，设备由 `--devices` 在执行时选（`include/camel/execute/devices.h`）。

**Tiles 私有的驻留存储：今天在哪（逐字清单，删除步的对象）**

1. `modules/tiles/src/resident_storage.cpp`——`HostStorage`、`retainHostValue`、`downloadValue`（"Backend-independent resident reads and immutable native host snapshots"）。
2. `modules/tiles/include/tiles/device.h`——`DeviceStorage`、`DeviceValue`、`HostLeaf`、`uploadValue`/`uploadValues`/`downloadValue`/`downloadValues`、`placeOnDevice`、`retainHostValue`、`executeResident`（文件头："Typed device values own native storage, never pointers into Camel's moving heap"）。
3. `modules/tiles/include/tiles/internal/submission.h`——`SubmittedProgram`、`submitProgram`（"Backend-native pending results. These never cross a Camel operator boundary."）。
4. `modules/tiles/src/cuda.cpp`、`cuda_submission.cpp`、`cuda_resources.cpp`——后端自己的分配、固定内存暂存与 `CompletionEvent`。
5. `modules/tiles/src/module.cpp`——`tiles:run_preferred_resident`（保留为内部入口）、`tiles:upload`、`tiles:download`。
6. 钉住私有存储读数的用例：`test/cases/modules/tiles/resident.cml`、`resident_compute.cml`、`resident_outputs.cml`、`resident_scalars.cml`、`resident_graph.py`，以及 `resident_loop.mjs` 后半段"twice is one resident call"那一句。

**删掉后谁接管**：数组值的设备块（`include/camel/core/mm/block.h` 的 `device_` + 设备层的 owner 记录，G2-1）；传输是普通的值写入节点（`tiles:upload`/`tiles:download`，G2-2）；提交在段的 exec 节点内部（G2-3）。**保留**的是算子本身：`upload`/`download`/`run_preferred_resident` 仍是显式路径与可回退入口（[GPU 设计稿](README.cn.md) §6 第 3 条），删的是它们背后的**私有驻留存储**——设备值不再由 Tiles 自己的存储持有，而是普通的数组块。范围之外的回退行为（`resident_fallback.cml`、`resident_fallback_fault.cml`）不变。

**依赖顺序（不能反）**：G2-1 → G2-2 → G2-3 → 本条。删除步不得先做：先删等于把设备上的值弄丢。

**验收读数**：删除步之后 `git grep -n "resident_storage\|retainHostValue\|executeResident" -- modules/tiles` 只剩显式路径的调用点（清单第 1、2 项里被删的那几个符号不再有调用者）；`resident*.cml` 与 `resident_loop.mjs` 的读数换成同一批数字的新来源——`mm::Block::device()` 与 `TILES_RESIDENCY` 的 `blocks`/`crossings`；窄门禁 `feat/modules/tiles` + `smoke` 全绿。

**未决点**：问题页第 1 条（owner 与释放归属，决定第 1、2 项删到什么程度）。

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

`std::profile json=…` 已经记录每个设备内核、每次传输、每次同步与每次 NVRTC 编译（`include/camel/profile/profile.h`），`hints` 列出留在主机上的逐元素循环及原因。验收断言：

- 训练步的循环里 `kernels > 0`，且 `toDevice`/`toHost` 每步是**常数**（不随层数增长）；
- 每步 `syncs` 是常数（段数 + 出口的 1 次），不随算子数增长；
- 算子表里**没有**张量算子（被编译进段的算子不经过 `profiledOperator`）；`hints` 为空；
- `tiles::inspect` 的段表：每个 `#gpu` 作用域里的主机段数 = 0（除非显式 `#cpu`），传输节点只在边界；
- 反向与优化器：`with_grad`/`sgd` 的段在同一份产物里（`inspect` 的 entries 包含它们），优化器每步 `allocations == 0`。

### 6.5 支持面的缺口清单

| 程序用到的算子 | 今天 | 谁补 |
|---|---|---|
| `matmul`、`add`、`multiply`、`sum_axis`/`mean`、`relu`、`exp`、`log`、`maximum` | 在表里 | — |
| `permute`/`transpose`/`reshape`、`softmax`、`gelu`、`layer_norm`、`mean`、`cross_entropy` | 不在表里 | 支持面（视图与逐元素/归约组合） |
| `cast`（f32↔f32 以外）、`sgd`/`adam` 的逐元素更新 | 部分 | 支持面（元素类型与原地契约） |
| `with_grad`/`grad` | 构图机制，不进设备表；`std::macro` 在编译期把它展开成派生图 | G1（§2.6） |

本计划交付时，"每个算子都在 GPU 上"的闸门按支持面内的算子全绿；表外算子逐条列在上表里，支持面补齐后同一脚本零缺口。这就是两段的分界：**前者让"放上去"成为可测的、正确的事，后者让"放上去的算子足够多"**。

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

（`--torch-device`/`--camel-devices` 是本计划要加的两个参数：`compare.py` 的 `camel_arg_command` 把 `--devices 0` 放在目标文件之前；`train.py` 的 `CAMEL_CONFIGS` 加一条 GPU 配置，`run_camel_full` 把 `--devices` 传下去，`model_torch` 按 `--device` 把权重与输入搬到 CUDA。GPU 配置的遍序是 `std::macro tiles::compile std::nvm`：`std::macro` 必须在 `tiles::compile` **之前**——派生图只有在编译期展开出来，才会被当成同一个单元的图（§2.6）；顺序反了 `tiles::compile` 会报缺少前置遍并点名 `std::macro`。）

**把一条红归给环境之前，先量出它是从哪一步开始是 0 的——归因也要读数。** 一条设备读数不对时，三段各打一个点：`load`（宿主→设备的拷贝做完，设备缓冲里是什么）、`launch`（核跑完是什么）、`readback`（交回宿主的是什么）；哪一步先变成 0，就是哪一步的错，`--devices cpu` 跑同一程序是宿主那一半的对照。三段都读对了而值仍不对，才轮得到"机器/驱动"的说法——"环境"这两个字要有读数才配写。`gpu_preference.named_device` 就是这样定出来的：`load` 阶段设备缓冲已经是 8 个零（`hostStorage` 对 f64 标量没有规则，读的是空的 `data`），不是核没跑，也不是读回；修在 `hostStorage`，同日 870e10935。

**A/B 要退到引入这条规则之前的那一笔——退得不够远，两侧都一样红。** 同一条红我先做过一次 A/B，退的是类型门 `a53b87e17`，而 `c397d72c2` 引入的 `hostStorage` 两侧都在，于是两侧都红，就被当成了环境；真正把它定住的是上面那三段读数。基线要落在"这条规则还不存在"的那一笔，否则"两侧都红"只说明这条规则被两次带上了。

**两笔都在改同一句话的时候，先定"谁是机器读的、谁是人读的"——id 给机器，句子给人。** 窄元素这条边界上两笔撞在一起：一笔把理由改成 `no narrow-float kernel on this back end`（一句话，直接当 id 用），一笔把它改成 `device_element_kernel_missing`（一个 id，句子留给 `gpuRefusal` 在真用设备时拼）。后者是对的：计划与断言读的是 id，人读的是句子，把句子当 id 用等于把一句话钉进读者的解析里，改一个字就要动所有读者。所以合并时 id 取 `device_element_kernel_missing`，`no narrow-float kernel on this back end` 不再作为字符串出现在任何地方，句子归 `gpuRefusal`（`the device has no kernel for a bf16 element`），另一句 develop 上的"这个后端只带 f32 张量与 i64 位置"各自保留、不重复。

---

## 7. 提交顺序与可独立落地

| 提交 | 内容 | 依赖 | 独立落地 |
|---|---|---|---|
| 1 | S1 启动校验（0x70 在启动时） | 无 | 是 |
| 2 | S2 放置键与两个标签（无消费者） | 无 | 是（若上一版已合则改它声明的键，未合则先落自己的声明） |
| 3 | S3 放置解析与作用域边界 | 2 | 是 |
| 4 | S4 拒绝与 0x71（接管上一版的判定） | 3 | 是 |
| 5 | S5 图内区域（段）与派生图展开（`std::macro`） | 3、4 | 是（两个提交：5a 段，5b 展开） |
| 6 | S6 设备块与值模型 | 无（可与 1–5 并行） | 是 |
| 7 | S7 驻留与传输 | 6 | 是 |
| 8 | S8 异步提交与就绪 | 6 | 是 |
| 9 | S9 整段编译 | 5、7、8 | 是 |
| 10 | S10 形状签名与持久缓存 | 9 | 是 |

上一版在途：它删的是旧 API、加的是 `tiles.gpu` 与 0x71。若它先合入，本计划的 2/4 是在它上面改键与扩展；若它后合入，先落 2/4 会与它冲突，所以**顺序上它先合**，否则本计划的 2/4 等它。

每个提交都能单独回退：S1 只加校验；S2 只加事实；S3/S4 只在 Tiles 内部生效；S5 之后 `#gpu` 才有语义；S6–S10 各自带开关（`#gpu` 之外的图不受影响）。

---

## 8. 破坏面

- **语言**：新增 `#gpu`/`#cpu` 的语义（此前只有上一版的 `#gpu` 单标签且立即拒绝）；删除已在上一版里做的 `@gpu`/`run`/`run_device` 不动。`Device<T>` 的写法见问题 2。
- **标准库**：`stdlib/tiles.cml` 的导出面（上一版已改）不变；若问题 2 决定保留 `Device<T>`，签名保持不变。
- **运行图**：region 0 可以带 attrs（`regionCount` 对只带标签、无控制流的图从 0 变 1）；`ControlRegions`/dump/encode/validator 都容忍（§2.2 已逐个看过调用点）。
- **测试**：`gpu_regions.cml`、`gpu_fallback.cml`、`async_execution.cml` 等上一版已在重写，本计划在其上再加段/放置断言；`std::gir` 的 golden 会因 `attrs=` 与 body 区域行而变，属于预期。
- **内部接口**：`RegionAnalysis`/`Region` 改成"段"、`outlineRegions` 的转移位置、`tiles.compiled_region` → `tiles.compiled_scope`、`compileTypes` 的调用点。
- **明确不破坏**：无标签程序的行为；`--devices cpu` 的现有机型；`upload`/`download`/`run_device` 的同步语义；`std::profile` 的既有字段。

---

## 9. 风险

1. **形状特化爆炸**：`?` 的程序每个形状一份特化。缓解：有界队列 + LRU + 磁盘缓存；验收里断言"符号维只编一次"。
2. **首次调用的 NVRTC 编译时间**：Draft §4 已定"自适应池"；G3 的验收把 `deviceCompiles` 记录在 profile 里，首次与稳态分开报，不让冷启动混进每步时间。
3. **磁盘缓存的错误命中**：指纹漏掉某个输入（新加的节点种类、新加的属性）就会命中旧产物。缓解：schema 号进指纹；加载时用记录的类型/形状重新校验；失效矩阵测试。
4. **与上一版的冲突**：见第 7 节；顺序上先合它。
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
