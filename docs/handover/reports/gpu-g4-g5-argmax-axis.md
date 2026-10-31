# `argmax_axis` 进表之前：一页纸

## 零、先说三件已经付出过代价的事（先读这里）

1. **同一个谎有两个入口。** `schedule.cpp#bytes` 曾经"f64 八字节，其余四字节"，`graph_regions.cpp`
   的 `candidate.dtype` 曾经"非 f64 一律 F32"——两处都会把八字节的下标按四字节写／读，而数字看起来都对。
   加元素类型时，**宽度表和 dtype 映射两处都要查**；只改一处，另一处会安静地把下标读成半个。
2. **`tiles::source` 不能用来读 i64 结果的核**：它的签名是 `NumericAggregate → string`，i64 结果实测
   `NoMatchingFunction`。要读核源码，走 `tiles::compile` + 计划级读数，别在这条死路上再试一次。
3. **进表之前先确认节点携带的 URI，不要按前缀推断。** `argmax` 由 `stdlib/array.cml` 的
   `#op('array:argmax_axis')` 声明，图里的节点是 **`array:argmax_axis`**；表里其余名字都走 `tensor:`
   前缀，这一条不走。按前缀写表项会得到一个**没人携带的名字**，作用域照旧被拒（实测：
   `TILES_REGION_REASON array:argmax_axis unsupported_operator`，表里写的 `tensor:argmax_axis` 完全没被查到）。

这是 A 组最后一条（`tensor:argmax_axis`）的前置笔记，**只写判断，不写代码**。理由：它要动的是
类型系统与归约契约，不是设备程序；先有一页纸，才有一笔代码。这一条今天**不在**
`modules/tiles/src/graph_regions.cpp#operations` 表里，缺口账本
（`docs/handover/reports/gpu-g4-g5-gap-list.md`）因此停在 26（transformer_step 13 + C2 13），A 组剩它 1 条。

## 一、`i64` 结果在 device 侧需要什么

三样东西，缺一样都走不通：

1. **类型表**。`modules/tiles/include/tiles/dtype.h#DType` 今天只有 `F32`、`F64`，全仓按它分支的地方
   13 个文件（`git grep -c "DType::F64" modules/tiles`）。加 `I64` 意味着每个 `switch` 多一处宽度：
   `modules/tensor/dtype.cpp#elementSize` 那条按元素类型取 `sizeof` 的路由、CUDA 侧的标量类型、
   标量表达式（`modules/tiles/include/tiles/internal/kernel_ir.h#ScalarValue`）的类型集合。
   注意 **`Store` 也只能存今天这几种**：`kernel_ir.h#Store{Access, ScalarExpr}`，i64 的位置要存得进去，
   先得让 `ScalarExpr` 能是 i64。
2. **缓冲区**。`modules/tiles/include/tiles/device.h#DeviceValue{dtype, address, …}` 与
   `uploadValues`/`downloadValue` 都按 `dtype` 算字节数；`DeviceValue` 的 `dtype` 是唯一的地方告诉
   后端"这 8 字节是一个下标"。另有一个反直觉的事实：**类型层今天已经放它进来了**——
   `modules/tiles/src/device.cpp#DeviceType::get` 只要求 `tensor::isTensorType(host)`，而
   `modules/tensor/type.cpp#isTensorType` 认任何数值数组（`int64[..]` 也算张量），所以
   `Device<int64[2]>` 今天可以**被写出来**，只是没人知道它的元素类型是什么。真正的闸门是
   `device.cpp#deviceScalarDType`（只认 Float32/Float64，标量用）和下面的回读。
3. **回读**。`modules/tiles/src/tree_transfer.cpp#materialize` 是设备值变成 Camel 值的唯一出口：
   非标量叶子今天只认 `DType::F32`（否则 `download tensor type mismatch`），且
   `tensor::createTensor` 写死 `TypeCode::Float32`。要按叶子的 dtype 建张量
   （`tensor::createTensor(TypeCode::Int64, …)`）。

还有三处按 f32 判的闸门，会在这条算子走到它们之前就拒：

- `modules/tiles/src/graph_regions.cpp#shape`：结果的静态形状只从 f32 张量读得出（f64 读成标量，
  其余 `nullopt`），所以 i64 结果今天在 `eligibility` 里是 **`unresolved_type`**，连 lowering 都到不了。
