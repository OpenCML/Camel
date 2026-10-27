# 原生多维数组与数据块 —— 实施计划

状态：**计划，待协调者批复**。本分支只改文档：不提交任何代码，因而不改变任何被观察到的行为。基线提交 `6a1c40c0`（2026-10-21，`develop`），构建与 `smoke` 已通过。

依据：

- [原生多维数组、内存管理与互操作](README.cn.md) 第 9 节第 1–3 步；
- [设计原则](../../design-principles.cn.md) 5.4（原生多维数组与维度）、10.5（库的边界与内存）；
- 批复 [计划批复]、[检查意见]、[测量复核] 的口径；
- 旧计划（已被本文件取代，保留作历史）；
- 已落地的相关工作：本计划的摘要报告 （现状、关键判断、已读文件与验证）、、、、。

本文的每个结论都对应到文件与类型。读它不需要读代码；第 13 节列出会被触碰的文件的完整清单。

---

## 0. 一页结论

任务四项，当前状态：

| 任务 | 状态 | 还差什么 |
|---|---|---|
| 1. 缓冲区对象与引用计数 | **已落地**（第一批 + 紧凑存储） | 三个证明缺口：外部释放回调恰好一次、外部缓冲区永不原地写、大数组循环的 GC 峰值有界（P1） |
| 2. 原生类型 `T[维度...]` | **未开始** | 核心 `ArrayType` 带维度；语法与绑定；形状检查进核心；`Tensor` 别名；第一维语义（P2–P5） |
| 3. 计数为 1 时原地写 | **部分落地** | 今天只有 `sort` 家族、`append`、`set_index`、循环状态；其余返回数组的算子尚未分类与覆盖（P6） |
| 4. 性能基线 | **未做** | 交错 A/B、先测噪声底、加张量程序、写进基线文档（P7） |

七个提交，按顺序：

| 步骤 | 内容 | 能否单独落地 | 门禁 |
|---|---|---|---|
| **P1** | 数据块自检收口（只加测试） | 能，且不改行为 | `feat/mm`、`feat/modules/tensor` |
| **P2** | 核心 `ArrayType` 带维度（无语法、无使用者） | 能，且不改行为 | `smoke`、`feat/mm`、`feat/modules/tensor` |
| **P3** | `T[维度...]` 语法、AST、绑定、删除形状位置的 `constas` | 能（P2 之后），合法程序行为不变 | `feat/parse`、`feat/compile`、`feat/modules/tensor`、全部张量用例 |
| **P4** | `Tensor` 别名、删除 `TensorType`、打印统一、元素类型精确 | 能（P3 之后），**本项最险** | 全量 `feat`、`smoke`、`feat/perf`（时间以外） |
| **P5** | 第一维语义：`map`/`filter`/`foreach`/`reduce`/`sort` | 能（P4 之后），也可拆成两次提交 | `feat/modules/tensor`、`feat/mm/hof` |
| **P6** | 原地写的广度与分配计数测试 | 能（P2 之后；排在 P5 后以便一次测完） | `feat/mm`、`feat/loops`、`feat/modules/tensor` |
| **P7** | 性能基线与记录 | 能，不改行为 | 手工测量，无测试计划 |

**最险的是 P4**（删除第二套张量类型）：它同时改类型身份、打印、入口参数与元素精度，波及 6 个模块（tensor、nn、onnx、tiles、tree、autodiff）、25 个文件的 51 处 `asTensorType`、87 个用例文件的 203 处 `const [` 与全部 464 处 `Tensor<` 拼写。它是**一个原子提交**，回退它不影响 P1–P3，见第 11 节。

**不改变行为**：`T[]` 仍是 `int[?]`，今天的程序（除 `Tensor` 拼写改写外）语义不变；`size()` 仍是第一维；`numel()` 仍是元素总数；小数组内联与 256 字节阈值不变；DLPack 仍只接受数值标量元素、CPU、连续布局。

不在本项：下标与切片语法、非连续视图（`transpose` 作视图）、秩 0、编译期规划/后端/Python/稀疏（设计第 9 节第 4–7 步）、临时值原地写、`constas` 在形状位置以外的退休。

---

## 1. 基线：现在有什么

### 1.1 值、数据块与视图（第 1 项）

一个数组值现在是**一个 `Array` 对象**，元素数据要么在对象尾部（内联），要么在一个 `mm::Block` 里：

```
Array（GC 对象，可移动）                    mm::Block（非 GC 对象，固定地址）
┌──────────────────────────────┐           ┌──────────────────────────────┐
│ type, storage=Compact|Slots  │           │ bytes, alignment 64          │
│ extents[], strides[]         │  block_   │ owner: Camel | External      │
│ elemTypeCode_, elementSize_  │ ────────► │ atomic refCount_             │
│ dataPtr_, offset_, numel_    │           │ release callback (External)  │
│ view_                        │           │ data[]                       │
└──────────────────────────────┘           └──────────────────────────────┘
```

- `include/camel/core/mm/block.h`、`src/core/mm/block.cpp`：`Block::create/retain/release/releaseFromFinalizer`、`unique()`、`camelOwned()`、`grow`、`setBytes`；`kInlineBlockBytes = 256`、`kBlockAlignment = 64`；`refCount_` 是原子量（`acq_rel`），因为 `std::prl` 与张量内核在多个任务里持有同一个块。`BlockOwner::External` 带一个 `BlockRelease`，计数到 0 时调用一次。
- 内联：`numel * elementSize <= 256` 时 `dataPtr_` 指向 `Array` 对象尾部，没有块、没有计数、没有终结器；导出 DLPack 或取视图时一次性提升为块（`Array::growCompact`、`ensureBlock`，`src/core/rtdata/array.cpp`）。
- 视图：`Array::view(source, extents, elementOffset)` 只接受紧凑数组；源是内联时先提升；视图是一个独立的 `Array` 对象，`view_ = true`，共享块并持有一份引用，带字节偏移。`canWriteInPlace()` 在视图、外部块、计数 > 1、偏移 ≠ 0 时为 false；内联数组为 true（没有块，别的值看不到它）。
- 终结：`Array::finalize()` 调 `block->releaseFromFinalizer()`，后者把释放推迟到 `mm::deferRelease` / `runDeferredReleases()` 的解锁后队列，避免在 GC 的原子段里跑外部回调。
- 统计：`std::mem::blocks()` 返回 `allocs/frees/live/bytes`（`src/builtin/modules/globals.cpp` 声明，`block.h` 的 `blockCreations/blockFrees/blockLiveCount/blockBytesLive`）。分配计数测试读它。

**收集器看见什么**（设计原则 10.5 的三层）：

- GC 扫描的是 `Array` 与 `FixedArray` 这类对象（`src/core/rtdata/array.cpp` 的 `updateRefs`）；紧凑数组在读引用之前就返回——元素是纯数据，没有引用可扫。
- `Block` **不是** GC 对象：没有类型头、不进堆、不被扫描、不被移动。它只有计数与所有者。
- 一个 `Array` 恰好持有一个块的引用；每个视图是独立对象、独立持有一份，所以块比源值活得久（`x = xs->view(...); xs = ...` 之后视图仍可读）。
- 块字节计入压力：`src/core/mm/gen.cpp` 的 `oldBytesHeldUnlocked()`、`blockBytesGrowthUnlocked()`、`checkBlockBytes()`、`blockBytesAtCollection_` 把活着的块字节算进年轻代/全量触发、大对象预算与堆上限请求。这是"循环里反复产生大张量不会先耗尽内存"的机制。
- 纯数据判据是 `type::isPureDataType()`，不是"只放数字"：结构体/元组/字符串数组也能紧凑存储（纯数据判据）。**任务里"缓冲区只放数字"的红线按纯数据判据落地**；"只放数字"只保留在 DLPack：`toDLPack` 断言元素是数值标量，`fromDLPack` 拒绝非数值。

### 1.2 数组类型（第 2 项的起点）

`include/camel/core/type/composite/array.h`、`src/core/type/composite/array.cpp` 的 `ArrayType` 今天只有 `elemType_`、`elemTypeCode_`：

- 驻留键是元素类型指针一个词（`TypeTable::Shape::Array`），所以每种元素类型只有一个 `int[]`；
- `toString`/`mangle` 由 `src/core/type/text.cpp` 统一处理（`typeItem(elem) + "[]"`）；
- `castSafetyFrom` 只承认 `equals`；`castSlotFrom` 在 Safe 时原值返回；`assignableFrom` 对元素协变；
- 没有 `unify`、没有 `widened`（继承 `Type` 的默认）、没有维度。

维度所需的零件已经在核心：`include/camel/core/type/extent.h` 的 `ExtentForm`（Unknown/Constant/Linear，规范化的项表、`evaluate`、`bind`、`operator==`、`toString`）与 `ExtentValues = std::map<std::string, int64_t>`。今天的 `TensorType`（`modules/tensor/type.cpp`，439 行）在模块里重复实现了这一整套：形状、`unify`、`widened`、`castSafetyFrom`、`extentForms`、`withExtentValues`、打印、驻留。第 2 项的核心动作就是把它搬进 `ArrayType`，然后删掉它。

### 1.3 张量模块：要被替掉的第二套类型

`modules/tensor/type.h/.cpp` 的 `TensorType : OtherType` 带可选 dtype 与可选形状，自己的驻留表、`kUnknownDim = -1`、`equals/unify/widened`、形状检查、`cloneWithParams/cloneWithArguments`（解析 `Tensor<f32, const [n, m]>`）。模块以此作为：

- 导出的类型 `Tensor`（已随 P4 的删除步走掉：`modules/tensor/module.cpp#exportEntity` 只导出算子实体，类型名不再导出；写 `Tensor` 现在是未知名字，过渡诊断 `TensorElementTypeRequired` 一起删除，见 5.1a）；
- 所有算子推断的返回类型（`modules/tensor/ops/*`、`support.cpp` 的 `tensorOf`）；
- 值类型细化器（常量数组 → dtype+形状）与 `.npy` 入口加载器的类型键；
- `modules/nn`、`modules/onnx`、`modules/tiles`、`modules/ckpt`、`modules/autodiff` 识别"这是不是张量"的依据（`asTensorType`，25 个文件、51 处）。

它也是"元素类型精确"的障碍：`normalizeTensorDType` 把 f64 读成 f32、i32 读成 i64，`Tensor<float>` 与 `Tensor<f32>` 同义。

### 1.4 第一维与原地写（第 3 项的起点）

- `Array::size()` 已经是第一维、`numel()` 是元素总数、`strides_` 已按行主序填好；运行时已经能创建 rank-n 数组。
- 但类型说不了形状：`Tensor<f32>` 是模块类型，`f32[3]` 的语法不存在；`map`/`filter` 等的算子规则（`src/builtin/modules/globals.cpp`）按 `arrType->elemType()` 得出元素类型，对 rank-n 数组按第一维的语义还没有实现。
- 原地写：`OperatorTable::setInPlaceForm` + `Builder::inPlaceRebindingOf` 把 `xs = xs->sort()` 改写成调用原地形式；今天注册的有 `array` 的排序家族（`src/builtin/operators/array.cpp`）、`:op/set_index` 与 `append`（`:struct/append_arr_in_place`）。`src/core/rtdata/array.cpp` 的 `reusableBlock`/`adoptBlock`/`canWriteInPlace` 是现成的积木。

### 1.5 缺口清单

