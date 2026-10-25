# H49 G1–G3 的未决点：八条要定的接口与口径

G1–G3 的施工步骤写在 [GPU 计划](../../design/gpu/plan.cn.md) 的 §2.8、§3.6、§4.6。下面每一条都是那些步骤里**今天就挡在施工前面**的接口或口径问题：写清问题、逐字证据、两个选项与各自代价。两个选项都能立刻开工，选哪条只改变改动面与读数形态，不改变 G1–G3 的目标。

---

## 1. 设备块的 owner 与释放归属

**问题**：设备内存最后由谁释放——数组块自己（`adoptExternal(..., release)` 的释放回调），还是 Tiles 设备层的 owner 记录？

**证据**
- `include/camel/core/mm/block.h` 的 `adoptExternal(void *data, size_t bytes, BlockRelease release, int device)` 已经有释放回调与 `device` 参数；`device()` 的注释写"Device blocks get their field filled by the Device lowering later"——今天没有任何写入者。
- `modules/tiles/include/tiles/device.h` 的 `DeviceValue{shape, dtype, address, owner, completion}`，`owner` 是 `shared_ptr<DeviceStorage>`，文件头注释是"Typed device values own native storage, never pointers into Camel's moving heap"。
- `modules/tiles/include/tiles/internal/cuda_backend.h` 的 `AllocationLease` 注释"A live lease keeps its backend alive; an idle pool slot never does"，`MemoryPool` 的空闲槽只放裸句柄与容量。

**选项 A（块是唯一所有者）**：设备块的释放回调把块还给池，Tiles 的 owner 记录只留流与事件。代价：GC 终结器要能在设备上下文里释放，`releaseFromFinalizer` 的延迟队列得带上设备上下文。

**选项 B（Tiles 的 owner 记录是所有者）**：块只带 `device_` 与一个丢弃引用的回调，真正的释放由设备层在它自己的上下文里做。代价：块的生存期与设备上下文绑在一起，跨线程释放要排队。

**影响**：G2-1 的第一步；G3-3 的池读数（`checkouts`/`reuses`/`abandoned`）解释方式随选项不同。

---

## 2. 观察点与 `syncs` 的计数口径

**问题**：一次主机观察算几次 `syncs`——按值（一次 `download` 一次），还是按等待（同一流上合并成一次）？

**证据**
- `src/profile/profile.cpp` 的 `synchronise(std::string_view what)` 与 JSON 里的 `device_work.syncs`；`modules/tiles/src/cuda.cpp` 在每次传输后调用 `profile_hooks::synchronise("resident upload")` / `("resident download")`。
- 今天的读数：`test/cases/modules/tiles/gpu_scope_host_effect.cml` 在 `tiles::compile` 下 `TILES_RESIDENCY` 记 `"downloads":0`，`println` 没有输出（[既有问题页](gpu-scope-in-body-host-effect.md)）。
- 复核对返工二的要求逐字为"`#gpu` 函数体里的 `println` 打印出来，`syncs` 计数为 1"。

**选项 A（一个观察点一次等待）**：一值一次，读数最好核对；代价：同流上相邻的两次 `download` 记两次，即使硬件只等了一次。

**选项 B（一次等待一次）**：更贴近硬件；代价：读数随流数与调度变化，用例必须同时钉住流数。

**影响**：G1-2 与 G2-2 的验收数字，以及 `std::profile` 文本报告里既有的 `syncs per step` 一行。

---

## 3. pending 设备值能不能穿过不观察它的主机算子

**问题**：G2-3 之后，一个只做打包的主机算子（元组/结构体构造、`let` 绑定、同放置的 `call`）能否把尚未完成的设备值原样传下去？