- `modules/tiles/src/schedule.cpp#validate`：`require(n.dtype == DType::F32)` 与操作数的同一条要求。
- `modules/tiles/src/compiler.cpp:1094`：`operator '…' needs a statically known f32 tensor result`——
  这是把 i64 结果落在运行图里的那道门的原话。

## 一之二、第 1 步做完了什么（上传 / 内核 / 回读里的第三段，加两处宽度）

四步顺序的第 1 步是"`I64` 走通上传 / 内核 / 回读"。这一笔做的是**回读这一段的全部**、
**内核那一段里 i64 的真实能力**、和**两处宽度**；上传段只做了一半，理由在下面的清单里。

**读数**（`test/plans/feat/modules/tiles.plan.toml#compiled_region_i64_leaf_reads_back`，
`--devices cpu`，跑 `tiles::test_outline_compiled` + `std::nvm`）：

```
TILES_OUTLINED 2
TILES_I64 {"device":[3,5],"host":[3,5],"equal":true}
```

那一行是 `modules/tiles/src/region_test_pass.cpp#i64LeafReport` 打印的：两个位置先作为 i64 **值**
进内核表达式层（`ScalarExpr::literal(ScalarValue::i64(3))`），再进一个 `DType::I64` 的设备叶子
（`retainHostValue({2}, DType::I64, 16 字节)`，就是 `Device<int64[2]>` 的存储），`downloadValue` 把字节
取回，`tree_transfer.cpp#hostLeafValue` 按叶子的 dtype 建出 `TypeCode::Int64` 的 `[2]` 张量，与主机
`[3, 5]` 逐位相同（`equal: true` 是断言之后的结论，不是口号：不同就抛，条目会红）。

**这一笔改了哪几处，为什么是这几处**（只在"加法是穷举的"或者"默认值在说谎"的地方动）：

| 位置 | 为什么必须现在改 |
| --- | --- |
| `modules/tiles/include/tiles/dtype.h#DType` | 加 `I64`。它是"元素类型"这个名字本身。 |
| `modules/tiles/src/schedule.cpp#bytes` | 原来是"f64 八字节，**其余四字节**"——i64 会被按四字节读出一半。这是唯一一处"默认值在说谎"，必须改。 |
| `modules/tiles/src/scalar_expr.cpp#ScalarType(DType)` | `DType` → 寄存器类型的唯一桥；它有 `I64` 这个 kind 可以映射，且对未知类型是 `throw`（穷举的加法）。 |
| `modules/tiles/src/tree_transfer.cpp#hostLeafValue` | 回读的唯一出口，原来写死 f32；i64 分支就是第 1 步的交付物。上传侧的 tensor 分支也一并认 i64（八字节/元素，宽度取自 `bytes`）。 |
| `test/cases/modules/tiles/*` + 计划条目 | 上面那条读数。 |

**其余十三处按 `F32`/`F64` 分支的代码一律没动**，理由：那些 `switch` 里没有 i64 的分支就会落到它们
自己的 `default:` 或 `throw`，报错比"一次全改"安全；真正需要它们的时机是第 2 步（内核要存 i64）和第 3 步
（plan 里要出现 i64 节点），不是现在。

**顺带记一条内核侧的事实**（对第 2 步是好消息）：`scalar_expr.cpp:175-178` 明写
`I64` 只承载"静态循环域元素"，i64 **算术**被拒（"unsupported i64 scalar operation"），因为那是另一份
wrapping 32 位算术契约。也就是说 i64 在下标这里不是"不能算"，而是"不需要算"——`argmax_axis` 要存的
位置正是循环域里的那个元素（`k_r`），不是算出来的数。第 2 步的内核因此不需要动标量算术。

## 二、带下标的归约在契约里怎么表达

**契约这边已经有了，不用新写**：`modules/tensor/ops/indexing.cpp#positionLaw` 给 `argmax_axis` 挂了
三条 law（f32 / i64 / bool 输入），字段是