1. 没有"外部释放回调只跑一次""外部块永不原地写""大数组循环峰值有界"的测试（P1）。
2. `ArrayType` 没有维度、没有 `unify`/`widened`、没有形状检查；`T[]` 也没有显式的未知维度（P2）。
3. 语法只认 `[]` 重复（`src/parse/syntax/parser_type.cpp`），AST 的 `ArrayTypeLoad` 只带 `dims_` 计数，格式化器逐维打印 `[]`（P3）。
4. `Tensor` 是 `OtherType`；`constas` 在形状位置仍被接受；`-1` 仍表示未知（P3/P4）。
5. 打印有两套格式（`Array::print` 的嵌套与 `modules/tensor` 的 `tensor(...)`）；入口参数按 `TypeCode` 找加载器，数组没有第二套类型码（P4）。
6. `map`/`filter`/`sort` 对 rank-n 的含义未实现；四个后端各自有专用的 `map_arr` 循环（P5）。
7. 原地写的算子面窄；没有"每个返回数组的算子都有原地形式或列为例外"的结构性检查（P6）。
8. 没有张量程序的 A/B 基线；`test/tools/perfbaseline/baseline.py` 的 `PROGRAMS` 里没有张量项（P7）。

---

## 2. P1：第 1 项收口

### 2.1 为什么几乎不用改代码

第 1 项的实现已经完整：值 = `Array` + `Block`；视图共享块与计数；`refCount_` 原子；DLPack 进出都零拷贝（`src/core/array/dlpack.cpp` 的 `toDLPack` 保留块并登记 `releaseExport`，`fromDLPack` 用 `Block::adoptExternal` + `Array::wrap` 接管外部内存）；GC 记账已接进 `gen.cpp`。P1 只补**证明**，不改机制。

### 2.2 三个证明缺口与对应的测试

`src/core/array/block_selftest.cpp`（164 行，`BLOCK_SELFTEST`）今天证明：内联 `int[3]`、视图提升与共享、`int[100]` 走块、DLPack 导出同址、导入同址、c64/c128 往返。缺三条：

1. **外部释放回调恰好一次**：用一块静态缓冲区 + 一个计数回调构造 `Block::adoptExternal`，`Array::wrap` 两次（两个视图），依次丢弃全部值，断言回调计数 = 1、`blockLiveCount()` 回到基线。这一条锁住 `releaseFromFinalizer` 的推迟路径与计数到 0 的时序。
2. **外部块永不原地写**：`canWriteInPlace()` 在外部块上为 false（即使计数为 1）；对一个外部数组调用一个原地形式的算子（例如 `sort`），断言返回的是新块、外部缓冲区字节不变。设计原则 11.3：另一个框架不能看到我们的修改。
3. **大数组循环的峰值有界**：在用例里循环 `reps` 次创建 `f32[1 << 20]`（4 MB，必然走块），每次丢弃；断言 `blocks().live - 基线` 有上界（例如 ≤ 2）且 `frees > 0`，即 GC 确实被块字节触发过。这是批复第 2 条的验收，也覆盖紧凑存储的元素视图不泄漏。

新增用例放 `test/cases/feat/mm/external_block.cml`（或直接扩 `blocks.cml`）与 `test/cases/feat/mm/block_pressure.cml`；计划 `test/plans/feat/mm/blocks.plan.toml` 加一行 `BLOCK_SELFTEST ok` 与两组计数断言。`gc_poison_sweep` 用例加入一个 rank-2 视图与一个外部块视图，确保毒化清除后没有悬挂读。

### 2.3 P1 的文件与门禁

改：`src/core/array/block_selftest.cpp`（加三个自检函数）、`test/cases/feat/mm/*.cml`、`test/plans/feat/mm/*.plan.toml`、`src/core/array/dlpack.cpp` 只在必要时加断言（预期不动）。

门禁：`node scripts/test.js test/plans/feat/mm`、`test/plans/feat/modules/tensor.plan.toml`、`smoke`。P1 可先合并。

---

## 3. P2：核心数组类型的维度

### 3.1 类型对象

`ArrayType` 增加：

```cpp
// 前缀维度 + 可选的尾部包（"..s"）；anyRank_ 表示尾部长度不固定
std::vector<ExtentForm> extents_;   // 已知的前缀维度，规范形式
bool anyRank_ = false;              // 尾部打开：T[3, ..s]、T[..]
std::string packName_;              // 包的名称；"" 表示匿名
bool numericUnknown_ = false;       // Tensor 家族：元素是数值，但类型未定
```

`numericUnknown_` 是为 `Tensor` 保留的（第 5.1 节）：一个元素未定、只接受数值标量的数组类型。它不是一个新 `TypeCode`，仍是 `TypeCode::Array`，`elemType()` 返回 `Void`；`arrayStorageFor` 仍判为 Slots（静态类型不创建值），运行时 `elemTypeCode_` 是真实数值类型。

工厂与不变量：

```cpp
static const ArrayType *create(const Type *elem);                       // T[] = T[?]，今天的含义不变
static const ArrayType *create(const Type *elem, std::span<const int64_t> extents);
static const ArrayType *create(const Type *elem, std::span<const ExtentForm> extents,
                               std::string_view pack = {});             // pack 非空 ⇒ anyRank
static const ArrayType *numericUnknown(std::span<const int64_t> extents,
                                       std::string_view pack = {});      // Tensor 家族
```

不变量：`T[]` 与 `T[?]` 是同一个对象；`T[m, n]` 是 rank 2，`T[m][n]` 是"数组的数组"（外层元素类型是 `T[m]`，不是 rank 2）；包只在末尾出现一次（语法层保证）；`extents_` 为空且 `anyRank_` 为假**不存在**（那就是 `T[]`，规范为 `{?}`）。

### 3.2 驻留与规范化

驻留键从"元素指针一个词"变成四个词：`{elem, flags, extentsPtr, packPtr}`。

- `extentsPtr` 指向一张**规范维度表**：`TypeTable` 新增 `internExtents(std::span<const ExtentForm>)`，键是 `ExtentForm::toString()` 的规范化文本（项已排序），同一形状只有一份，进程生命周期。这样 `ArrayType` 的相等比较只看指针。
- `flags` 位：`anyRank`、`numericUnknown`。
- `packPtr` 指向驻留的包名（空串用 0）。包名进键，因为打印与诊断要保留它；`equals` **忽略**包名与维度名，只比较结构（与今天 `TensorType` 的 `equals` 一致）。
- `mangle`：`Array_<elem>_<rank或"any">_<规范形状文本>`，名字不进 mangle；`toString` 打印 `T[...]`（见 3.3）。

### 3.3 相等、可赋值、转换、统一、加宽

| 函数 | 规则 |
|---|---|
| `equals` | 两边都是数组：元素 `equals`、`anyRank` 相同、`numericUnknown` 相同、前缀维度逐个 `ExtentForm::operator==`（名字不比）。`T[]` 与 `T[?]` 相等。 |
| `assignableFrom(target, source)` | 元素：target 是 `numericUnknown` 时 source 元素必须是数值标量；target 是 Void 时接受任意；否则 `target.elem->assignableFrom(source.elem)`。维度：target 固定秩 ⇒ source 秩相同且每个已知维度相等，未知维度接受任意；target `anyRank` ⇒ source 秩 ≥ 前缀长度且前缀相等；source 的包不参与。 |
| `castSafetyFrom` | 元素可赋值且维度形状兼容、但不相等 ⇒ `Unsafe`（运行时检查真实形状），否则 `Safe`/`Forbidden`。`castSlotFrom` 做检查：`array->rank()/dim(i)` 与类型逐项比，不匹配抛运行时错误（沿用今天的 `RuntimeError` 文本格式）。 |
| `unify` | 两边都是数组：秩不同或包不同 ⇒ 保留公共元素（同 dtype 保留；都是数值但 dtype 不同 ⇒ `numericUnknown`），秩任意（今天的"任一维度"）；秩相同 ⇒ 逐维：两边已知且相等保留，否则 `?`；元素同上。**丢名字**。 |
| `widened` | 同秩、所有维度变 `?`，保留元素与 `numericUnknown`。这样 `var xs = [1, 2]; xs->append(3)` 仍是 `int[]`（与今天 `TensorType::widened` 的"任意秩"不同，见问题 Q5）。 |
| `clone` | 保留维度与包。 |
| `resolve` | 数组字面量的元素细化：保留 `[?]`；`int[][]` 仍是嵌套。 |

`ArrayType` 不再只有 `elemType_`；`elemTypeCode_` 取自元素。

### 3.4 形状检查搬进核心

从 `modules/tensor/type.cpp` 搬过来的具体函数：`equals`、`unify`、`widened`、`castSafetyFrom`/`castSlotFrom`、`extentForms`、`withExtentValues` 与打印。**形状参数的解析**在 P4 从 `TensorType::cloneWithArguments` 移到核心 `ArrayType::applyArguments`（P2 先把接口放到 `Type` 上）：`Tensor` 模块不再解析 `Tensor<f32, [n, m]>`，只导出一个构造器类型；在 P4 之前 `TensorType` 仍解析，P3 的 `Tensor<f32, [n, m]>` 因此照常工作。搬完后 `TensorType` 只剩"导出名字"的壳，P4 删除。

同时把 `OtherType` 上的两个虚函数上移到 `Type`（并把 `TypeArgument` 用起来）：

```cpp
// include/camel/core/type/base.h
virtual bool extentForms(std::vector<ExtentForm> &out) const;      // 默认 false：不接受维度参数
virtual std::string_view packName() const;                          // 默认空
virtual const Type *withExtentValues(const ExtentValues &values,
                                     const ExtentPacks &packs) const; // 默认 this
virtual const Type *applyArguments(std::span<const TypeArgument> args,
                                   std::string &error) const;          // 默认报"不接受参数"
```

`ExtentPacks = std::map<std::string, std::vector<ExtentForm>>` 加在 `extent.h`。`OtherType` 的 `extentForms/withExtentValues` 变成空实现（`modules/tensor` 先继续用自己的 override，P4 删除）。`TypeArgument` 已经在 `include/camel/core/type/other.h`，不动。

调用点只有两处 GIR 与一处 GCT：`src/compile/gir/builder_operators.cpp`（调用点绑定、返回类型细化）、`src/compile/gct/builder.cpp`（`isOtherType` 分支）。全部改成走 `Type` 虚函数，不再 `dynamic_cast<const OtherType*>`。

### 3.5 调用点的绑定与替换

`builder_operators.cpp` 现有逻辑保留，只换接口：

1. 取形参类型的 `extentForms(forms)` 与 `packName()`。形式全为未知（例如 `T[]` 的 `[?]`）⇒ 不绑定任何名字，细化保持原类型。
2. 取实参类型的 `extentForms(actual)` 与 `packName()`；实参类型不是数组 ⇒ 不匹配，报现有的 `DimensionMismatch`。
3. 前缀逐项绑定：形参有名字、实参是常数（`ExtentForm::isConstant()`）时 `bind`；实参是名字或未知时不绑定（运行时的 cast 补检查）。
4. 包：`rest = actual[prefix..]`；实参自己带包时尾部未知，不绑定包（保留声明，运行时由 cast 检查）。
5. `paramRefined = param->withExtentValues(values, packs)`；`refined->assignableFrom(actual)` 通过就替换，否则按今天的规则 `castSafetyFrom` + `castNodeTo`；形状不符在运行时由 `castSlotFrom` 报错。
6. 返回类型：`ret->withExtentValues(values, packs)`。若某个维度形式有未绑定的名字（只出现在返回类型），GCT 已经报 `DimensionNameNotBound`（第 4 节），不会走到这里。

