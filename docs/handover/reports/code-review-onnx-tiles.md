# ONNX 导出与 Tiles/GPU 代码审查

范围：`modules/onnx/`（`lowering.cpp`、`lowering_grad.cpp`、`emitter.cpp`、`exporter.cpp`、`capability.cpp`、`module.cpp`）与 `modules/tiles/`（`compiler.cpp`、`graph_regions.cpp`、`placement.cpp`、`module.cpp`、`cuda*.cpp`、`schedule.cpp`），对照 `docs/design/gpu/README.cn.md`、`test/cases/modules/onnx`、`test/cases/modules/tiles`。只读代码，没有构建、没有运行；标"需确认"的是由代码推断、未实测。

总体印象：Tiles 在数值契约上很扎实（标量除零按两种零号都报故障、规范 NaN、-0 序、f64 累加，`scalar_zero*`、`extrema_bits`、`max_special` 等差分测试到位）；ONNX 导出对切片、argmax 的 NaN/-0 做了认真的复刻，但同一份契约在其余算子上被丢掉了，而且 ORT 对照测试只覆盖了其中一小部分。问题集中在"导出的模型悄悄算出与运行时不同的结果"。

按严重度排序。

## 高：导出静默地与运行时不同

### O1 标量除法与类型转换的故障没有复刻

- 运行时：`src/builtin/operators/ops.cpp:681-700` 的 `/` 对 Int32/Int64/Float32/Float64 的除数为 0（浮点含 -0）都报 `DivisionByZero`；整数 `min / -1` 有明确的商（`utils::quotient`）；`src/core/numeric_convert.h` 规定浮点转整数遇 NaN、无穷、越界为 `ConversionOutOfRange`，int 到 i32 越界同样是故障。
- 导出：`modules/onnx/lowering.cpp:1263-1290` 把 `:op/div_{i,l,f,d}` 直接降为 ONNX `Div`，`:op/{i,l,f,d}to{i,l,f,d}` 直接降为 `Cast`（`lowerScalarCast`，约 1000 行处）；`tensor:cast`（`lowerCast`，约 280 行处）同。
- 失败场景：依赖模型输入的标量 `a / b`，`b = 0.0` 时运行时故障，导出模型给出 inf/NaN（整数则是 ORT 的未定义行为或报错）；`int(x)` 的 x 为 NaN 或 1e30 时运行时故障，导出模型静默饱和或回绕。Tiles 对同一件事有测试和故障（`test/cases/modules/tiles/scalar_zero*.cml`），ONNX 没有。
- 修法：除数不是已证非零的常量时，按切片的先例在图里放"守卫"（对除数做 `Equal(0)`，用 `Where` 接一个必然让 ORT 失败的 Gather/Reshape），或明确拒绝导出（`ExportError`：该除法的除数依赖模型输入，运行时会报除零故障，导出无法等价）。转换同理：窄化与浮点到整数要么加范围守卫，要么拒绝。与其静默，不如拒绝，再逐个补守卫。

### O2 负下标：运行时是故障，导出里是"从末尾数"

- 运行时：下标一律不接受负数（`docs/design-principles.cn.md` 5.4 末尾："不支持负数下标（负数是故障）"；`ops.cpp:804-` 的 `raw < 0` 即故障）。
- 导出：`lowerArrayIndex`（`lowering.cpp:853-898`）对形状数组的常量下标做 `i < 0 ? i + n : i`，并对张量元素读发 `Gather`；ONNX 的 `Gather` 对 -n..n-1 的下标按从末尾数处理。`lowerTensorIndex`（约 920 行）、`lowerEmbedding`（约 1190 行）、`lowerTake`（1170-1190）对"计算出的"下标同样直通；`lowerTake` 只对常量下标列表检查了 `< 0`。
- 失败场景：模型里计算出的下标恰为 -1 时，运行时故障，导出模型读最后一行，且不报错。切片已经用"Gather 零向量守卫"解决了同一问题（`lowerSliceSub` 的 `boundOf`），`take/embedding/idx/idx_arr` 没有套用。
- 修法：把切片里的守卫抽成 `guardIndex(e, index, extent)` 复用于所有 Gather；常量下标的 `i + n` 改为拒绝。注意 CUDA EP 的 `Gather` 对越界不一定失败（需确认），守卫依赖的"ORT 失败"只在 CPU EP 成立，应在文档里写明目标 EP，并让 `capability` 的目标只放行已验证的。

### O3 轴向归约丢掉了 7.3 / 7.7 的契约，而 `argmax` 却做对了