- `kind = LawKind::ArgMax`（`argmin_axis` 是 `ArgMin`）
- `accumulator = 输入的元素类型`，`result = TypeCode::Int64`
- `init = LawInit::FirstContribution`（第一个贡献者起算，不是单位元）
- `empty = LawEmpty::Fault`（空轴是错，不是 0）
- `exactAssociative`/`exactCommutative` 都为真，注释写明前提是"**carrying the positions**"

也就是说语义层表达的是"域上第一个极值的**位置**"，而不是"先算极值再回头找位置"。设备侧要表达的是
同一件事的计算形式：循环走归约域时，位置**就是**归约坐标本身——`_axis` 只有一个归约维
（`reduceAxisIndexing`），`reductionAccess` 的 `r` 已经是这一维的线性坐标。所以"带下标的归约"在
设备契约里不需要新的语义概念，需要的是一个**两槽累加器**：值（用同一个 IEEE maximum 序）与位置
（更早者胜），最后把位置写进 i64 结果。

## 三、`reductionAccess` 要长出哪一种入口

**顺序版本不需要新入口。** `modules/tiles/include/tiles/internal/kernel_ir.h#reductionAccess` 返回
`ReductionAccess{count, address}` 已经够：位置由 `r` 给出，值由 `address` 读。今天挡路的是归约内核
里对 law 的三选一——`modules/tiles/src/reduction_kernel.cpp` 的
`require(… law.kind == Max/Add …, "unsupported numerical law")`——`ArgMax`/`ArgMin` 会落在这条
`require` 之外。要加的就是那一条分支加一个内核体（`modules/tiles/src/serial_kernel.cpp` 的归约分支
里已经分出 `k_acc`，把它换成 `k_best` + `k_pos` 两个绑定即可）。

**并行/分块版本需要新入口。** 两段各自算出 (值, 位置) 之后要合并成一对，这不是
`reductionAccess` 能表达的；形态照着已有的"成员归约器"来——`serial_kernel.cpp` 的
`launch.implementation == "ordered_reduce"` 走的就是"归约器在一个成员节点上"的路
（`lower.reduction` / `reducer`），把合并规则改成"比较值、值相同取更早位置"即可，入口与它同级。
建议先做顺序版本 + 一次读数，再谈并行：今天 `argmax_axis` 在主机上就是串行 `kernels/reduce.cpp`
的语义，位置逐位可对。

## 四、"不放进表"这句话如何继续成立

表里的名字是"**有 device 程序**"的宣称（`operations()` 是 `deviceCapable` 的唯一定义处，
`modules/tiles/src/region_outline.cpp` 用它决定接受还是拒绝一个 `#gpu` scope）。所以顺序是死的：

1. ~~`DType::I64` 走通"上传 → 内核 → 回读"~~ —— **第 1 步已做**：回读整段通了（有上面的读数），
   宽度与寄存器类型两处穷举点改了，上传段只认了类型与宽度（见下面的清单）；
2. `ArgMax` law 的内核分支落地（顺序版）；
3. `eligibility` 的归约分支已经能接（`_axis` 走 `DimRole::Reduction` 求 `attributes`，
   再 `reductionAccess`），但 `graph_regions.cpp#shape` 要能读 i64 结果，否则永远停在 `unresolved_type`；
4. **一条 device-free 读数**：`--devices cpu` 下 `#gpu` scope 里的 `argmax_axis` 位置与主机逐位相同
   （照 `test/cases/modules/tiles/gpu_scope_views.cml` 的写法钉值）；
5. 这之后才把 `tensor:argmax_axis` 写进 `operations()`，C2 的读数才允许从 21 = 8 + 13 变成 21 = 9 + 12。

### 四个"真正拒它的地方"：谁现在改、谁留到第几步、为什么可以留