`withExtentValues` 的特例：前缀为空、尾部包绑定为空 ⇒ 返回**元素类型**（标量）。这正是 `first_row(xs: T[n, ..rest]): T[..rest]` 对 `f32[3]` 的返回类型。参数位置不会出现这种情况（数组参数至少一个维度），若出现则细化成标量、与实参不匹配，按普通类型错误处理。

### 3.6 兼容承诺

- `ArrayType::create(elem)` 仍是 `T[]`，所有今天的调用点不改；`int[]` 打印仍是 `int[]`（`[?]` 打印为空括号）。
- `T[]` 与 `T[?]` 是同一对象；等于、可赋值、`as` 全部与今天相同。
- `size()` 第一维、`numel()` 总数、`strides_` 行主序不变。
- 张量类型仍是 `OtherType`，行为不变；P2 不与 `TensorType` 抢任何调用点。

### 3.7 P2 的文件与门禁

改：`include/camel/core/type/composite/array.h`、`src/core/type/composite/array.cpp`、`include/camel/core/type/base.h`、`src/core/type/base.cpp`、`include/camel/core/type/extent.h`、`src/core/type/text.cpp`、`src/core/type/intern.{h,cpp}`、`src/core/type/other.h`、`src/compile/{gct,gir}/builder*.cpp`、`src/core/type/composite/array.cpp` 的 `resolve/clone`。

新增自检：`src/core/type/array_type_selftest.cpp`（或并入 `test/plans/feat/compile/type_*.cml` 的用例）：`T[]` 驻留唯一、`T[3]` ≠ `T[?]`、名字不进 equals、mangle 稳定、`unify`/`widened` 表驱动、`withExtentValues` 的空包回退。门禁：`smoke`、`feat/compile`、`feat/mm`、`feat/modules/tensor`。P2 合并时行为不变，可以先落地。

---

## 4. P3：语法、AST 与维度绑定

### 4.1 语法

```
arrayType   : specType arrayDim+
arrayDim    : '[' ']'                          // 一个未知维度，T[] = T[?]
            | '[' extent (',' extent)* ']'
extent      : '?'                              // 未知
            | '..' ident?                      // 尾部包：..rest 或 ..
            | linearExpr                       // 只允许 + - * 常数 / 整数
```

`specType` 的类型参数列表同样接受 `[extent, ...]`（去掉 `const`）：

```
Tensor<f32, [n, 784]>     // 长写法，等价于 f32[n, 784]
Tensor<[3]>               // 元素未定的数值数组，形状 3
Tensor<f32>               // f32[..]
```

规则：

- `T[][]` 与今天的含义相同（嵌套数组），**不是** rank 2；rank 2 写作 `T[m, n]`。
- 一个数组类型里 `..` 最多一次，且必须最后（`DimensionAfterAnyRank`）；`T[.., 3]` 是错误。
- 裸 `..` 匿名（打印 `T[3, ..]`）；`..s` 命名，名字只活在类型里。
- `?` 是未知维度，`T[?]` 就是 `T[]`；`2 * n - 1` 这类线性形式保留名字。
- 元素类型可以是类型变量：函数头声明 `T` 后写 `T[n, m]`（`typeUnit` 已有 `identDef 'of'` 前缀）。
- 形状参数里写 `const`（`Tensor<f32, const [3]>`）报 `InvalidTypeArgument`，消息给出"去掉 `const`"的写法；不新增诊断码（批复 Q2 的同类处理）。

### 4.2 AST、CST 与格式化

- `node_kinds.def:113` 的注释更新为 `ArrayType // specType arrayDim+`；新增 `ArrayDim // '[' (extent (',' extent)*)? ']' | '[' '..' ident? ']'`。
- `src/parse/syntax/parser_type.cpp` 的 `parseArrayType`（158–206）改为循环解析 `ArrayDim`；`parseSpecType`（208+）的 `typeOrData` 增加"以 `[` 开头的形状参数"分支（解析成 `ArrayData`，里面的项是 extent 表达式），`const` 分支保留但只对普通类型参数有效。
- AST：`include/camel/parse/ast/type.h` 的 `ArrayTypeLoad` 从 `dims_`（计数）改为 `std::vector<DimGroup>`：每组是 `std::vector<node_ptr_t>` 的 extent 表达式 + `pack`（无/匿名/名字）。`geneCode`（`src/parse/ast/type.cpp#ArrayTypeLoad`）按源文本打印，`[]` 仍打印为空括号，`?` 打印 `?`，包打印 `..name`。
- CST→AST：`src/parse/ast/builder.cpp#visitArrayType` 每遇到一个 `ArrayDim` 建一组；`visitArrayType`（`src/compile/gct/builder.cpp#visitArrayType`）对每组建一个 `ArrayType`：`[]` → `[?]`，`[..s]` → `anyRank` + 包名，`[n, m]` → 两个形式。**一组 `[]` 仍是一个嵌套层**，与今天一致。
- 格式化器：`tools/format/fmt.cpp#Formatter` 的 `visitArrayType` 逐组打印 `[?]/[..s]/[n, 3]`；`visitSpecType` 打印形状参数不带 `const`。格式化幂等测试（`feat/format`）覆盖新语法。

### 4.3 绑定规则

在一个函数签名的**参数类型**里（`dimBindAllowed_` 只在 `visitFuncType` 访问形参时置位，`src/compile/gct/builder.cpp#dimBindAllowed_`）：

1. 一个普通名字 `n` 第一次出现 ⇒ 绑定为维度，产生 `ExtentForm::linear({{n, 1}})`；
2. 同一签名里再出现 `n` ⇒ 引用已绑定的维度（`T[m, m]` 表示方阵）；
3. 名字只在参数里出现一次 ⇒ 在签名结束时发**警告** `DimensionNameUsedOnce`（建议写 `?`；出现在返回类型里的名字至少出现两次，不会触发）；包名不受这条限制；
4. 名字只出现在返回类型、参数里没有 ⇒ 错误 `DimensionNameNotBound`（今天已有）；
5. 参数里的名字同时是一个可编译期求值的常量（`const IN = 784; f32[b, IN]`）⇒ 按常量维度，不绑定名字（设计 5.4 的例子）；
6. `?` ⇒ `ExtentForm::unknown()`，不绑定；
7. `..s` ⇒ 绑定包名 `s`（参数里可再次引用 `..s`）；`..` 匿名；包名与维度名分属两个命名空间，重名是错误；
8. 线性形式只从名字与整数常数构造：`n`、`n + 1`、`2 * n`、`n - 2` 保留；名字乘名字、除法、函数调用 ⇒ `ExtentForm::unknown()`（现有 `visitExtent` 4173–4252 的规则，保留）。

删除 `collectDimensionNames`（3697–3720 的 `constas` 预扫描）：绑定按源顺序发生，不再需要预示。删掉之后，`constas` 在形状位置不再被识别 ⇒ 普通语法错误（批复 Q2：不加 `ConstasRetired`）。`ConstAsLoad` 节点与 `constas` 关键字本身留给后续工作。

**名字与值作用域**：维度名只在类型里，函数体里的同名变量不冲突；反过来，一个已绑定的维度名不能在同一签名里再当作常量求值。这是设计 5.4 "维度名只活在类型里"的直接结果。

### 4.4 诊断

| 诊断 | 何时 | 今天 |
|---|---|---|
| `DimensionMismatch` | 秩/已知维度不符 | 有（GCT） |
| `DimensionNameNotBound` | 名字只在返回类型或拼错 | 有 |
| `DimensionNameUsedOnce` | 名字只出现一次（警告） | **新增** |
| `DimensionAfterAnyRank` | `..` 不在最后或出现两次 | **新增** |
| `InvalidExtentExpression` | 非整数、名字乘名字等 | 有（未知维度不报错，沿用） |
| `InvalidTypeArgument` | `const` 形状、`Tensor<string>` | 有 |

`DimensionNameAlreadyBound`、`ConstasNotAllowed` 保留定义但不再从形状路径发出（统一处理）。`docs/diagnostics.md` 由脚本重新生成。

### 4.5 标准库与文档

- `stdlib/signatures/globals.cml`：`map`/`filter`/`foreach`/`reduce` 的声明从 `T[]` 改为 `T[..]` 并在注释里写第一维规则（实际类型规则在 `globals.cpp`，见 P5）；`append`/`extend`/`slice`/`concat` 仍是 `T[]`（只对 rank 1 有意义）；`len` 从 `T[]` 改 `T[..]`。
- `stdlib/array.cml`：`sort` 家族与 `argsort` 改 `T[..]`；`sort_by` 等 `stdlib/array.cml` 的 Camel 实现保持 rank 1（下标切片落地后再泛化）。
- `docs/language/08_top_level_and_grammar.md` 与 `en/`（76–77、201–202 行）的 `arrayType` 产生式、`constas` 说明；`docs/language/03_data_model.md` 的类型表加 `T[dims]`、`?`、`..s`、`Tensor` 别名。
- `docs/stdlib-reference.md` 由 `scripts/stdlib-reference.mjs` 重新生成。

### 4.6 测试重写（机械，同车道）

- 87 个用例文件含 `const [`；逐个按 `Tensor<T, const [d]>` → `Tensor<T, [d]>` 改写；其中 20 个含 `-1`，逐个改 `?`（`Tensor<f32, const [-1, 3]>` → `Tensor<f32, [?, 3]>`）。
- 12 个用例含 `constas`：形状位置改写为首次出现的名字，其余引用；例如 `f<T>(x: T[constas n, 4])` → `f<T>(x: T[n, 4])`。
- `test/cases/modules/tensor/static_types.cml`：`var w = zeros([2, 1]); w = grow(w) (Tensor any)` 依赖"任意秩加宽"；按 Q5 的结论（同秩加宽）改写成两次 `var` 或显式 `as`。另有一处与加宽无关：`grow(t: Tensor): Tensor` 的裸 `Tensor` 签名返回过渡类型，进不了元素类型已定的格子（`f32[?, ?]`）；改写签名为具体类型（`grow(t: f32[?, ?]): f32[?, ?]`）。
- `test/cases/tools/grammar/keywords.cml` 的 `Tensor<f32, const [constas n, 4]>` 改写为 `Tensor<f32, [n, 4]>`，并重新生成 `test/plans/feat/tools/golden/grammar/keywords.scopes` 金标。
- 新增 `test/cases/compile/array_shape/*`：语法、绑定、`?`、`..s`、线性、常量名、诊断、`T[]` 不变、`T[][]` 嵌套、`as` 检查；`test/plans/feat/compile/array_shape.plan.toml`。
- AST/格式化/金样：新增语法进入 `feat/format` 的往返语料；`feat/compile` 的 AST 快照只对 smoke 语料，不改既有快照。

### 4.7 P3 的文件与门禁

改：`include/camel/parse/syntax/node_kinds.def`、`include/camel/parse/ast/type.h`、`src/parse/syntax/parser_type.cpp`、`src/parse/ast/type.cpp`、`src/parse/ast/builder.cpp`、`src/compile/gct/builder.{h,cpp}`、`tools/format/fmt.cpp`、`include/camel/core/type/extent.h`、诊断定义与文档、标准库签名、上列用例。

门禁：`feat/parse`、`feat/format`、`feat/compile`、`feat/compile/array_shape`、`feat/modules/tensor`、`smoke`、全部张量用例（改写的 87 个）。P3 合并后合法程序行为不变（只有旧拼写被改写）。

---

## 5. P4：`Tensor` 别名（最险）

### 5.1 别名是什么

`Tensor` 不再是一个 `OtherType`，而是**一个由张量模块导出的 `ArrayType` 构造器对象**：