- `reduceAxisOp`（`lowering.cpp:670-690`）：`sum_axis/mean_axis/max_axis/min_axis` 在元素类型上直接 `ReduceSum/Mean/Max/Min`。运行时按 7.3 用 f64 累加、舍入一次（`reduceAllOp` 特意先扩成 f64 就是为此，`lowering.cpp:900-915`）；`max/min` 按 7.7 是 IEEE 754-2019（NaN 给规范 NaN，-0 低于 +0），`lowerArgMax` 为此写了一整套组合（`lowering.cpp:700-790`），但同一契约下的 `max_axis/min_axis` 直接用 `ReduceMax/Min`。`tensor:maximum/minimum`（`sameTypeOp("Max"/"Min")`）与 `tensor:relu`（`Relu`）同样没有保证 NaN 与 -0。
- 失败场景：f32 张量按轴求和，运行时先加到 f64 再舍入，导出在 f32 里累加，位不同；含 NaN 或 ±0 的行求最大，运行时给规范 NaN / +0，ORT 的结果依实现（需确认）。`relu(-0.0)` 运行时是 +0（`relu(x) = maximum(x, +0)`），ORT `Relu` 可能返回 -0。
- 修法：`sum_axis/mean_axis` 先 `Cast` 到 f64 归约再 `Cast` 回来；`max/min` 复用 `lowerArgMax` 里的 NaN/零处理（抽成 `ieeeMaxMin`）；`relu` 用 `Max(x, +0)` 加同样的零处理。补 ORT 对照用例：含 NaN、±0、全相等行。

### O4 非 f32 的张量被静默地换成 f32

- `emitter.cpp:398-430` 的 `Emitter::operand(value, dtype)` 在符号值的 dtype 与要求不同时自动插入 `Cast`。`softmaxOp`（约 780 行）、`lowerLayerNorm`、`lowerConv2d`、`poolOp`、`lowerBatchNorm*`、`lowerArrayIndex/lowerTensorIndex`（要求 `TypeCode::Float64`）都写死 `TypeCode::Float32`/`Float64`。
- 失败场景：f64 张量的 `softmax`（运行时 `softmaxInfer` 对 f64 给 f64）导出后在 f32 里算，输出节点的值信息却声称 f64（`ctx.emit` 用 `result_` 的 dtype），要么 ORT 加载失败要么结果精度悄悄降低；f32 张量元素读 `t[i]` 在导出里变成 f64 标量，之后的算术在 f64 里做，运行时在 f32 里做，位不同。这与 `lowering.cpp` 开头自述的"不做隐式转换"（5.1）矛盾，`operandDtype` 对逐元素算子有 `refuseMixedElementTypes`，这些算子没有。
- 修法：写死 f32 的算子先检查输入 dtype，不是 f32 则 `ExportError`（命名算子与 dtype），或按 dtype 参数化；`Emitter::operand` 对"符号值 dtype 与要求不同"改为内部错误，只有 lowering 显式要求的转换才走独立的 `emitCast`。

### O5 `zeros/ones/full` 对形状依赖输入时 dtype 不对，且 `full` 的值被压成 f32

- `constantOfShapeOp`（`lowering.cpp:808-830`）：值张量只有 Int64 与 float32 两种；dtype 为 Float64、Int32、Bool 时 ConstantOfShape 仍产生 float32，而 `out.dtype = dtype` 把值标成 f64/i32/bool。`full([n], v)` 的 dtype 取自值的类型（`creation_ops.cpp` `fullInfer`），f64 值被 `static_cast<float>`。
- 失败场景：`full([n], 0.1)`（f64）依赖输入形状时，导出模型的节点实际是 f32 的 0.1f，值信息说 f64。
- 修法：值张量按 `elemTypeOf(dtype)` 生成各类型的标量；至少对未支持的 dtype `ExportError`。

## 中

### O6 `layer_norm` 只按最后一个轴归一化

- `lowerLayerNorm`（`lowering.cpp:795-806`）写死 `axis = -1`；运行时 gamma/beta 可覆盖输入的末尾若干维（`layerNormInfer` 允许 `p->size() <= x->size()`，核函数用 `gamma.numel` 作宽度，`kernels/gradient.cpp` 的 `layerNormGrad` 同）。gamma 为二维时，导出的 `LayerNormalization(axis=-1)` 与 scale 形状不符，ORT 加载失败或按错误的范围归一化（需确认）。修法：`axis = -rank(gamma)`；并补用例。

### O7 `reshape` 的 `allowzero` 仅在 opset ≥ 14 写入

- `lowerReshape`（`lowering.cpp:343-348`）：低 opset 下形状里的 0 在 ONNX 中是"复制该维"，Camel 里 0 是零。目前 `kDefaultOpset = 17`（`proto/onnx_writer.h:34`），`export_model` 也没有暴露 opset（`ExportOptions::opset` 存在但 `module.cpp` 的 `exportKernel` 不传），所以这条路径不可达；但它和 `lowerGelu` 的 ≥ 20 分支、`reduceAxes` 的 `< 18` 分支一样，是没有任何测试的死代码。要么暴露 opset 并补各 opset 的 ORT 用例，要么删掉不可达分支。