| 闸门 | 决定 | 理由 |
| --- | --- | --- |
| `device.cpp#deviceScalarDType` | **留到第 2 步之后**（今天不改） | 它管的是**标量**叶子；`argmax_axis` 的结果是张量，走的是 tensor 分支，与它无关。而改它等于宣称"设备能持有一个 i64 标量"，那是把一个没有程序的结果形状提前放进类型面。 |
| `graph_regions.cpp#shape` | **留到第 3 步** | 它是 `eligibility` 的入口：改它，i64 结果就能进入 lowering，而 lowering 那边没有程序（第 2 步才做）。先改这里只会把拒绝从 `unresolved_type` 变成更靠后的、更难看懂的一种。 |
| `schedule.cpp#validate` | **留到第 2 步之后** | 它守的是**计划**：只有第 2 步让内核能存 i64，计划里才可能出现 i64 节点。现在放宽，等于允许一个内核无法存储的 dtype 进计划。 |
| `compiler.cpp:1094`（`needs a statically known f32 tensor result`） | **留到第 3 步** | 它是"算子结果能不能落在运行图上"的那句话。第 1 步只让**已经存在的** i64 字节能被正确读回，没有新增任何能产生 i64 结果的算子；这句话这时是对的，不能提前松。 |

四条的共同理由一句话：**这一笔只让 i64 的"存储与回读"正确，没有让任何东西能"产生" i64 结果；
任何让产生变可能的改动，都属于第 2、3 步。**

在第 4 步有读数之前把名字填进表，只有两种下场，原话留档在这里：**"提前进表只有两种下场：在
`unresolved_type` 处拒（数字好看、能力没变），或者不拒而把 4 字节的 f32 结果按 8 字节下标读出去。"**
「不拒」那条今天已经有实证：宽度那一处如果不改，`bytes()` 就会按四字节算，回读出来的下标正好是半个。

## 五、位置是**下标本身**：新增 `ScalarExpr::fromIndex`（只读，不加算术）

第 2 步要把位置接到真内核上（`argmax` 分支 + 两槽累加器）。开工前把四条路走了一遍，结果是
**存储那一半本来就能写，更新那一半缺一个原语**——缺的不是 i64 算术（那个本来就被拒），而是
"**把循环下标读成一个寄存器值**"这一步。先把边界交回去，决定选了最小的一刀：

**`ScalarExpr::fromIndex(IndexExpr, ScalarType)`**（`scalar_expr.h`）——把一个下标读成 **i64 寄存器值**，
只读、不参与算术。它与 `scalar_expr.cpp:175-178` 那句"i64 只承载静态循环域元素"**同向**：那句禁的是
**算术**。三处配套：`scalar_expr.cpp#Node::Kind::Index`（`evaluate` 按 index 绑定取名、`symbols()` 报成
i64、`cuda()` 输出 `((long long)(k_r))`）、`kernel_ir.cpp#scalar`（下标读按 index 校验，非循环名仍是
`unbound index`）、`modules/tiles/CMakeLists.txt`（`tiles-module-lease` 一并编 `index_expr.cpp`）。

**读数**（`test/cases/modules/tiles/kernel_ir_check.cpp#checkI64PositionRead`，`plan_validation` 钉住）：

```
tiles i64 position: the loop index reads into an i64 slot and stores that position, a plain symbol of the same name still refuses, and i64 arithmetic stays refused
```

一条用例同时钉三件事，缺一条就有人以为路开了：

| 断言 | 手段 |
| --- | --- |
| 下标读**通过**校验、且**存进去的确实是那个下标** | `validateBody` 通过 + `emitBody` 里出现 `k_pos=((long long)(k_r));` |
| 同名的**普通符号仍被拒** | 同一个 body 换成 `symbol("k_r", I64)` → 恰好 `unbound scalar` |
| **i64 算术仍被拒** | `apply(Add, {fromIndex(k_r), i64(1)})` 抛 `invalid_argument`（`k_pos += 1` 这条路没有开） |

A/B 就是上面第二行：把 `fromIndex` 换回普通符号，读数回到 `unbound scalar`。

（下面是当时停下来的记录，留作"为什么是这个原语"的依据。）

四条路都查过，没有第五条：

| 想走的路 | IR 的态度 | 结论 |
| --- | --- | --- |
| `k_pos = k_r`（下标读进 i64 寄存器） | `kernel_ir.cpp#validateBlock` 把 `Loop` 的索引声明为 **index**（`declare(nested, s.index, {true, …})`），`scalar()` 要求 `!index` → `unbound scalar` | 缺一个原语 |
| `k_pos` 按 i64 算术累加（每轮 +1） | `scalar_expr.cpp:175-178` 明拒，`checkScalarI64` 早已在钉 | 被设计拒 |
| `Store` 直接存一个下标 | `Store` 的 value 是 `ScalarExpr`，只有 `Access.address` 是下标 | 缺一个原语 |
| 先用 `IndexBinding` 起别名再读 | 仍是 index 绑定 | 同第一条 |