- 导出：`modules/tensor/module.cpp#Reference` 从 `exportType(Reference("Tensor"), TensorType::Default())` 改为 `exportType(Reference("Tensor"), ArrayType::numericUnknown({}))` —— 元素是"数值、类型未定"，任意秩。`elemType()` 是 `Void`，`numericUnknown_` 为真。
- 应用：`ArrayType::applyArguments`（P2 加的 `Type` 虚函数）在 `numericUnknown_` 为真时解释参数：
  - 无参数 ⇒ 自身（`Tensor`，任意秩）；
  - 一个类型参数 `E` ⇒ 要求 `E` 是数值标量（`i32/i64/f32/f64/bool/byte/c64/c128`），否则 `InvalidTypeArgument`（`Tensor<string>` 是错误）；带形状参数 ⇒ `E[extents]`，否则 `E[..]`（任意秩）；
  - 只有形状参数 ⇒ `numericUnknown(extents)`，打印 `Tensor<[3]>`；
  - 形状参数 `[]` 按 Q8 的结论处理（建议不支持秩 0，报错并建议写 `[1]`），非空列表按 4.1 的 extent 规则。
- `Tensor<f32>` 与 `f32[..]` 是**同一个驻留对象**（应用后不再有 `numericUnknown_`）；`Tensor<f32, [n, 784]>` 与 `f32[n, 784]` 同一个对象。两者相等、互相可赋值。
- 形状参数的两种来源：常量列表 `[3, 4]` 由 GCT 作为 `TypeArgument.data`（`ArrayData`）传入；含名字的列表（`[n, 4]`）作为 `TypeArgument.extents`（`ExtentForm` 列表）传入。`applyArguments` 两者都接受。
- 裸 `Tensor` 的打印是 `Tensor`（源码可解析）；`numericUnknown[3]` 打印 `Tensor<[3]>`。
- 不带类型参数的 `Tensor<>` 视同 `Tensor`。

这样"别名名副其实"（批复 Q1/Q3）：`Tensor` 系列的名字只是 `T[dims]` 的另一种拼写，类型系统里没有第二套张量类型。

### 5.1a 过渡状态与删除步（决策者 2026-10-25 照录 tensor-alias-transition 的建议）

`Tensor` 作为 `ArrayType::numericUnknown()` 的导出、以及由此产生的"数值、
元素类型未定的数组"是**迁移中的过渡**，不是语言保留的类型（设计原则 5.5：图上只有具体类型，运行时
不需要类型信息；泛型要在头部用 `::<T>` 声明，裸 `Tensor` 形参等于隐式泛型，已否决）。

P4 的终点：
- 签名里的裸 `Tensor` 是**编译错误**，诊断提示写 `Tensor<T>` 并在头部声明 `T`，或写具体元素类型
  （`f32` 等）；
- 标准库的张量运算改为 `func op::<T>(x: Tensor<T>, …) when is_float::<T>()` 一类写法（按
  按 `log_softmax` 现行的写法）；
- **值的元素类型总在类型里**；形状可以是 `?`（大小运行时才知道是值的事，不是类型的事）。

删除步（P4 合入 develop 之前完成，负责人 2026-10-25：不留过渡、不留技术债。过渡状态只能存在于翻转分支上，develop 上不出现别名 `Tensor`、`numericUnknown` 或本节依赖表里的任何一处）：
1. `ArrayType::numericUnknown*` 与其标记 `numericUnknown_` 删除；`Tensor` 的 `Reference` 改为
   `Tensor<T>` 形式（元素类型参数必填，`Tensor<..>` 的 `applyArguments` 只接受类型参数 + 形状参数）；
2. 签名里出现裸 `Tensor` 的诊断（`InvalidTypeArgument` 或新的语法诊断）；
3. 本页第二节列出的每一处过渡依赖同时删除；
4. `test/cases/compile/array_alias/bare_tensor.cml` 与
   `test/plans/feat/compile/array_alias.plan.toml` 的 `alias.bare_tensor` 一条改写成"裸 `Tensor` 形参
   是编译错误"的负例；`test/cases/loops/iterate_tensor.cml` 的 `countRows(t: Tensor)` 同理改写成
   `countRows::<T>(t: Tensor<T>)` 或删掉。

#### 依赖过渡类型的位置（删除步一并处理）

| 位置 | 依赖什么 | 删除步怎么做 |
|---|---|---|
| `modules/tensor/type.cpp#tensorFactsOf` 的 `isNumericUnknown()` 分支 | 读过渡类型的事实（无元素类型） | 该分支随标记一起删；事实只剩"数值数组"一种 |
| `modules/tensor/ops/support.cpp#tensorOf(facts)` 的 `!facts.dtype` 两支 | 造过渡类型（`numericUnknown` / `numericUnknownFromForms`） | 元素类型必有时删掉，改成断言 |
| `modules/tensor/ops/vjp.cpp#vjpTensorType()` | VJP 结果类型是过渡类型（`ArrayType::numericUnknown()`） | 改为由被微分值的切线类型给出具体元素类型 |
| `src/core/derivative.h/.cpp#setTangentType`/`statedTangentType` | 过渡类型的元素是 `Void`，结构上取不到切线，所以新加一个注册钩子 | 随过渡类型删除；`computeTangentType` 的 `Array` 分支恢复只走元素类型 |
| `modules/autodiff/pullback.cpp#carriesGradient` 的 `Array` 分支 | 同上（`tangentTypeOf` 判据） | 可恢复为 `carriesGradient(elemType)` |
| `modules/tensor/module.cpp` 的 `.npy` 加载器/保存器 | 检查值对着 `ArrayType::numericUnknown()` 做形状/元素检查 | 改为对着声明的具体数组类型检查 |
| `modules/tensor/ops/gradient_ops.cpp#registerTensorTangentSpace` | 判断过渡类型有没有切线 | 随钩子删除：切线类型由元素类型给出 |
| `modules/onnx/emitter.cpp` 的 `Value::constant(..., numericUnknown())` | 数组常量当张量时的声明类型 | 改为值自己的数组类型 |
| `src/core/rtdata/equality.cpp` 的 `registerContentComparableType` / `isRegisteredContentComparableType`，以及 `isContentComparableImpl` 里的 `isNumericUnknown()` 分支 | 让模块的 `Tensor` 类型与元素类型未定的数组参加整体比较 `==` | 随过渡类型删除：张量就是数组，走数组自己的比较；注册表整个删掉 |
| `modules/tensor/ops/layout_ops.cpp` 的 `OpDef idx_take`（`.exports = {"__idx__"}`）与 `setVjp(defs, "idx_take", &takeVjp)` | 模块的 `Tensor` 值经它参加 `t[idx]`；核心 `:op/idx_take` 已对任意秩按第 0 维取行、带反向 | 删掉这份副本，`t[idx]` 只有核心一条规则；函数形式 `tensor::take` 保留；`export_matrix` 去掉 `tensor:idx_take` 一行；`take_rows`、`gather_grad_f64`、`discrete_zero`、`gather_discrete_index_zero` 四个用例的 `t[idx]` 落到核心 |
| `stdlib/rules.cml` 的 `tensor_f32_*`、`tensor_i64_*` 一族规则（约 13 条） | 张量是单独的类型，标量的代数律管不到它，于是每条律再抄一份 `Tensor<f32>` 版本 | 删掉这一族；标量那条律推广到数值数组（逐元素成立，元素与数组共用同一条律，写法由规则语言那一版定），同一条代数律只写一次 |
| 切片：张量模块的 `tensor:slice`（含 `__slice__` 导出）与 `globals.cpp` 里按"是不是程序自己的数组"分派下标的 `isTensorSubscript` | 模块的 `Tensor` 值经它参加 `t[a:b]`，核心只答 `T[]` | 切片只有核心一个算子、一条伴随规则（`t[a:b]` 与 `slice(t, 0, a, b)` 同一个节点，对任何秩、任何元素类型），模块副本与按类型分派的那段一起删掉；伴随交回的值带写明的元素类型与形状 |
| `src/compile/gir/builder_operators.cpp` 的 `var` 实参接管（普通实参与 `with` 实参各一处） | 形参的裸 `Tensor` 与实参值的数组拼法不是同一个类型，cell 的"内容类型必须相等"（`src/core/type/composite/tuple.cpp`）会拒 | 随标记删除：两侧是同一个类型，接管分支删掉（详见下一节） |

**没有让新的消费者依赖它**：所有从 `TensorType` 迁过来的构造点走 `tensor::ops::tensorOf(...)`，元素类型
来自事实（`facts.dtype`）；过渡类型只在"事实里没有元素类型"时出现，即 `Tensor` 的两种拼写本身。

`var` 形参写裸 `Tensor` 时，调用点的 cell 按声明的形参类型接管（h419 的那一段）是过渡依赖，列在上表；P4 把这类形参改写成显式泛型（`var w: T[..]`）后两侧同型，接管分支删除。cell"内容类型相等"的不变式不放宽。

### 5.2 元素类型精确

- 删除 `normalizeTensorDType`（P4 已删；`TensorType::get` 只校验元素类型，`modules/tensor/type.cpp` 起）：`Tensor<float>` 就是 `float[..]`（f64 存储），`Tensor<i32>` 就是 `i32[..]`，`Tensor<f64>` 就是 `f64[..]`。`zeros`/`ones` 等不写 dtype 的构造仍是 f32（与今天一致）。
- 审计模块内核的 dtype 支持：elementwise 已支持 f32/f64/i32/i64（`modules/tensor/kernels/elementwise.cpp`）；conv/bn 的若干内核只接受 `const float*`（`kernels/conv.cpp` 283+、404+、572+）。这些算子的推断处要声明只支持 f32（今天没有声明，遇到 f64 会读错），遇到 f64 输入报运行时故障（而不是静默读错）。测试里没有 `Tensor<float>` 的用例（已核对），只有 `Tensor<int>` 出现在 `modules/nn` 的 checkpoint 用例里，`int` 就是 i64，与今天相同。
- 删除 `TensorType::castSafetyFrom/castSlotFrom` 里的 dtype 归一化；`as` 的检查变成 `ArrayType::castSlotFrom` 的形状检查（P2）。

### 5.3 删除 `TensorType` 的迁移表

| 今天 | 之后 | 备注 |
|---|---|---|
| `TensorType::get(dtype, shape, forms)` | `ArrayType::create(elem, forms)` / `create(elem, extents)` | 形状来自 `StaticShape` 的转换 |
| `TensorType::Default()` | `ArrayType::numericUnknown({})` | 裸 `Tensor` |
| `asTensorType(type)` | `asArrayType(type)` + `isNumericArrayType(type)`（核心新增谓词：元素是数值标量，或 `numericUnknown_`） | 25 个文件、51 处 |
| `TensorType::dtype()`/`shape()` | `ArrayType::elemTypeCode()`/`extents()` | 运行时仍用 `Array::dtype()/shape()` |
| `TensorType::extentForms/withExtentValues` | 核心 `ArrayType` 的同名函数（P2） | 删除 override |
| `TensorType::cloneWithArguments` | `ArrayType::applyArguments` | 解析 `Tensor<...>` |
| `TensorFacts{dtype, shape}` | `ArrayFacts{TypeCode elem, std::vector<std::optional<int64_t>> extents}` | 模块内部中间量 |
| `kUnknownDim = -1` | `std::nullopt` | 测试里逐处改 `?` |
| `TensorType::typeCode()` 的注册 | `TypeCode::Array` + 谓词 | 入口参数、细化器 |
| `modules/tensor/module.cpp` 的值类型细化器 | 用 `isNumericArrayType` 判常量数组，返回 `ArrayType` | 形状来自运行时常量 |
| `.npy` 入口加载器 | 注册在 `TypeCode::Array` 上，先 `isNumericArrayType` 且秩已知才接，否则拒绝 | 见 5.5 |