### O8 能力表只有 onnxruntime-cpu，且"表里没有的算子"一律放行

- `capability.cpp:98-103` 的 `rejection`：算子不在探测表里就返回 `nullopt`（放行）。`CapabilityTable::forTarget` 只认 `onnxruntime-cpu`；导出的模型若在 CUDA EP 上跑，`Gather` 越界、`ScatterND`、`ConstantOfShape` 的行为都没有被探测过。修法：未探测的算子返回"未探测"并在导出报告中列出；增加目标 EP 时同步探测。

### O9 其他 dtype 的拒绝没有点名

- `elemTypeOf`（`emitter.cpp:134-157`）只认 f32/f64/i32/i64/bool，其余 `ExportError("unsupported tensor dtype")`，不说是哪个 dtype、哪个算子；bf16/f16 张量（`tensor` 模块支持）导出失败时用户看不出原因。同一函数开头有一个与 `switch` 里重复的 `Float64` 分支，注释"tensors store float32"已不成立。修法：点名 dtype 与算子，删掉重复分支和过时注释。

### O10 梯度降级的覆盖面

见 `code-review-autodiff.md` M8：`lowering_grad.cpp:694-715` 缺 `tensor:take_grad`、`tensor:row_add_at`、`nn:avg_pool2d_grad`、`nn:softmax_cross_entropy_grad`、`nn:embedding_table_grad`；多处 `ctx.input(…, TypeCode::Float32)` 写死（`lowering_grad.cpp:141,205` 等），与 O4 同类。

## 低（ONNX）

- `module.cpp:158`、`supportedOperatorsKernel` 声明为"有副作用，直到有人检查它不是"，这是占位式的声明，应当核定（它只读注册表，应为纯）。
- `exporter.cpp` 单文件 5021 行；大函数（按相邻函数起点估算）：`evalCall` ≈450 行、`evalIndirectCall` ≈416、`translateLoop` ≈345、`translateLinear` ≈310、`evalAccs` ≈237、`evalFill` ≈201、`recordFates` ≈103。建议按"调用/循环/聚合/变量"拆文件。
- 历史叙述型注释：`lowering.cpp:443`（`x[3::-2]` "was `[]` instead of `[4,2]`"）、`:323`（"plan section 5.3's migration table"）、`:851`、`:869-870`（"since the tensor flip … before the flip"）、`exporter.cpp:4581`（"the name is historical: the pass used to fold shapes"）。按现状改写。
- `NoCollectionScope` 罩住整个导出（`module.cpp:62-66`），注释说分配量由图大小界定，但权重常量被复制（`emitter.cpp:446-452` 的 `tv.raw.assign`），大模型导出期间 GC 完全停止，峰值内存约两倍权重。

## Tiles/GPU

### T1 `#gpu` 范围内的拒绝有三类"不算拒绝"的出口（需确认运行期是否补上）

- 设计 6.1：范围内不支持的算子编译期报 `GpuScopeUnsupported`，不回退。`graph_regions.cpp:205-235` 的 `gpuRefusal` 对 `unresolved_type`、`unresolved_index_instance`、`node_kind`、`control_edge` 返回空串（"the run retries with the actual shapes"），对副作用算子也返回空串（"a host effect is the boundary"）。若运行期重试后仍然无法解析，节点是否报错、还是静默留在主机，从代码里看不出（`test/cases/modules/tiles/gpu_scope_*.cml` 测了已知的几种）。失败场景：`#gpu` 块里一个形状无法解析的算子、一个 `while` 或 `match`，用户以为在 GPU 上，实际在主机，正是设计文档第 1 节批评的"静默回退，GPU 利用率 0–10%"。
- `capability.cpp:21-30`：没有 `--devices` 时 `configuredCudaOrdinal()` 返回 0，"没有设备则主机运行"。对 `#gpu` 范围而言，没有设备应当是错误还是整体回主机，设计 6.1 说"调试时 `--devices cpu` 整体跑在 CPU"，即默认不应静默回退；`gpu_fallback.cml` 钉住了现状，需要作者确认这是不是想要的。
- 修法：运行期的重试结果必须二选一——解析成功，或报 `GpuScopeUnsupported` 并点名节点；剖析报告对"范围内但在主机上"的节点逐个列出。

### T2 三份"支持的算子"表要手工保持一致

- `graph_regions.cpp:42-66` 的 `operations()`（17 个算子，无 `sigmoid/tanh/gelu/softmax/where/transpose` 等）、`compiler.cpp:787-1205` 的 `operation()`（`transpose/permute/slice`、行核 softmax 等，自有 `if` 链）、以及 `tensor::ops::OpRegistry`。区域提取用第一份，编译用第二份；一个算子只在其中之一，就会"能编译但提取不到"，或相反。`graph_regions.cpp:112` 还把任何非 f64 的节点按 `DType::F32` 当候选（整数、布尔、bf16 张量的算子也被当作 f32 评估，实际编译在 `knownResult` 处要求 f32，需确认不会漏进来）。修法：一张表（算子、dtype 集合、形状约束、核函数），提取与编译都读它。