**所以停在这里，把选择交回去**（三个候选，都不等于"放宽 i64 算术"）：

1. **`ScalarExpr::fromIndex(IndexExpr, ScalarType)`**——最小的一刀：允许把下标读成 i64 寄存器值，只读、
   不引入算术。它与 `scalar_expr.cpp:175-178` 那句话不冲突：那句禁的是**算术**，不是**读取**。
2. **让 `Loop` 的索引同时可当 i64 标量读**（`declare(nested, s.index, {true, DType::I64, …})` 并对
   i64 的 index 绑定放行）。改动面更大，且把 `IndexExpr` 与 `ScalarExpr` 的边界弄模糊。
3. **加一条 `IndexStore` 语句**：把 `IndexExpr` 直接存进 i32/i64 缓冲。概念最干净（下标本来就是下标），
   但要动 `codegen`、`validateBody` 与 `KernelBody` 的语句集合。

倾向 **1**。**在拿到决定之前不动 `reduction_kernel.cpp` 的 law 门**：先让 `domain()` 接受 ArgMax 而
`serial_kernel.cpp` 没有分支，`lower.value(...)` 会把输入值当结果存下去——那比"数字好看、能力没变"更糟，
是错的数。

### 顺序按"先有分支、再开门"落实了

`fromIndex` 落地之后，第 2 步的剩余部分按这个顺序做完（顺序不是洁癖：反过来就会得到上面那个错的数）：

1. **`serial_kernel.cpp` 的 ArgMax/ArgMin 分支**（先写，写了也还是死代码）——两槽：`k_best` 是 f32、
   `k_pos` 是 i64；循环体里 `k_x` 取值、`Conditional{Less(k_best, k_x)}` 成立才 `k_best = k_x` 且
   `k_pos = fromIndex(k_r)`；严格小于 ⇒ 相等时保留更早的位置；结束时 `result = k_pos`。
   **位置的写入是 `fromIndex`，不是算术**，所以 `k_pos += 1` 这条路仍然不存在。
2. **`reduction_kernel.cpp#domain` 的 law 门**——位置定律单列一支：`input/accumulator == f32`、
   `result == Int64`、`init == FirstContribution`、`order == Ascending`、`finalize == None`、`kind` 与算符一致。
   同时 `canonicalAxisReduction` 对这两条算子**返回 false**：位置走通用归约，物理 axis 内核答的是 f32。
3. **`schedule.cpp#validate` 的窄放宽**——只对 `argmax_axis`/`argmin_axis` 允许 i64 结果（`positional(op)`），
   其他任何算子仍是"结果必须是 f32"。**这条不是提前松闸，而是让第 2 步的程序真的能被计划出来**：
   没有它，`generate()` 会在自己的门上报错，两槽分支永远没有一条计划走到过。

**读数**（`test/cases/modules/tiles/reduction_domain_check.cpp#checkReductionDomains` 末尾，
`plan_validation` 钉住）：

```
tiles position reduction: an argmax plan keeps the value and stores the loop index as i64 in its planned body and source
```

它断言的是**计划出来的东西**，不是手搭的 body：`argmax_axis`（`f32[2,3]` → `i64[2]`）经 `generate()` 得到
**一个** launch、走通用归约（`canonicalAxisReduction` 为假）、贡献数 3；该 launch 的 `serialBody`
过 `validateBody`，`emitBody` 与最终 `program.source` 里都有 `k_pos=((long long)(k_r));`。

### 今天还有什么在阻止 `tensor:argmax_axis` 进表

1. ~~**内核程序（第 2 步）**~~ —— **已做**：law 门、两槽分支、窄放宽、计划级读数都在。**能力这一侧不再有闸门。**
2. **`graph_regions.cpp#shape`（第 3 步）**——i64 结果是 `unresolved_type`，`eligibility` 到不了 lowering；
   这是现在第一道、也是唯一一道拦在表外的门。
3. **`compiler.cpp:1094`（第 3 步）**——`needs a statically known f32 tensor result`；它是"算子结果能不能落在
   运行图上"的那句话，与第 2 项一起构成第 3 步。