逐模块：

- `modules/tensor`（`type.cpp` 整文件删除，约 439 行；`module.cpp`、`ops/*`、`support.cpp`、`interop.cpp`、`npy.cpp`、`binary.cpp`、`passes/index_eval.cpp`、`tensor.cpp` 的类型构造点）；
- `modules/nn`：checkpoint 的类型文本（`Tensor<f32>` 打印为 `f32[..]`）、`load_tree::<Tensor<int>>`、参数类型；
- `modules/onnx`：`emitter.cpp`/`exporter.cpp`/`lowering.cpp` 用 `asTensorType` 与 `normalizeTensorDType` 判断 dtype/形状的地方；
- `modules/tiles`：张量识别与 `Tensor` 拼写；
- `modules/ckpt`、`modules/autodiff`：签名与算子里的 `Tensor` 类型；
- `src/execute/entry_args.cpp`：加载器键与"能拒接"的谓词；
- `src/builtin/modules/globals.cpp`：`std::mem::blocks` 之外的张量相关声明不受影响。

### 5.4 打印统一

- `modules/tensor` 的 `tensor(...)` 打印删除（`printTensor`），全部走 `Array::print`：rank 1 是 `[1, 2, 3]`，rank 2 是嵌套括号 `[[1, 2], [3, 4]]`（已有的紧凑聚合打印），元素用运行时的 `elemType_` 而不是声明类型的 `Void`（`Array::print` 现在从 `arrayType->elemType()` 取元素类型，`Tensor` 家族要改为 `elemType_` 优先）。
- 影响面：`test/plans` 里 10 个计划、72 行含 `tensor(` 的期望输出；11 个用例文件。逐行改为嵌套括号。这是 P4 里最机械也最容易漏的部分，按 `grep -l 'tensor('` 清单逐个更新，并由 `feat/modules/tensor` 全绿背书。
- 类型文本：`f32[]`、`f32[3, 4]`、`f32[..]`、`Tensor`、`Tensor<[3]>`。`docs/stdlib-reference.md` 与诊断文本随之更新。

### 5.5 入口参数与 `.npy`

`EntryArgumentLoader` 今天按 `TypeCode` 精确查表（`src/execute/entry_args.cpp#EntryArgumentLoader/350`），且先试聚合类型。数组与张量共用 `TypeCode::Array` 之后：

- 张量模块在 `TypeCode::Array` 上注册 `.npy` 加载器，回调先判"这是不是数值数组、秩是否已知"；不是就返回"不接"（批复 Q4 明确要求加载器能拒接）——`Type[string]` 的入口参数仍按原来的"不能从命令行给出"报错。
- 返回值的静态类型是**声明的数组类型**（例如 `f32[2, 3]`），不是裸 `Tensor`；`.npy` 的真实 dtype/形状必须与声明相符，否则报入口参数错误。`from_bytes` 的声明保持 `Tensor`（任意数值数组）。
- `.npy` 保存器同样注册在 `TypeCode::Array` 上；非数值数组拒绝。
- `modules/tensor/module.cpp` 的 `.npy` 入口键从 `TensorType::typeCode()` 改为 `TypeCode::Array`；这是本项最容易被其他模块的数组参数误接的地方，测试覆盖"`int[]` 入口参数 + 张量模块已加载"与"`string[]` 入口参数"两种。

### 5.6 测试重写

- 464 处 `Tensor<`、87 个文件：P3 已把 `const [` 改写为 `[`；P4 再把仍然依赖"张量类型语义"的断言改写（打印、`as`、`var` 加宽、类型文本）。这一步与 P3 的改写分开提交，便于二分。
- `test/cases/modules/tensor/static_types.cml`、`var_tensor_reference.cml`、`shape_inference/var_argument*.cml`：`var` 加宽与裸 `Tensor` 的用法按 Q5 调整。
- 签名里写裸 `Tensor` 的示例与用例（`test/cases/modules/nn/lrit.cml`、`mnist.cml` 及其 `nn.*` 计划条目等）：P4 终点上裸 `Tensor` 签名是编译错误，按 5.1a 改写成具体元素类型与秩（`var` 形参收的是同秩加宽后的格子类型，写 `f32[?, ?]`；非 `var` 形参可以写定 extent，如 `f32[?, 1]`）或在头部声明的泛型；不放宽可赋值规则，也不把格子写成裸 `Tensor`。
- `grad(f)` 的结果类型（`modules/tensor/ops/vjp.cpp#vjpTensorType`）：由被微分值的切线类型给出，`grad of f32[?, 4]` 就是 `f32[?, 4]`；不再返回过渡类型（`test/cases/feat/mm/gc/gc_training_peak.cml` 的 `sgd(wf, g, 0.5)` 依赖这一条）。
- 新增 `test/cases/compile/array_alias/*`：`Tensor<f32>` ≡ `f32[..]`、`Tensor<f32, [2, 3]>` ≡ `f32[2, 3]`、裸 `Tensor` 接受 `int[3]` 拒绝 `string[3]`、`Tensor<string>` 报错、`Tensor<f32>` 的类型文本、`var` 加宽、`as` 的形状检查、四个后端一致。
- `test/plans/feat/compile/array_alias.plan.toml`。

### 5.7 P4 的文件与门禁

删除：`modules/tensor/type.h`（131 行）、`modules/tensor/type.cpp`（439 行）、`normalizeTensorDType`、`printTensor`、`TensorType` 的所有引用。
修改：本表 + 5.3 的逐模块清单 + 打印期望 + `docs/language` 与 `docs/stdlib-reference.md`。

门禁：`smoke`、`feat` 全层、`feat/modules/{tensor,nn,onnx,tiles,autodiff,tree}`、`feat/perf` 的**正确性**（时间在 P7 测）、`test/cases` 全部张量用例。P4 必须一个提交、一次构建通过；中间不合并。

---

## 6. P5：第一维

### 6.1 语义表

对 rank ≥ 2 的数组，现有数组算子按第一维解释：

| 算子 | rank 1（不变） | rank ≥ 2 | 结果类型 |
|---|---|---|---|
| `map` | 逐元素 | **按行**：`f` 收到一行（rank-1 视图），结果每个元素是 `f` 的返回值 | `U[d0]`（rank 1；`f` 返回数组时是"数组的数组"） |
| `filter` | 保留满足谓词的元素 | 谓词收到**一行**（rank-1 视图），保留整行 | `T[?, d1, ..., dn]` |
| `foreach` | 每个元素 | 每行 | `void` |
| `reduce` | 逐个折叠 | 折叠行 | `f` 的返回类型 |
| `sort` | 按元素 | 按行**字典序**（先比第 0 列，相等再比第 1 列……） | `T[d0, ..., dn]` |
| `sort_by`（`stdlib/array.cml`） | 保持 | 不泛化 | — |
| `argsort` | `int[n]` | 行的字典序排名，`int[d0]` | `int[d0]` |
| `len` | `n` | `d0` | `int` |
| `slice`/下标 | 下标切片 | 下标切片 | — |
| `append`/`extend`/`concat` | 不变 | 类型错误（会改变秩） | — |

`map` 也按第一维：rank 1 的"行"就是元素，所以 `map(xs, f)` 对 `T[]` 与今天完全相同；rank ≥ 2 时 `f` 收到一行，结果是 `d0` 个返回值组成的 rank-1 数组（`U[d0]`；`f` 返回数组时元素类型是数组，即"数组的数组"）。这与任务与设计第 8 节的"按第一维"一致。曾考虑逐元素保形，但那与任务文字不符；而且今天的 `map_arr` 在 rank ≥ 2 上本来就按 `size()` 循环（对紧凑数组会读错元素），没有兼容包袱。`filter`/`foreach`/`reduce`/`sort` 同理走行。

### 6.2 运行时辅助

在 `include/camel/core/rtdata/array.h` 加（实现于 `array.cpp`）：

```cpp
/// 第 i 行的视图：extents = 去掉第一维，元素偏移 = i * stride(0)。仅紧凑数组。
Array *rowView(mm::IAllocator &allocator, uint64_t row) const;   // 新 Array，持有一份块引用
/// 第 i 行复制成独立的槽数组（非纯数据元素时使用）。
Array *rowCopy(mm::IAllocator &allocator, uint64_t row) const;
```

- 行视图是**一等数组值**：可以逃逸、可以存进容器，块计数保证源被释放后仍可读。它与"元素视图不得流进累加器"不是一回事：元素视图是指向聚合体槽内的指针，行视图是独立的 `Array` + 块引用。
- 每次调用 `rowView` 产生一个小 GC 对象；`foreach`/`filter` 在回调是安全点的前提下，用 `mm::RootHandle` 把这一行护住，回调返回后再释放。这是行语义的代价，P7 会记录一行 `tensor.hof` 的耗时，不作为红线。
- 槽数组（`string[m, n]` 等非纯数据）不能取视图：`rowCopy` 复制槽并用写屏障。这类数组上的 `filter`/`foreach` 仍可用，只是每次一行复制；`sort` 只支持数值数组（今天已如此）。

### 6.3 类型规则

在 `src/builtin/modules/globals.cpp` 的 HOF 规则（1890–2050）：

- 由 `arrType->extents()` 得 `rowType`：rank 1 ⇒ 元素类型；rank ≥ 2 ⇒ `ArrayType::create(elem, extents[1..])`（未知维度保留）。
- `map`：`f` 的参数类型 = `rowType`（rank 1 时即元素类型），结果 = `ArrayType::create(f->exitType(), {d0})`；rank 1 时与今天完全相同（`U[n]`）。
- `filter`：`f` 的参数类型 = `rowType`，结果第一维 `?`，其余保留。
- `foreach`：`f` 的参数类型 = `rowType`。
- `reduce`：`f` 的第一个参数是累加器（`init` 的类型），第二个是 `rowType`。
- `sort`：参数与结果同类型；`argsort` 结果 `int[d0]`。
- `len`：`arrType->extents()` 的第一个形式，rank ≥ 1；`T[..]` 的未知维度返回 `int`（运行时是真实长度）。

### 6.4 四个调度器

`src/passes/sched/{nodevm,fastvm,taskflow}.cpp` 与 `src/builtin/operators/higher_order.cpp` 各有一套 `map_arr/filter_arr/reduce_arr/foreach_arr` 专用循环，全部按 `size()` + `getSlot(i)` 写。P5 的改动：

- `map`：循环 `size()`，rank 1 时把元素（`getElementView(flat)` 或 `getSlot(flat)`）传给 `f`，rank ≥ 2 时把 `rowView(i)`/`rowCopy(i)` 传给 `f`；结果 `Array::create(type, d0)`。四个实现同构，抽一个 `mapRows` 辅助（`src/builtin/operators/higher_order.cpp`），三个调度器调用它。
- `filter`/`foreach`/`reduce`：循环 `size()`，传 `rowView(i)`/`rowCopy(i)`；结果按行追加（`appendRow`，见下）。
- `Array::appendRow(const Array *row)`：紧凑数组按 `row->byteSize()` 拷贝并推进第一维（容量不足用 `reusableBlock` 增长）；槽数组逐槽 `setSlot` + 写屏障。
- `filter` 的原地形式（P6）在最后收缩容量/维度（`setExtents({kept, d1...})`）。
- `sort`/`argsort`：`src/builtin/operators/array.cpp` 的 `__doSortSlots__` 已按元素宽度分派；rank ≥ 2 时按 `rowBytes` 交换行，比较用元素字典序。只接受数值元素（`canWriteInPlace` 与 dtype 检查）。`argsort` 的 `int[]` 结果与今天相同（rank 1 时）。
- 四个后端必须一致：`feat/modules/tensor` 的用例在 nvm/fvm/jit 各跑一遍（计划里的 `backends` 字段）。