**证据**
- `modules/tiles/include/tiles/internal/submission.h` 的文件头逐字："Backend-native pending results. These never cross a Camel operator boundary."；同文件 `SubmittedProgram{outputs, completion}` 的注释"Required even when outputs is empty: faults are an execution obligation."
- `modules/tiles/include/tiles/device.h` 的 `DeviceCompletion{wait, ready}` 已经能把"尚未完成"表达出来。
- 设计原则 3.1（按依赖求值）与索引化计算 §6.3（完成事件、依赖保证可见性、没有隐式同步）。

**选项 A（只有观察才等）**：打包节点不等待，异步面最大；代价：要逐条列举"哪些主机算子不观察设备值"，列错一处就是提前等待或读到未完成的数据。

**选项 B（任何主机算子读到设备值就等）**：规则只有一条，与今天一致；代价：异步的收益只发生在段与段之间，主机侧中间节点仍同步。

**影响**：G2-3 的目标与 `syncs` 读数；G7 的跨流事件也建立在这条口径上。

---

## 4. 磁盘缓存的默认位置、清理与版本目录

**问题**：编译产物默认落不落盘、落在哪、谁清、版本怎么带？

**证据**
- `modules/tiles/src/codegen_cache.cpp` 的 `planKey` 逐字：`key.number(8); // Process-local key schema; not a persistent semantic fingerprint.`；读数只有进程内的 `GenerationCacheStats{hits, misses, entries, retainedBytes}`——今天没有落盘，全仓也没有缓存目录参数（`CAMEL_*` 里没有 cache 一类）。
- GPU 计划的 §4.3 写"磁盘位置与布局沿用构建产物的约定（`CAMEL_HOME`/缓存目录），带版本目录"。
- [plan-modules.cn.md](../../design/gpu/plan-modules.cn.md) §4 记下共享主机上的残留会让"第二次命中"变成假绿，那里的用例因此把目录写在每次运行的临时目录。

**选项 A（默认落盘）**：落在 `CAMEL_HOME` 下的版本目录，配 LRU 上限与显式清理；第二次运行 `compiles == 0` 默认成立。代价：共享主机与多用户下的权限、残留要处理，测试必须显式指向临时目录。

**选项 B（默认不落盘）**：持久缓存由显式参数开启；默认行为可预测，测试不用特判。代价：外部报告里"启动 73 s"只有显式开启才消失。

**影响**：G3-2 的键里要不要带"缓存目录身份"，以及"第二次运行 0 次编译"这条验收的默认形态。

---

## 5. 自适应显存池的上界依据

**问题**：池的上界按什么定？

**证据**
- `modules/tiles/include/tiles/internal/cuda_backend.h` 的 `MemoryPolicy{device{64 MiB, 32}, host{16 MiB, 32}}`（固定）与 `PoolStats{checkouts, reuses, allocations, cached, freed, trims, oomRetries, abandoned, disabled}`。
- `modules/tiles/src/cuda_pool.cpp` 已有"OOM 时回收本类空闲块重试一次"；native 检查 `test/cases/modules/tiles/memory_pool_check.cpp` 与 `memory_pool_failures.cpp`、`async_submission.py` 的 `tiles pool: …` 断言都在读这些计数。

**选项 A（比例 + 空闲显存探测）**：上界写成设备总显存的一个比例，分配前问一次空闲显存。代价：共享卡上"空闲显存"是他人负载的函数，读数会动，只能当上限、不能当断言。

**选项 B（工作集高水位）**：池只保留测出的稳态高水位，超出即释放，冷启动与稳态分开报。代价：高水位需要一个测量窗口，第一次运行的读数不能代表稳态。

**影响**：G3-3 的两条验收（"每步 0 次直接分配"与"峰值不随步数增长"）在两条选项下都能取，只有"上界本身"这条读数在选项 A 下能直接读出。

---

## 6. 驻留结论与区域分析的两张传输表

**问题**：G2-2 之后，插传输由 `residency.cpp` 的结论说了算，还是仍由 `regions.cpp::planRegionTransfers` 说了算？