4. ~~**`schedule.cpp#validate`（第 2 步之后）**~~ —— 已按算子窄放宽（见上）。
5. **`--devices cpu` 的那条位置读数（第 4 步）**——要等第 3 步让 `argmax_axis` 进得了 `#gpu` scope。

`deviceScalarDType`（标量叶子）**不在这条路上**：结果类型是张量，走 tensor 分支。

## 附：这一笔之后账本上的位置

- A 组 5 条：4 条视图已进表（`a3d119690`），剩 `tensor:argmax_axis` 这 1 条，卡在本文第一、三项。
  缺口**没有变**，还是 26：这一笔让 i64 的存储与回读对了，没有让任何东西能产生 i64 结果，
  `argmax_axis` 仍未进表。
- 缺口 26 = transformer_step 13（`nn:sgd`、`gelu`、`gelu_grad`、`layer_norm`、`layer_norm_grad`、
  `matmul_grad_lhs`、`matmul_grad_rhs`、`numel`、`permute_inverse`、`shape`、`softmax`、`softmax_grad`、
  `zeros_like`）+ C2 13（`nn:adam`、`nn:adam_state`、`softmax_cross_entropy_labels`、
  `softmax_cross_entropy_labels_grad`、`argmax_axis`、`cast_like`、`gt`、`matmul_grad_lhs`、
  `matmul_grad_rhs`、`numel`、`shape`、`strict_eq`、`take`）。其中 12 条是 B 组（等作者的语义声明那页），
  `argmax_axis` 是本文这一条。
## 六、两个探针，和"声明缺失"的四格读数

**探针是通用件，不专属 argmax**（`region_test_pass.cpp`，由 `reasons=true` 打开）：

```
TILES_REGION_REASON <uri> <reason>            # 该节点为什么不在任何区域里；eligible 表示进了区域
TILES_OPERATOR_DECLARATION <uri> <declared|undeclared> major=<n>
```

任何"算子进不了区域"的问题，这两行都能直接回答"谁、为什么"，不必再读分析代码。

**四格读数**（同一个作用域，`--devices cpu`）：

| URI | 声明 | `major` |
| --- | --- | --- |
| `array:argmax_axis`（图里真正携带的那个） | **undeclared** | **-1** |
| `tensor:reshape`（已在表里，作对照） | declared | 1 |

**结论一句话：要修的是"注册"，不是"版本"**——`array:argmax_axis` 这个 `OperId` 上**根本没有语义声明**，
所以 `graph_regions.cpp:142` 的 `!decl || major != 1` 里命中的是前半句，而不是后半句。两种修法二选一，
都必须先有一条读数证明"声明挂上了、major 是 1"再动表：

1. 让 tensor 侧把 `positionLaw` 挂到 `array:argmax_axis` 这个 id 上（如果它和 tensor 注册的是同一个算子）；
2. 或者承认两者是**两个** `OperId`，在区域那一侧把 array 的名字映射到有声明的那一个（映射必须是
   显式的、带读数的，不能靠前缀猜）。

### 六之二、"同一个算子两个 URI"——这里停下，要作者裁

读数（`reasons=true`，同一条作用域）：

```
TILES_OPERATOR_ID array:argmax_axis id=340 tensor_spelling=422
TILES_OPERATOR_ID tensor:reshape id=406 tensor_spelling=422
```

`array:argmax_axis` 是 **340**，`tensor:argmax_axis` 是 **422**，**两个都已在表里存在**（`lookup` 都查得到），
**是两个 `OperId`**，不是同一个算子的两种拼法。图里的节点带 340，而语义声明挂在 422 上——所以 340 上
`undeclared`，`graph_regions.cpp:142` 命中 `!decl`。

**决定：不在区域侧加映射把它盖过去。** 两个 URI 意味着同一个算子有两条身份，这会同时影响**进表**和
**导出格式**（`onnx::export`、`std::jgir` 里的算子名），区域侧加一层映射只会让第三处知道这件事、前两处继续不一致。
这一步要作者裁：哪个拼法是正名，另一个是别名还是应当不存在。

在等答复期间并行的事：330/422 之外的 A 组工作已经没有了，缺口停在 26、`operations()` 停在 21。