### 6.5 标准库

`stdlib/signatures/globals.cml` 的 `map/filter/foreach/reduce` 文本在 P3 已改 `T[..]`；P5 补注释与用例。`stdlib/array.cml` 保持 rank 1（`sort_by`/`flat_map`/`zip`/`flatten` 不改）；`stdlib/array.cml` 的 `sort`/`argsort` 已改 `T[..]`。

测试：`test/cases/compile/hof_multidim/*`（map 按行：rank 1 不变、rank 2 得 `U[d0]`、filter 行、foreach 行、reduce 行、sort 字典序、argsort、len、`int[m,n]` 与 `string[m,n]`、空维、1 行、1 列）、`test/plans/feat/compile/hof_multidim.plan.toml`；`feat/mm/hof_block_alloc` 加 rank 2 的分配计数。

### 6.6 P5 的文件与门禁

改：`include/camel/core/rtdata/array.h`、`src/core/rtdata/array.cpp`（`rowView/rowCopy/appendRow/setExtents`）、`src/builtin/modules/globals.cpp`、`src/builtin/operators/higher_order.cpp`、三个调度器、`src/builtin/operators/array.cpp`、标准库签名、用例。

门禁：`feat/compile/hof_multidim`、`feat/mm`、`feat/modules/tensor`、`feat/para`、`smoke`。P5 可与 P6 交换顺序；它们都不依赖对方。

---

## 7. P6：计数为 1 时原地写

### 7.1 判据

一个"返回新数组"的算子可以在调用者写 `xs = xs->op(...)`（或 `xs = op(xs, ...)`）时**原地**完成，当且仅当：

1. **编译器证明旧值不可观察**：`Builder::inPlaceRebindingOf` 只对"把结果赋回同一个变量"的重绑写法改写；这份证明在内存规划里一次定下、随执行计划交给各后端（`ExecPlan::storageReuseOf`）：值由本图所造、且此后没有会留下引用的读（`let y = xs`、传参、放进容器：`:var/get`）时，更新节点标为复用输入自身的存储（`Input`）；证明不了的格子交第二层的计数（`ByArrayCount`）；有观察者时标为拷贝（`Copy`）。只喂给留不下引用的读（`:var/peek`）不结束证明。`LOOP` 的状态在下一轮开始时旧值不可见，按批复第 4 条同样处理（已落地）。
2. **运行时证明存储可写**：`Array::canWriteInPlace()`——紧凑、Camel 自有块、计数为 1、偏移 0、不是视图；内联数组没有块，恒为真。外部块永不原地（设计 11.3）。
3. **算子允许**：结果与操作数 0 的元素类型和形状相同，且内核只做同下标读写（或压缩/重排自身）。

### 7.2 为什么"计数为 1"不够

两个值槽可以指向**同一个 `Array` 对象**（`let ys = xs; xs = xs->sort()`），此时块计数是 2，`canWriteInPlace` 已经为假；但反过来，块计数为 1 也不代表旧值不可见：`map(xs, f)` 里的 `f` 可能读到 `xs`，即使它只有一个块引用。所以判据必须同时是"编译器的不可观察证明 + 运行时的唯一可写存储 + 算子语义允许"。`map` 因此**排除**：`f` 可以捕获 `xs`，写它所遍历的数组会让结果依赖于遍历顺序。

### 7.3 算子分类

设计原则 10.5 的"返回新值的数组操作"指结果与操作数 0 同类型、且语义允许覆盖的操作；下表的"否"不是遗漏，而是语义不允许（例如 `argsort` 的结果类型不同、`matmul` 的形状不同）。每个"是"都有分配计数用例，每个"否"都有理由与结构测试的例外清单。

| 算子 | 原地？ | 方式 | 不做的理由 |
|---|---|---|---|
| `append`（`xs = xs + [v]`） | **是**（已落地） | `:struct/append_arr_in_place`（`setInPlaceForm` 声明），就地追加 | — |
| `extend` | **是** | 复用操作数 0 的块，追加 | — |
| `concat`（`xs = xs + ys`） | **是** | 结果以操作数 0 为底 | — |
| `filter` | **是** | 原地压缩，末尾收缩 | — |
| `unique` | **是** | 原地压缩 | — |
| `sort` | **是**（已落地） | `array` 的原地形式（`src/builtin/operators/array.cpp`） | — |
| `set_index`（`xs[i] = v`） | **是**（已落地） | `:op/set_index_in_place` | — |
| `tail` | 不分配（视图） | 返回 `rowView`/偏移视图 | 不是"原地写"，而是不分配 |
| `head` | 不分配 | 返回元素 | — |
| 张量逐元素一元/二元（`add`、`sub`、`mul`、`div`、`neg`、`relu`、`exp`、`log`、`sqrt`、`abs`、`tanh`、`sigmoid`、`maximum`、`minimum`、`pow` 等） | **是**，当结果类型与操作数 0 完全相同时 | 内核按同一下标读写；注册时为每个这样的算子生成 `_in_place` 形式 | 广播的第二个操作数不参与写；混合 dtype 提升的结果类型不同 ⇒ 不原地 |
| `map`/`apply` | 否 | — | `f` 可以读原数组 |
| `argsort` | 否 | — | 结果类型不同（`int[d0]`），且键必须保留 |
| `flatten` | 否 | — | 行是独立数组（嵌套），不是一块连续内存 |
| `zip` | 否 | — | 元素类型改变 |
| `copy`/`clone` | 否 | — | 定义就是复制 |
| `range` | 否 | — | 没有操作数可复用 |
| 切向量算子（`tangent_add_arr` 等） | 否 | — | 梯度累加语义，别名由 autodiff 持有 |
| `reshape`/`view` | 不分配 | 视图 | — |
| 张量归约（`sum`/`mean`/`max` 带轴） | 否 | — | 结果形状不同 |
| `matmul` | 否 | — | 结果形状不同，操作数不可覆盖 |
| `conv`/`bn` | 否 | — | 结果形状不同 |

"计数为 1 时原地"是**保证**，不是机会：测试逐项断言分配次数（7.5），结构测试保证没有漏项（7.6）。

### 7.4 实现

- 重绑改写的机制已存在（`OperatorTable::setInPlaceForm`、`declareReusesOperand`、`Builder::inPlaceRebindingOf`），P6 只是**注册更多对**，不新造机制。批复第 5 条要求的"临时值原地写"（`relu(exp(x))` 按使用者计数）不在 P6：接口留在 `reusesOperand`，由图存储提供使用者计数。
- 集合算子（`extend`/`concat`/`merge`/`filter`/`unique`）：在 `src/builtin/operators/struct.cpp` 的现有实现里加 `bool inPlace` 分支，复用 `reusableBlock(newBytes)`（它已经在计数为 1 且容量够时返回原块）；结果对象用 `adoptBlock` 接管；`filter`/`unique` 结束时把 `extents_[0]` 设为保留数并 `shrinkToFit`（容量可大于长度，紧凑数组允许容量 > 长度，见 `reserve` 注释）。
- 张量逐元素：在 `modules/tensor` 的算子注册处，对满足"结果类型 == 操作数 0 类型"的逐元素算子生成 `_in_place` URI（一个循环注册，不逐算子手写）；内核拿到输出数组时先 `canWriteInPlace()`，可以就把输出指向操作数 0 的存储（`Array::reusableBlock`），否则新建。四个后端都走同一段内核，所以只改一处。
- 反向排除的注释写进 `OperatorTable` 的注册旁（"为什么不原地"），作为 7.6 结构测试的清单来源。

### 7.5 分配计数测试

用 `std::mem::blocks()` 的 `allocs` 差值，数组必须 > 256 字节才走块（否则内联数组没有分配，测不到）。每个算子一个用例，形状 `int[64]`（=512 字节）或 `f32[128]`：

```camel
let before = std::mem::blocks().allocs;
xs = xs + [7];                       // 或 xs = xs->sort()
let after = std::mem::blocks().allocs;
println(after - before);             // 期望 0
```

覆盖：`append`、`extend`、`concat`、`filter`、`unique`、`sort`（各一次）、`set_index`、张量逐元素一元与二元各一次（f32 与 f64 各一组）、`tail`（视图，0 次）。对照用例（**必须分配**）：`let ys = xs; xs = xs->sort()`（1 次——`take` 复制，再原地排序）、`map(xs, f)`（1 次输出）、`argsort`（1 次）。

文件：`test/cases/feat/mm/dyn_in_place/*.cml`、`test/plans/feat/mm/dyn_in_place.plan.toml`。断言用打印的计数差，不用时间。

### 7.6 结构性测试

一个自检（`src/core/operator_table_selftest.cpp` 或 `test/plans/feat/mm/op_in_place.plan.toml` 的遍历）：对算子表里每个返回数组的算子，断言它要么有原地形式（`setInPlaceForm`）且 `reusesOperand` 一致，要么在例外清单里（清单与 7.3 的"否"行一一对应，写在 `src/builtin/executors/builtin.cpp` 附近）。这样以后新增一个返回数组的算子而忘了分类，测试会失败。

### 7.7 P6 的文件与门禁

改：`src/builtin/operators/struct.cpp`、`src/builtin/executors/builtin.cpp`、`src/core/operator_table.cpp`（只读清单）、`src/builtin/operators/array.cpp`、`modules/tensor` 的注册与内核入口、用例与计划。

门禁：`feat/mm/dyn_in_place`、`feat/mm`、`feat/loops`、`feat/modules/tensor`、`feat/para`、`smoke`。P6 在 P5 之后做，避免行语义与原地压缩同时改。

---

## 8. P7：性能基线

### 8.1 两条红线

- **小型逻辑程序不变慢**：批复第 1 条——`fib.deep`、`alloc_small`、`map_chain` 在改造前后各测一次（多轮中位数、空载机器），时间与峰值内存不得上升超过 ±3%。复核收紧为**交错 A/B**、**先测噪声底**：同一提交加一处不执行的无关改动再构建一次，两者之差是布局噪声；只有超过噪声底才算数。
- **张量程序的时间与峰值内存不升**：同一组张量程序在 A（改造前）与 B（改造后）上跑，时间在噪声底以内，峰值内存不超过 A 的 +3%。

### 8.2 程序

现有 `test/tools/perfbaseline/baseline.py` 的 `PROGRAMS` 只有 `train.mlp` 与五个 GC/调用程序，没有小的张量程序。P7 增加（全部用**两套构建都接受的语法**，即只用 `Tensor<f32>` / 裸 `Tensor` / `zeros`/`new`，不写 `const`、不写形状注解，这样同一份文本能在 A、B 上编译）：

| 名字 | 程序 | 测什么 |
|---|---|---|
| `tensor.elementwise` | 长度 `1 << 20` 的 f32 数组上 20 次 `x = exp(x * 1.0001 + 0.5)` | 逐元素内核 + 每次一个新数组的分配 |
| `tensor.matmul` | `256 × 256` 乘 `256 × 256`，5 次 | 计算密集 + 内核分派 |
| `tensor.reduce` | `1024 × 1024` 的 `sum`，20 次 | 归约与缓存 |
| `tensor.alloc` | 循环 200 次创建长度 `1 << 20` 的 f32 数组并丢弃 | 块字节触发的 GC 与峰值内存 |
| `tensor.hof` | `[512, 128]` 上的 `map`/`filter`/`sum` | 第一维语义的新代价；**只作记录，不设回归阈值** |