**证据**
- `modules/tiles/src/residency.cpp` 的注释逐字："the pass that inserts the nodes takes its answer from here."
- `modules/tiles/include/tiles/internal/regions.h` 的 `RegionTransfers{uploads, downloads, resident}`、`RegionNode::control`，以及 `planRegionTransfers` 的注释"A region never spans two of them"。
- `test/cases/modules/tiles/resident_loop.mjs` 今天钉住的正是两者之差：结论说 device→device 调用 `transfers: 0`，同一份 jgir 的 callee 入口仍有 1 个 `tiles:upload`/`tiles:download`。

**选项 A（有标签走结论，无标签保留区域分析）**：改动面最小，`regions.*` 的既有期望不动；代价：并存期要写清哪条路径读哪张表，直到并发收尾。

**选项 B（合成一张）**：由一张表插节点；代价：改动面更大，`tiles_regions.plan.toml` 与 `regions.*` 的既有读数要一起对。

**影响**：G2-2 的落地点，以及 `resident_loop.mjs` 那句"the number the insertion changes"改成 0 的方式。

---

## 7. P4 合入后设备块的元素类型与秩

**问题**：设备块的元素类型与秩从哪读——值的类型（今天的 `Tensor<f32, [n, m]>`），还是块自己的 dtype/秩字段？`device_` 这个整数字段又怎么同时表达"0 是主机"与"CUDA 序号 0"？

**证据**
- `include/camel/core/mm/block.h` 只有 `device_`、`bytes()`、`capacity()`，没有 dtype 也没有秩；`device()` 的注释逐字是"Device the data lives on (0 is the host)"，而 `adoptExternal` 的 `device` 参数与 DLPack 的 `device_id` 都在同一个位置上用 0 表示第一块卡。
- 同一份设计里的 `include/camel/core/array/dlpack.h` 有 `device_type` 与 `device_id` 两个字段；`modules/tiles/include/tiles/dtype.h` 的 `DType{F32, F64}` 与 `modules/tiles/include/tiles/program.h` 的 `Shape` 是 Tiles 侧的类型事实。
- 任务书上写着"H49 中不依赖张量并进数组的部分现在就做，不等 P4——S2…S5b…；S6–S10 等 P4 合入"。

**选项 A（值的类型是唯一来源）**：块不记 dtype/秩，传输节点从值的类型拿；代价：P4 期间两套类型并存时要临时映射。

**选项 B（块上补 dtype/秩）**：传输与 DLPack 导出不必回看图；代价：同一个事实有两处，类型与块可能分叉。

**影响**：G2-1 的接口、G3-1 的单元入口，以及 H50 的 DLPack 交换。

---

## 8. 命名空间块上的标签：`Nodes` 位还是新增 `Namespace` 位

**问题**：`Positions` 今天有 `Func|Type|Field|Nodes|Param|Module`，没有命名空间块的位置；标签贴 `namespace { … }` 靠哪个位置？

**证据**
- `include/camel/core/tags.h` 的 `enum Position` 里 `Nodes = 1u << 3, // on a function or a block: the nodes written in it`。
- `src/core/tags.cpp` 里 `#gpu`/`#cpu` 的 `on = static_cast<Positions>(Func | Nodes | Module)`。
- 复核对返工一派的活是"改成确认标签能贴在 `namespace` 块上，块里的函数都带上、内层的胜出"。

**选项 A（按 `Nodes` 位处理）**：不加新位，前端把块里的声明当作写在块里的东西；代价：`Nodes` 今天的措辞是"写在函数或块里的**节点**"，要确认声明也在内。

**选项 B（新增 `Namespace` 位）**：语义直白，`#gpu` 的 `on` 里用 `Namespace` 换掉 `Module`；代价：`Positions` 是标签位置的公开枚举，多一位要过一遍所有消费者。

**影响**：G1-1（返工一的实现面）与 `camel inspect --tags` 的输出形态。