### T3 设备上的数组仍是 Tiles 私有的包装，没有统一到数组值

- 设计第 3 节要求"设备上的张量就是数据块在设备上的数组值"。现状：`module.cpp:351-430` 的 `hostResidentValue` 把设备值包成 `ForeignResourceObject(shared_ptr<DeviceValue>)`，类型是 `Device<T>` 的独立类型；上传是整体复制（`retainHostValue`），且只支持 f32（`"host resident tensors require f32 elements"`）。这是 G2 未做，不是缺陷，但值得记入：上传/下载在 `NoCollectionScope` 内进行，整棵参数树的拷贝期间 GC 停止。

### T4 生产模块里带着测试专用的内核与区域通道

- `module.cpp` 的执行器表里有 `test_call`、`test_compiled_call`、`run_compiled`、`run_compiled_device`、`retain_host`、`run_host_resident`（`:176-186`），构造函数里 `registerRegionTestPasses(...)`（`:741-757`）把三条测试专用的区域通道注册进生产模块。设计第 6 节第 3 条已定"`run_preferred_resident` 保留为内部入口，其余删除"。修法：测试专用的搬到测试构建（`region_test_pass.cpp` 只在测试目标里编译），执行器表只留 `retain_preferred / run_preferred_resident / upload / download / source / inspect / regions / benchmark / available / device`。

### T5 字符串分派与大函数

- `Node.op` 是 `std::string`，全模块约 115 处 `op == "…"` / `ends_with("_axis")` 比较（`schedule.cpp` 37、`compiler.cpp` 19、`loop_kernel.cpp` 16、`serial_kernel.cpp` 14 …）。拼写错误编译期不会发现，且每次查询都是字符串比较；改成枚举（带 `uri` 与名字的表）即可。
- 大函数：`compiler.cpp` 单文件 1327 行，`eval`（`:511` 起约 235 行，含内联的结构体转换）、`operation`（`:787-1205` 约 420 行，标量、切片、布局、归约都在一条 `if` 链里）；`cuda.cpp:141` 起的 `outputHasRelaxedDependency` 约 300 行（按相邻函数起点估算，需确认）；`schedule.cpp` 的 `validate` ≈200、`validatePlan` ≈160、`schedule` ≈126。建议把 `operation` 按算子族拆成注册表项。

### T6 `placement.cpp` 对未知放置事实静默当作主机

- `placement.cpp:63-70` 的 `classify`：既不是 `gpu` 也不是 `cpu` 的事实按主机处理（注释承认"a value that is neither label … is answered the same way"）。若标签只会由 `#gpu/#cpu` 写入则不可达；若别的写入者能写，就是拼写错误静默变成主机。修法：不可达则 `ASSERT`，可达则报错。设计 G1 还有 `#cpu` 标签、驻留推导、Tiles 读图内区域未做，README 与设计里"已落地/未做"的两张清单需要对齐（`tiles/README.md:439-446` 仍写"不是所有导数规则"）。

### 资源泄漏

- 没有发现泄漏的证据：`IMPLEMENTATION.md` 描述了池、租约、失败后弃用而不回收的策略，并有 `memory_pool_failures.cpp`、`module_lease.cpp`、`async_submission_lifetime.cpp` 测试；我没有逐行审 `cuda_pool.cpp` 与 `cuda_submission.cpp` 的失败路径，这部分的结论是"未发现"，不是"已证明"。

## 测试缺口

ONNX（`test/cases/modules/onnx/ort_*.py` 是与 ORT 的对照，覆盖了 argmax、核心切片及其梯度、循环、训练步、动态参数）缺：

1. `softmax / log_softmax / layer_norm`（含二维 gamma）、f64 张量、`sum_axis / mean_axis / max_axis / min_axis` 的舍入与 NaN/±0（O3、O4、O6）。
2. 标量除零、浮点转整数越界/NaN、int 转 i32 越界（O1）。
3. 计算出的负下标经 `take / embedding / idx / idx_arr`（O2）。
4. 形状依赖输入的 `full / zeros / ones` 的 f64/i32/bool（O5）。
5. `maximum / minimum / relu` 的 ±0 与 NaN。
6. 非默认 opset（O7）；`capability` 对其他元素类型的拒绝文本。

Tiles：`#gpu` 范围内"形状运行期才解析且仍无法解析"、"范围内有 `while`/`match`"、"没有 `--devices` 且无设备"各给一个明确期望的用例（T1）；区域提取与编译两张算子表一致性的单元检查（T2）。