`tensor.hof` 在 A 上的语义不同（rank 1 元素），不能作 A/B 回归行；它在 B 上只测一次，把每行的分配与耗时写进报告，供后续优化参考。

`baseline.py` 同时加进 `PROGRAMS`（`required=False`，与其它程序一样参与 `ab.py`），并在 `docs/performance/graph-store-baseline.md` 的程序表里登记。

### 8.3 顺序与方法

1. **A 的构建**：在动手写 P1 之前，于当前工作树构建基线提交（`6a1c40c0`，或实施开始时的 `develop` 头），把 `out/latest` 整个复制到工作树外的 `out-a` 目录，记录 `git rev-parse HEAD`。A 的二进制之后不再重建。
2. **B 的构建**：P6 完成后（P7 之前），构建同一工作树，复制到 `out-b`，记录 HEAD。
3. **噪声底/交错 A/B**：用 `test/tools/perfbaseline/ab.py`，它对每个程序跑 `A1 B A2` 成对、取中位数、打印 `|A1−A2|/A` 的噪声底与峰值 MB：

   ```
   python3 test/tools/perfbaseline/ab.py --a <out-a> --b <out-b> --pairs 5 --rounds 5 \
       --what compile,optinline,opt,nvm,fvm,jit --only fib.deep,alloc_small,map_chain
   ```

   张量行同理，`--only` 换成 `tensor.elementwise,tensor.matmul,tensor.reduce,tensor.alloc`。红线行按批复只判时间与峰值；`compile` 也一并测，作为"编译期没有变慢"的证据。
4. **中间红线（P4 之后、P5 之前）**：批复要求"超了先修再继续第 5 步"。用同一份 A 与 P4 后的构建 B′，只跑三个红线程序的 `nvm,fvm,jit`（`--pairs 3`），阈值同 8.4；超了就在开 P5 之前修 P2–P4。P7 的最终测量仍用 P6 后的完整 B，写进文档。
5. **空载**：同一时段、无其它构建与测试并行；先跑 `fib.deep` 一次确认噪声底 < 3%，否则换时段重跑。作废受干扰的整轮（复核的做法），不挑单个数字。
6. **不跨构建单次比较**：任何执行时间只在同一次 `ab.py` 调用内比较；`docs/performance/graph-store-baseline.md` 已有的"执行时间不跨构建单次比较"一节继续有效（复核第 3 条已写）。
7. **认真记录**：结果表（程序、模式、A、B、B/A、噪声底、峰值 A/B）写进 `docs/performance/graph-store-baseline.md` 新增一节"改造前后"。A 的提交、B 的提交、机器、时间、`--pairs/--rounds` 全部写明。

### 8.4 通过标准

- 三个红线程序：`B/A ≤ max(1.03, 1 + 噪声底)`，且峰值 B ≤ A × 1.03；P4 后的中间检查超了 ⇒ 在开 P5 之前修；P7 的最终表超了 ⇒ 先修再合并 P7。
- 张量行：`B/A ≤ 1 + 噪声底`（时间），峰值 B ≤ A × 1.03；超了先查是否 P2/P4 引入（数组类型驻留多了几个词、打印路径、`getElementView` 分派）。
- `compile` 模式若超出，先看 P3/P4 的解析与类型驻留开销（`ArrayType` 键从 1 个词变 4 个词）。

### 8.5 P7 的文件与门禁

改：`test/tools/perfbaseline/baseline.py`（PROGRAMS）、`test/cases/perf/tensor/*.cml`、`docs/performance/graph-store-baseline.md`。不改被测代码。

门禁：`python3 test/tools/perfbaseline/ab.py ...` 的两组调用；`feat/perf` 只保证程序能编译与运行。P7 完成后，`docs/performance` 的表格是评审依据。

---

## 9. 提交顺序与可独立落地

| # | 提交（一次构建通过、相关测试通过） | 依赖 | 删除/迁移的旧结构 | 回退点 |
|---|---|---|---|---|
| P1 | `test: prove the block boundary` | 无 | 无 | 单独 revert，无影响 |
| P2 | `feat(core): extents on ArrayType` | 无 | 无（`TensorType` 仍在） | 单独 revert，无影响 |
| P3 | `feat(lang): T[dims] syntax and dimension binding` | P2 | 形状位置的 `constas`、`ArrayTypeLoad.dims_` 计数 | 单独 revert；P2 留下 |
| P4 | `refactor(tensor): Tensor is an ArrayType alias` | P3 | `TensorType`（570 行）、`normalizeTensorDType`、`printTensor`、第二套类型码 | 单独 revert；P1–P3 留下 |
| P5 | `feat(lang): first-dimension array operators` | P4 | 四个后端的旧 `map_arr` 元素循环 | 单独 revert；P4 留下 |
| P6 | `feat(mm): in-place forms for array-returning operators` | P2（实际排在 P5 后） | 无（只加注册） | 单独 revert |
| P7 | `docs(perf): before/after baseline` | 全部 | 无 | 单独 revert |

每一步都能构建、相关测试通过；P2、P3 是"行为不变"的，可以先进 `develop`；P4 是原子提交，不能拆；P5/P6 相互独立，顺序可换；P7 只写文档与基准程序。

**哪一步可以单独落地**：P1、P2、P3 都能脱离后续步骤独立合并。P3 合并后，`Tensor<f32, [n, m]>` 已经能用（`TensorType::cloneWithArguments` 接受 `extents`），只是 `Tensor` 还是模块类型；P4 不做也不会留下半转换的树。P5 依赖 P4（行类型需要数组类型），P6 不依赖 P5。

---

## 10. 破坏面

### 10.1 语言与标准库

| 位置 | 现在 | 之后 | 性质 |
|---|---|---|---|
| `Tensor<f32, const [n, m]>` | 唯一写法 | `Tensor<f32, [n, m]>` 或 `f32[n, m]` | 源级破坏（87 个用例文件、203 处 `const [`) |
| `Tensor<f32, const [-1, 3]>` | `-1` 未知 | `?` | 源级（20 个文件、26 处） |
| `constas n` 在形状里 | 绑定维度 | 语法错误 | 源级（12 个文件） |
| `Tensor` 裸 | 任意数值张量 | `Tensor`（数值、dtype 未定、任意秩）——语义相同，表示不同 | 打印/类型文本 |
| `Tensor<float>` | 归一化为 f32 | f64 存储 | **语义变化**（批复 Q1 已同意；无测试使用） |
| `Tensor<i32>` | 归一化为 i64 | i32 存储 | **语义变化**（无测试使用） |
| `Tensor` 的打印 | `tensor(...)` | `[[...], [...]]` | 输出（10 个计划、72 行） |
| `T[]` | `int[]` | `int[?]`（同一对象） | 不变 |
| `var` 加宽 | 任意秩 | 同秩（Q5） | `static_types.cml` 需要改写 |
| `map`/`filter`/`foreach`/`reduce`/`sort`/`len` 签名 | `T[]` | `T[..]` | 标准库声明 + 文档 |
| `Tensor<byte>`/`Tensor<c64>` | 接受为 `TensorType`（许多算子不支持这种元素，运行时才暴露） | `byte[..]`/`c64[..]`；不支持的算子报故障 | 放宽且提前暴露 |

### 10.2 测试与文档

- 87 个用例文件含 `const [`（203 处）；104 个张量用例中 51 个含 `Tensor<`；34 个计划提到张量；10 个计划/72 行含 `tensor(` 期望输出。
- 12 个 `constas` 用例（18 处）、20 个 `-1` 形状（26 处）。
- `test/cases/tools/grammar/keywords.cml` 的 `Tensor<f32, const [constas n, 4]>`（金标 `test/plans/feat/tools/golden/grammar/keywords.scopes` 随之更新）。
- `test/plan-selection.json`：`src/core/type`、`src/core/rtdata/array.cpp`、`include/camel/core/type` 的改动应选中 `feat/compile/array_*`、`feat/mm`、`feat/modules/tensor`（新增规则）。
- 文档：`docs/language/08_top_level_and_grammar.md`（+`en/`）、`docs/language/03_data_model.md`、`docs/stdlib-reference.md`（重新生成）、`docs/diagnostics.md`（重新生成）、`docs/performance/graph-store-baseline.md`（P7 节）。
- 格式化：`feat/format` 的往返语料加 `T[n, m]`、`?`、`..s`、`Tensor<f32, [3]>`。
- 快照：`feat/compile` 的 AST 金样只在 smoke 语料上；新语法进入 `feat/format` 与 `feat/parse` 的语料，不改旧金样。

### 10.3 内部接口

- `ArrayType` 的构造与字段（`elemType_` 之外新增）；`typeEquals/typeMangle/typeToString` 的数组分支。
- `Type` 新增四个虚函数（有默认实现，非纯虚，避免所有子类被迫实现）；`OtherType` 的 `extentForms/withExtentValues` 变空实现。
- `TypeTable` 新增 `internExtents`。
- `EntryArgumentLoader` 增加"能否接这个类型"的谓词。
- `Array` 新增 `rowView/rowCopy/appendRow/setExtents`。
- `TensorType`、`asTensorType`、`normalizeTensorDType`、`printTensor` 删除。
- 调度器的 `map_arr` 循环签名（内部）。
- `OperatorTable` 的原地注册面扩大（接口不变）。

### 10.4 明确不破坏

- `T[]` 的含义与打印；`int[][]` 的嵌套含义；`size()`/`numel()`/`strides_`；小数组内联 256 字节；`std::mem::blocks()` 的字段与语义；DLPack 的 CPU/连续/数值标量限制；`std::cpp` 的多维拒绝；`constas` 在非形状位置（统一处理）。

---

## 11. 风险与放弃路径

### 11.1 P4 为什么最险

1. 它是**类型身份**的改动：`TensorType` 是模块类型的唯一代表，删掉后所有"这是不是张量、dtype 是什么、形状是什么"的判断改走 `ArrayType` + 谓词；漏一处就是运行期崩溃或错误的类型检查。
2. 波及 6 个模块（tensor、nn、onnx、tiles、tree、autodiff）、25 个文件的 51 处 `asTensorType`、87 个用例文件的 203 处 `const [`、10 个计划的 72 行期望输出。
3. 元素类型精确会暴露 conv/bn 的 f32-only 内核；打印统一会暴露所有依赖 `tensor(...)` 的期望。
4. 入口参数按 `TypeCode::Array` 查加载器，稍不注意会让 `int[]` 入口参数被张量模块接走。

### 11.2 放弃 P4 的步骤

P4 是一个提交。如果评审或实施中发现它无法在合理时间内收敛：

1. `git revert <P4>`（或丢弃该提交），树上回到 P3：`T[dims]` 语法、`ArrayType` 维度、`constas` 退休、维度的 `unify`/`widen`/`cast` 都在；`Tensor` 仍是 `TensorType`，`Tensor<f32, [n, m]>` 仍由 `TensorType::cloneWithArguments` 接受（P3 保留这条路径）。
2. 不需要回退 P2/P3，也没有"一半数组类型、一半张量类型"的状态：P3 的 `Tensor<...>` 应用走的是 `OtherType` 的现有路径，P4 只是把它换成 `ArrayType::applyArguments`。
3. 第 2 项里"形状检查进核心"在 P2 已落到 `ArrayType`，P3 的 `T[dims]` 直接用它；P4 只是把 `Tensor` 也切过来并删掉旧的第二套实现。剩下三项（第一维、原地写、性能）与 P4 无关，可照常进行。
4. 报告里记录放弃原因与后续计划（例如等 `modules/onnx` 的 dtype 审计完成后再做）。