### 六之三、B 组不是一类，是两类（口径已改在账本原位）

`CAMEL_TILES_TEST_DECLARATIONS=1` 的读数：5 条 `nn:*`（`sgd`、`adam`、`adam_state`、
`softmax_cross_entropy_labels`、`softmax_cross_entropy_labels_grad`）**`lookup` 返回 none——不在算子表里**；
16 条 `tensor:*` 在表里但 `undeclared major=-1`。**前者要注册，后 15 条要写声明，排期不能合并。**另外第六条读数推翻了"B 组"这个划分本身：**`tensor:softmax`（id 424）已有声明 `major=1`**，它属于 A 组，缺的只是表里一个名字与 device 程序——所以 21 条是**三类**，不是两类。
缺口账本 `gpu-g4-g5-gap-list.md:39` 那句已原位改掉（不是另起一段），否则下一个人照旧读到旧口径。

### 七、身份统一之后：`major` 清单，与"统一不等于变得可导出"

**并排的两句**：

- **`argmax_axis` 现在读作 `unsupported_operator`**（节点已带 `tensor:argmax_axis`，表里仍没有这个名字）。
- **命名统一不会让一个算子变得可导出；它只是让"不可导出"的原因从*命名错*变成*表里没有*——后者才是要修的东西。**

**身份统一之后，`operations()` 仍然是 21（未动）**：这一笔只改 stdlib 的声明位置与模块注册，没有向表里加任何名字。
**"统一身份"与"声称能力"是两件事**——今晚有一份"能力未动"的报告，这里再点明一次，免得读者以为它悄悄变了。

**19 个缺口名字的 `major` 读数**（`TILES_OPERATOR_DECLARATION` / `TILES_DECLARATIONS_*` / 声明表源读）：

| 名字 | `major` | 来源 |
| --- | --- | --- |
| `tensor:argmax_axis` | **1** | 探针（本轮，计划已钉住） |
| `tensor:softmax` | **1** | 探针 |
| `tensor:gelu` | -1 | 探针 |
| `tensor:gelu_grad` | -1 | 探针 |
| `tensor:layer_norm` | -1 | 探针 |
| `tensor:layer_norm_grad` | -1 | 探针 |
| `tensor:softmax_grad` | -1 | 探针 |
| `tensor:matmul_grad_lhs` | -1 | 探针 |
| `tensor:matmul_grad_rhs` | -1 | 探针 |
| `tensor:cast_like` | -1 | 探针 |
| `tensor:gt` | -1 | 探针 |
| `tensor:strict_eq` | -1 | 探针 |
| `tensor:take` | -1 | 探针 |
| `tensor:numel` | -1 | 探针 |
| `tensor:shape` | -1 | 探针 |
| `tensor:zeros_like` | -1 | 探针 |
| `tensor:expand_axis` | -1（有声明）* | 源读 `semantics.cpp:233` |
| `tensor:zeros` | **无声明** | 源读（`semantics.cpp` 无此条） |
| `tensor:dim` | **无声明** | 源读（`semantics.cpp` 无此条） |
| `nn:adam`、`nn:adam_state`、`nn:softmax_cross_entropy_labels`、`nn:softmax_cross_entropy_labels_grad` | **无声明**（且不在算子表里） | 探针（`lookup` = none） |

*`expand_axis` 的 `major` 数本轮未取（源读到"有声明"，版本未读）。

**合计**：`major=1` 两个（`argmax_axis`、`softmax`）；有声明但版本未读一个（`expand_axis`）；`-1` 十五个；连声明都没有的五个 `nn:*` + `zeros` + `dim` 共七个。

### 八、`argmax_axis` 的内核查：三种结果里的第一种，且有内核

**结论：有内核，只是名字没进 `operations()`。** 三处原文：

- `modules/tiles/src/serial_kernel.cpp:145` —— `else if (op == "argmax_axis" || op == "argmin_axis")`，两个槽：极值决定、位置就是循环下标，下标原样写进 i64 槽（不做算术，所以并列时保留更早的位置）。
- `modules/tiles/src/reduction_kernel.cpp:153` —— 同一算子参与归约规则。
- `modules/tiles/src/schedule.cpp:135` —— `positional("argmax_axis")` 为真。