若 P5/P6 在 P4 之后出问题，它们各自是独立提交，单独 revert 即可；P4 不回退。

### 11.3 其它风险

| 风险 | 表现 | 缓解 |
|---|---|---|
| `..s` 空包 | `T[..rest]` 在 rank 1 实参上折叠成元素类型（标量）；语义新，评审要点头 | Q1 先定；`withExtentValues` 一处实现；用例锁住 |
| 裸 `..` | 打印任意秩类型需要它 | Q2；只允许在最后 |
| `map` 的行语义 | rank ≥ 2 结果是 `U[d0]` 而非保形 | Q3 确认；rank 1 不变，用例锁住两种秩 |
| `sort` 行序 | 字典序 vs 其它 | Q4；选字典序 |
| `var` 加宽 | 同秩会改写 `static_types.cml` | Q5；选同秩，改写一个用例 |
| `Tensor<float>` 变 f64 | 用户可见 | 批复 Q1 已同意；无测试使用；文档写明 |
| conv/bn 的 f64 | 静默读错 | 推断处拒绝；内核入口断言 |
| 行视图的分配 | `foreach`/`filter` 每行一个小对象 | 记录在 P7 `tensor.hof`；不设阈值；下标切片的编译期规划可消掉 |
| 性能红线 | 数组类型多几个词、`getSlot` 分支 | P7 交错 A/B；超噪声底先修 |

### 11.4 不留半转换的检查清单

- 源码里不再有 `TensorType`（`grep -rn 'TensorType' src include modules` 为空）。
- 不再有 `asTensorType`、`normalizeTensorDType`、`printTensor`、`kUnknownDim`。
- 不再有形状位置的 `constas`（用例与标准库的 18 处已改写；其余用法统一处理）。
- `T[]` 的打印与今天一致；`int[]` 与 `int[?]` 是同一驻留对象（自检）。
- 每个返回数组的算子有原地形式或列在例外清单（结构测试）。
- `baseline.py` 的新程序在 A、B 上都能编译（P7 的第一件事）。

---

## 12. 给协调者的问题

**Q1（`..s` 空包）**：`f32[..rest]` 在 `xs: f32[3]` 上，`rest` 绑定为空。返回类型 `T[..rest]` 应折叠成元素类型 `T`（标量），这样 `first_row(xs: T[n, ..rest]): T[..rest]` 才能对 rank 1 返回标量。另一种选择是禁止 rank 1 实参、要求至少两维。建议：**折叠**；数组本身仍 rank ≥ 1。

**Q2（裸 `..`）**：打印 `Tensor<f32>` 需要 `f32[..]` 这个拼写。建议：**允许裸 `..`**（匿名包），只允许在最后、最多一次。

**Q3（`map` 的行语义）**：按任务与设计第 8 节的字面规则，`map` 与 `filter`/`foreach`/`reduce`/`sort` 一样按第一维：rank 1 的"行"是元素（行为与今天不变），rank ≥ 2 时 `f` 收到一行、结果是 `U[d0]`（见 6.1）。另一种是逐元素保形（`f` 收到元素、结果保留全部维度），它与任务文字不符、且需要另一套类型规则。建议：**按行**；请确认 rank ≥ 2 时 `map` 的结果是 rank 1 的 `U[d0]`，而不是保形成 `U[d0, ..., dn]`。

**Q4（`sort` 对 rank ≥ 2）**：按行字典序（建议）、按行首元素、还是拒绝？建议：**字典序**，`argsort` 同样。

**Q5（`var` 加宽）**：`var xs = [1, 2]; xs->append(3)` 要求加宽保留 rank 1（`int[?]`）。建议：**同秩加宽**（更安全，`append` 仍工作）；代价是 `var w = zeros([2,1]); w = ones([5])` 不再通过，要改写 `static_types.cml` 这一个用例。任意秩加宽会让 `var xs = [1]` 加宽成任意秩、`append` 失去形状检查。

**Q6（裸 `Tensor`）**：建议 `Tensor` = "元素是数值、dtype 未定、任意秩"的数组类型（`numericUnknown_` 标记，打印 `Tensor`），`Tensor<[3]>` 打印 `Tensor<[3]>`；`Tensor<f32>` = `f32[..]`。这样 `Tensor` 不再接受 `string[]`（今天也不接受），且仍是 `ArrayType`，没有第二套类型。请确认这个"标记"而不是"新的元素类型"。

**Q7（`Tensor<float>` = f64）**：批复 Q1 已同意；确认在文档里写明，并把 conv/bn 等 f32-only 算子的拒绝方式定为运行时故障。

**Q8（秩 0）**：`Tensor<f32, []>` 今天表示标量张量（形状列表为空）。建议：**本计划不支持秩 0**，`Tensor<*, []>` 报错并建议写 `[1]`；`f32[]` 仍是 rank 1 未知。没有测试使用 `const []`。若必须保留标量张量，请指示，P2 的 `ArrayType` 与 `Array` 要支持零维（`numel=1`、无第一维）。

**Q9（标准库 `sort_by`/`array.cml`）**：本计划只泛化 `sort`/`argsort`/`filter`/`foreach`/`reduce`/`len`；`sort_by`、`flat_map`、`zip`、`flatten` 保持 rank 1，下标切片落地后再泛化。请确认。

**Q10（问题记录的流程）**：按批复最后一条，以上选择写在本计划与报告的"待决问题"里，由协调者批复后进裁定记录；施工方不直接改裁定记录。

---

## 13. 文件清单

### 新增

| 文件 | 内容 |
|---|---|
| `docs/design/arrays-memory/plan.cn.md` | 本文件 |
| `test/plans/feat/compile/array_shape.plan.toml` | 语法/绑定/诊断 |
| `test/plans/feat/compile/array_alias.plan.toml` | `Tensor` 别名 |
| `test/plans/feat/compile/hof_multidim.plan.toml` | 第一维 |
| `test/plans/feat/mm/dyn_in_place.plan.toml` | 分配计数 |
| `test/plans/feat/mm/external_block.plan.toml`（或并入 `blocks`） | P1 自检 |
| `test/cases/compile/array_shape/*.cml`、`array_alias/*.cml`、`hof_multidim/*.cml` | 用例 |
| `test/cases/feat/mm/dyn_in_place/*.cml`、`external_block.cml`、`block_pressure.cml` | 用例 |
| `test/cases/perf/tensor/{elementwise,matmul,reduce,alloc,hof}.cml` | P7 程序 |
| `src/core/type/array_type_selftest.cpp`（或并入已有自检） | 类型自检 |

### 修改（主要）

| 文件 | 改动 |
|---|---|
| `include/camel/core/type/base.h`、`src/core/type/base.cpp` | `extentForms`/`packName`/`withExtentValues`/`applyArguments` 虚函数 |
| `include/camel/core/type/composite/array.h`、`src/core/type/composite/array.cpp` | 维度、驻留键、equals/unify/widened/cast/resolve/clone |
| `include/camel/core/type/extent.h` | `ExtentPacks`、`evaluate` 的空绑定行为 |
| `src/core/type/intern.{h,cpp}` | `internExtents` |
| `src/core/type/text.cpp` | 数组的 toString/mangle/equals 分支 |
| `include/camel/core/type/other.h`、`src/core/type/other.cpp` | 两个虚函数变空实现；`TypeArgument` 保留 |
| `include/camel/parse/syntax/node_kinds.def` | `ArrayDim` |
| `include/camel/parse/ast/type.h`、`src/parse/ast/type.cpp` | `ArrayTypeLoad` 的维度组 |
| `src/parse/syntax/parser_type.cpp`、`src/parse/ast/builder.cpp` | 解析与建树 |
| `src/compile/gct/builder.{h,cpp}` | 绑定、`visitExtent`、`visitArrayType`、`visitSpecType`、诊断 |
| `src/compile/gir/builder_operators.cpp` | 走 `Type` 虚函数的绑定与返回细化 |
| `tools/format/fmt.cpp` | 打印新语法 |
| `include/camel/core/rtdata/array.h`、`src/core/rtdata/array.cpp` | `rowView`/`rowCopy`/`appendRow`/`setExtents`；`print` 用运行时元素类型 |
| `src/builtin/modules/globals.cpp` | HOF/`len` 的第一维类型规则 |
| `src/builtin/operators/{higher_order,struct}.cpp` | 第一维实现；原地分支 |
| `src/passes/sched/{nodevm,fastvm,taskflow}.cpp` | 同一套循环 |
| `src/builtin/operators/array.cpp` | 行排序/argsort；原地注册 |
| `modules/tensor/type.{h,cpp}` | **删除** |
| `modules/tensor/{module,tensor,interop,npy,binary,dtype}.{h,cpp}`、`ops/*`、`passes/index_eval.cpp` | 别名、精确 dtype、加载器 |
| `modules/{nn,onnx,tiles,tree,autodiff}/**` | `asTensorType` → `asArrayType`/谓词 |
| `src/execute/entry_args.{h,cpp}` | 加载器的"能否接"谓词 |
| `src/builtin/executors/builtin.cpp`、`src/core/operator_table.cpp` | 原地注册与例外清单 |
| `stdlib/signatures/globals.cml`、`stdlib/array.cml`（注释） | 签名与第一维 |
| `docs/language/03_data_model.md`、`docs/language/08_top_level_and_grammar.md`（+`en/`）、`docs/diagnostics.md`、`docs/stdlib-reference.md`、`docs/performance/graph-store-baseline.md` | 文档 |
| `test/tools/perfbaseline/baseline.py` | 张量程序 |
| `test/plan-selection.json` | 新计划规则 |
| 本计划路径 | 指向本计划 |

### 删除

`modules/tensor/type.h`、`modules/tensor/type.cpp`、`normalizeTensorDType`、`printTensor`、`asTensorType`、`kUnknownDim`、形状位置的 `constas` 支持、`collectDimensionNames`、`ArrayTypeLoad.dims_`、`TensorType` 的驻留表。

---

## 附：关键接口（签名草案）

```cpp
// core/type/composite/array.h
class ArrayType : public Type {
  public:
    static ArrayType *create(Type *elem);                                   // T[] = T[?]
    static ArrayType *create(Type *elem, std::span<const int64_t> extents);
    static ArrayType *create(Type *elem, std::span<const ExtentForm> extents,
                             std::string_view pack = {});
    static ArrayType *numericUnknown(std::span<const int64_t> extents,
                                     std::string_view pack = {});

    const Type *elemType() const;
    TypeCode elemTypeCode() const;
    std::span<const ExtentForm> extents() const;   // 前缀
    bool anyRank() const;
    std::string_view packName() const;
    bool numericUnknown() const;

    bool extentForms(std::vector<ExtentForm> &out) const override;
    const Type *withExtentValues(const ExtentValues &values,
                                 const ExtentPacks &packs) const override;
    const Type *applyArguments(std::span<const TypeArgument> args, std::string &error) const override;
};

// core/type/base.h（默认实现：不接受参数、不是数组、原样返回）
virtual bool extentForms(std::vector<ExtentForm> &out) const;
virtual std::string_view packName() const;
virtual const Type *withExtentValues(const ExtentValues &values, const ExtentPacks &packs) const;
virtual const Type *applyArguments(std::span<const TypeArgument> args, std::string &error) const;

// core/rtdata/array.h
Array *rowView(mm::IAllocator &allocator, uint64_t row) const;
Array *rowCopy(mm::IAllocator &allocator, uint64_t row) const;
void appendRow(const Array *row);
void setExtents(std::span<const int64_t> extents);
```