**所以 `unsupported_operator` 不是"真的不支持"，而是"表里没登记"**：设备程序能生成，只是 eligibility 这一层没放它过去。
**补的是一个名字，不是一条降级路径**——与 ONNX 的 `ArgMax`（那是导出侧的 lowering，另一件事）不同。

### 九、`operations()` 的真值：24，不是 21

数函数体（`modules/tiles/src/graph_regions.cpp:46-82`），两个来源：

- **字面量条目 22 条**：`neg`、`abs`、`exp`、`log`、`sqrt`、`rsqrt`、`relu`、`add`、`subtract`、`multiply`、`divide`、`maximum`、`minimum`、`matmul`、`sum_axis`、`mean_axis`、`max_axis`、`sum_to`、`transpose`、`permute`、`reshape`、`broadcast_like`；
- **`emplace` 条目 2 条**：`tensor:sum` → `sum_all`、`tensor:mean` → `mean_all`。

**合计 24。** "21"这个数此前怎么来的：它是**布局算子那一族（`transpose`/`permute`/`reshape`/`broadcast_like`）加入之前**的数，
之后没人重数过。**21 应更正为 24**；`TILES_CENSUS ops=` 打的是程序里的算子数，不是表的条数，两者不能互证。

### 十、一条判据

**一处能力有两个数（表里的名字、设备上的内核），只有一个数变了不算能力变了。** 今晚 `argmax_axis` 的 `major` 从 -1 变 1，
**能力一个没变**——同一晚，它的设备内核一直在（`serial_kernel.cpp`），只是名字没登记。

### 十一、窄浮点：今天的位置，与 (b) 的五步

**一条被推翻的诊断**：我曾说 `graph_regions.cpp#elementType` 把 `f16`/`bf16` 静默当 `F32`（2 字节存储、4 字节读取）。
**读门那一行发现不成立**：`graph_regions.cpp:41-43` 在它之前就把非 `Float32`/`Int64` 的类型拒了（返回 `nullopt`），
兜底那一行对窄浮点不可达。**真缺口只有一个 `enum`**：`modules/tiles/include/tiles/dtype.h:11` ——
`enum class DType { F32, F64, I64 };`，**设备侧没有窄浮点的表示**。

**唯一一个带 `#gpu` 的窄浮点用例**（`test/cases/gpu/nofallback/scalar_literal.cml`）单独跑出来的读数：
```
{"segments":[],"regions":[],"transfers":{...},"excluded":[{"node":0,"reason":"node_kind"},{"node":1,"reason":"node_kind"},{"node":2,"reason":"unresolved_type"}]}
```
**它是"排除通过"，不是"支持通过"**：那个 `bf16` 张量的算子被 `unresolved_type` 排除，计划却通过——因为它的描述钉的是
"不再因 `scalar_tensor_operand` 被排除"，那是**另一件事**。**这是"读数说的是另一件事"的又一例。**

**(b) 的五步（现在立计划，不马上做）**：

1. **`DType` 加 `F16`/`BF16`** —— 验收：`tiles/dtype.h` 两个新值，`dtypeWidth`/`isFloating` 覆盖它们，编译通过且无行为变化。
2. **`serial_kernel` 能对窄浮点发标量表达式** —— 验收：一个 `bf16` 逐元素 add 生成段程序，读数为"生成成功、无 `unresolved_type`"。
3. **CUDA 代码生成用 `__half`/`__nv_bfloat16`** —— 验收：生成源文本里出现对应类型名，且 `f32` 计算-窄化写回的形状与主机内核一致（字符串读数即可，不需 GPU）。
4. **类型门放行窄浮点**（`graph_regions.cpp:41`）—— 验收：`TILES_REGION_REASON <op> eligible`，且 `TILES_CENSUS` 的 `gap` 不因它变大。
5. **能力表认领** —— 验收：`operations()` 里对应的名字带窄浮点支持、`TILES_CENSUS_GAP` 计数下降一条，并且一条 bf16 用例在 `--devices cpu` 上与主机结果逐元素相等。

**（a）今天的边界要写明**：设备侧看不到 `bf16` **是边界，不是缺陷**；它会作为（b）第 1 步之前的一条读数留在本页。
