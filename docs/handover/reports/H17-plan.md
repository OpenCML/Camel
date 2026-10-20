# H17 实施计划：原生多维数组与缓冲区

分支：`handover/H17-arrays-plan`（基于最新 `develop`）。本文件只是计划，没有实现；按
[交接说明](../README.cn.md) 第 5 节"先交计划的任务"，由协调者在 `reviews/H17.md` 批复后再动手。

依据：[原生多维数组、内存管理与互操作](../../design/arrays-memory/README.cn.md) 第 9 节第 1–3 步，
[设计原则](../../design-principles.cn.md) 5.4、10.5（中文为准），任务书第 1–4 项。

任务书第 1 项 = 提交 1–4，第 2 项 = 提交 5–8，第 3 项 = 提交 9，第 4 项 = 提交 10；提交 0 是这份计划本身。

---

## 0. 这份计划的范围

**做**：缓冲区与引用计数、张量改为"数组值 + 缓冲区"、DLPack 两向零拷贝、核心多维数组类型
`T[维度...]`、维度绑定规则（在 H15 已合入的 `constas`/`ExtentForm` 之上）、`Tensor<T, [dims]>`
别名、形状检查搬进核心、多维数组上的 `map`/`filter`/`sort`/`reduce`、计数为 1 的原地更新、性能基线。

**不做**：

- `src/compile/gir` 的存储与提交、`std::opt`、内联、图 API、执行计划（H18 收回，由协调者实施；
  见 [交接说明](../README.cn.md) 5.0 丙车道）。第 1 层"编译期内存规划"（设计稿第 9 节第 4 步）
  依赖执行计划的使用者索引，H17 只把它的接口位置留出来（见 2.4、2.12）。
- Python 互操作（设计稿第 9 节第 6 步）：`modules/python` 不动。H17 给 C++ 层的 DLPack 进出与自检，
  第 6 步只是加一层 capsule 包装。
- 稀疏、量化等库类型（第 9 节第 7 步）。
- 其它后端的内存规划（第 9 节第 5 步）；各后端只做"照样能跑、照样一致"。

**与其它车道的冲突**：数组类型规则与甲车道（H15/H5）相交，产出类型拼写与诊断的改动集中在提交 5–7，
与甲车道确认后合入；`tensor`、`array`、`algo` 三个模块按 5.0 的约定由乙车道先改，戊车道等合入。

---

## 1. 现状（代码事实）

| 事实 | 位置 |
|---|---|
| 张量是**一个** GC 对象：头部 + 形状 + 数据同在一块可移动的内存里，视图靠 `owner_` + 偏移指向源对象 | `modules/tensor/tensor.h:67`（`TensorObject`、`std::byte storage_[]`）、`createView` |
| 张量元素类型只有 `Float32`/`Int64`/`Bool` 三种存储，`float`/`double` 归一化成 `Float32` | `modules/tensor/dtype.h`、`dtype.cpp:43 normalizeTensorDType` |
| DLPack **两个方向都拷贝**，且拒绝 `strides` 与非零 `byte_offset` | `modules/tensor/interop.cpp:248 tensorToDLPackCopy`、`:276 tensorFromDLPackCopy` |
| 张量类型是 `OtherType` 子类，形状与元素类型解析、运行时形状检查都在模块里 | `modules/tensor/type.h:60 TensorType`、`type.cpp:269 castSlotFrom`、`type.cpp:238 castSafetyFrom` |
| 维度名与线性形式 H15 已合入核心：`ExtentForm`（`?` / 常数 / 线性），绑定在**签名解析**期，`constas` 是唯一的绑定写法 | `include/camel/core/type/extent.h`、`src/compile/gct/builder.cpp:3364–3830`（`dimNames_`、`ConstAs`、`bind`）、提交 `c1734432` |
| 调用点按实参的已知 extent 绑名字，再细化形参类型，不等则 `DimensionMismatch` | `src/compile/gir/builder_operators.cpp:1140–1219` |
| 核心数组类型**只有元素类型**，没有秩、没有维度；打印成 `elem[]` | `include/camel/core/type/composite/array.h:49`、`src/core/type/text.cpp:301` |
| 运行时数组是槽数组：10 个内联槽 + 可选 `FixedArray`；数字与引用混在一起；没有形状、没有计数、没有缓冲区 | `include/camel/core/rtdata/array.h:229` |
| 现成的原子计数 + 延迟释放模型（外部资源控制块、释放队列在收集器解锁后的安全点跑） | `include/camel/core/rtdata/foreign.h`、`src/core/mm/gen.cpp:1037/1056`、`runDeferredForeignReleases` |
| "旧值没人看得到"已有机制：变量格的 `holdsUnsharedArray` 标记，`:var/get` 与 `:op/assn` 清、`:var/take` 后置 | `include/camel/core/rtdata/tuple.h:87`、`src/builtin/operators/ops.cpp:59–165`、`Builder::inPlaceRebindingOf`（`builder_operators.cpp:764`） |
| 内存空间只有 auto（分代 GC）、meta、graph、perm；没有"可单独释放、不移动"的数字缓冲区空间 | `include/camel/core/mm.h:48`、`src/core/mm/mm.cpp:222–355` |
| 入口参数的 loader/saver 按 `TypeCode` 找，张量模块按 `TensorType::typeCode()` 注册 `.npy` 读写；数组没有 | `src/execute/entry_args.cpp:329`、`modules/tensor/module.cpp:103/134` |
| 语法只有空方括号：`arrayType : specType ('[' ']')*` | `src/parse/syntax/parser_type.cpp:185`、`docs/language/08_top_level_and_grammar.md:76` |
| 性能基线 6 个程序，**没有张量程序**；分配计数只在 Linux/glibc 下可用 | `test/tools/perfbaseline/baseline.py`、`docs/performance/graph-store-baseline.md`、`test/tools/allocshim/allocshim.c` |
| 规模：1461 个 `.cml`；张量 99 个用例 / 7 个 plan 文件；`Tensor` 出现在 20 个 plan 文件；`tensor(` 在 21 个文件的期望输出里；44 个文件引用 `TensorObject`；`Array::create` 在 29 个文件；`get<Array*>` 67 处 / 15 文件；`->data()` 78 处 / 23 文件 | 见附录 |

---

## 2. 目标结构与接口

### 2.1 缓冲区对象 `Buffer`（核心，新）

`include/camel/core/array/buffer.h`、`src/core/array/buffer.cpp`。

```cpp
namespace camel::core::array {

enum class BufferOwner : uint8_t { Camel, External };

/// 外部数据的释放方式（DLPack 的 deleter、Python 的 capsule 之类）。
struct BufferRelease { void (*release)(void *context) noexcept; void *context; };

class Buffer {
  std::atomic<uint32_t> count_{1};
  std::byte *data_       = nullptr;
  size_t bytes_          = 0;   // 有效数据
  size_t capacity_       = 0;   // 可增长：仅 Camel 拥有时可原地扩
  BufferOwner owner_     = BufferOwner::Camel;
  BufferRelease release_ = {};   // owner_ == External 时用
  int32_t device_ = 0;           // DLPack DLDevice：H17 只有 CPU
public:
  static Buffer *create(size_t bytes, size_t align = 64);                 // Camel 拥有
  static Buffer *adopt(std::byte *data, size_t bytes, BufferRelease);     // 外部数据
  void retain() noexcept;                                                // fetch_add(1, relaxed)
  bool release() noexcept;                                               // 减到 0 时按 owner_ 释放
  std::byte *data() const noexcept;
  size_t bytes() const noexcept;
  size_t capacity() const noexcept;
  bool camelOwned() const noexcept { return owner_ == BufferOwner::Camel; }
  bool unique() const noexcept { return count_.load(std::memory_order_relaxed) == 1; }
  void grow(size_t bytes);            // 仅 unique() && camelOwned()
};

} // namespace camel::core::array
```

要点：

- **只放数字**（红线）。类型与缓冲区的对应关系由 2.3 的规则从元素类型算出，`Array` 的构造处断言，
  调用点不能选：一种数组类型只有一种表示。
- 计数 `std::atomic<uint32_t>`，全程 `relaxed`，只有"减到 0"的那一次用 `acq_rel` 并在 0 时取
  `acquire`（与 `ForeignResourceControlBlock` 同一种写法，见 `include/camel/core/rtdata/foreign.h`）。
  并行的 `std::prl` 与张量模块自己的池（`modules/tensor/kernels/parallel.cpp`）上都会创建/销毁视图，
  没有标志位区分"单线程/多线程"——一条路径（自查第 2 条：不造第二套机制）。
- **数据不在可移动的堆里**：新增 `mm::bufferSpace()`，一个可单独 `free`、不移动的空间，走
  `GrowableFreeListAllocator` 的实例（与 `metaSpace` 同一种，带 `CAMEL_BUFFER_LIMIT_MB` 上限，
  不写死）。它和 `metaSpace` 一样**不在退出时销毁**（`src/core/mm/mm.cpp:325` 有同样的
  destruction-order 注释），因此 DLPack 的 deleter 在运行时退出之后从任何线程调用都安全。
- 进程退出时仍在的缓冲区由空间自己回收（不逐个释放），计数器照常统计。
- 计数器进 `mm::profiler`（已有的 `summaryToText`），打印一行
  `buffers: allocs=… frees=… live_bytes=… peak_bytes=…`。

### 2.2 数组值 `Array`（核心，改）

`include/camel/core/rtdata/array.h` 升级为"两种存储 + 形状"。类名与 `get<Array*>` 不变。

```cpp
class Array : public rtdata::Object {
  Buffer      *buffer_   = nullptr;  // 紧凑存储持一次引用
  IAllocator  *allocator_ = nullptr; // 槽存储用（现状）
  slot_t      *slots_     = nullptr; // 槽存储：内联或 FixedArray（现状）
  uint32_t     size_      = 0;       // 第一维长度（!= 元素总数）
  uint32_t     numel_     = 0;       // 元素总数
  uint32_t     capacity_  = 0;       // 槽存储的容量
  uint32_t     rank_      = 1;
  int64_t     *extents_   = nullptr; // rank_ 个（随对象尾部，onMoved 重算）
  int64_t     *strides_   = nullptr; // rank_ 个，行主序；H17 只产生连续
  size_t       offset_    = 0;       // 视图的字节偏移
public:
  static Array *create(IAllocator &, const ArrayType *, std::span<const int64_t> extents);
  static Array *view(Array *source, std::span<const int64_t> extents, size_t byteOffset);
  // 名字与今天兼容；rank 1 时 size() == numel()
  uint32_t size() const noexcept;    // 第一维长度；len(xs) 用它
  uint32_t numel() const noexcept;   // 元素总数（新增）
  uint32_t rank() const noexcept;
  int64_t extent(uint32_t i) const noexcept;
  int64_t step(uint32_t i) const noexcept;
  bool compact() const noexcept { return buffer_ != nullptr; }
  Buffer *buffer() const noexcept;
  // 元素读写：槽存储与紧凑存储都走这两个（新增）
  slot_t getSlot(uint32_t flat) const;
  void setSlot(uint32_t flat, slot_t value, const ArrayType *);
  std::byte *elementPtr(uint32_t flat) const;   // 紧凑存储
  slot_t *data() const;                         // 槽存储，或紧凑且元素 8 字节（布局逐位相同）
  template <typename T> T *dataAs() const;      // 紧凑存储，元素类型必须是 T
};
```

- **`size()` 保持今天的含义**（第一维长度 = `len`），不重命名，避免 ~150 处机械改动；
  新增 `numel()` 给"元素总数"用。需要 `numel()` 的地方很少且固定：打印、`equals`、`clone`、
  内存大小、`.npy`、DLPack、以及把数组当平面的内核。rank 1 时两者相等，所以漏改只在 rank ≥ 2 时错，
  由多维用例兜住（这也是把 `numel()` 单列而不是重命名 `size()` 的理由：改动小、审阅面固定）。
- 紧凑存储的 `dataAs<T>()`、`getAs<T>()`、`setFrom<T>()` 是张量内核今天用的名字
  （`modules/tensor/tensor.h`），搬到核心时**保持同名同义**，让 44 个文件的改动压到"指针类型与工厂函数"。
- 槽存储（非数字元素）与今天完全一致：内联 10 槽 + `FixedArray` + 写屏障。
- 视图：`view()` 只 `retain()` 源缓冲区并记偏移与形状，不拷贝；`strides_` 在 H17 只写连续行主序，
  非连续视图（`transpose` 直接当视图）留到后续（见第 8 节）。
- `onMoved()`：重算 `extents_`/`strides_`/`slots_` 的自指针；`data_` 不需要修（缓冲区不动）。
- `finalize()`（新增）：把 `buffer_` 交给**延迟释放队列**，由 `runDeferredForeignReleases()` 的
  同一处、同一时机（收集器解锁后、安全点、不持运行时锁）`release()`。理由：`finalize` 在收集过程
  中跑，不能在那里调外部释放回调（DLPack 的 deleter 可能回调任意代码，也可能加锁）。
- 紧凑数组在创建时登记 `noteFinalizable`（与 `ForeignResourceObject` 同一机制），否则年轻代里直接
  死掉的对象不会跑 `finalize`，缓冲区就漏了。

### 2.3 元素类型与存储规则（一处事实）

`include/camel/core/type/composite/array.h`：

```cpp
enum class StorageClass : uint8_t { Compact, Slots };
/// 唯一的判据：数字元素类型走紧凑缓冲区，其余走槽数组。
StorageClass storageFor(const Type *elementType);
```

- 数字类型：设计原则 5.1 的数字类型表。今天核心里已有 `bool`、`u8`、`i32`、`i64`、`f32`、`f64`；
  `f16`、`bf16`、`c64`、`c128` 随各自的类型任务加入，`storageFor` 那时只是多认几个 `TypeCode`
  （规则本身不分组、不列名清单以外的东西）。其余（`string`、`Bytes`、元组、结构体、`enum`、函数、
  数组的数组）走槽。
- **8 字节数字（`i64`/`f64`/`c64`）的紧凑布局与今天的槽布局逐位相同**：`data()` 在
  "紧凑且元素 8 字节"时返回 `(slot_t *)buffer_->data()`，所以 `int[]`、`float[]` 的绝大多数
  现有 C++ 代码（`data()[i]`、`get<int64_t>`）**不改也能跑**，行为与分配来源之外的语义不变。
  这条是有意的：它把"数字数组全部紧凑"的改动面压到 1/2/4/16 字节元素（`bool[]`、`f32[]`、`u8[]`、
  `c128[]`）——今天正好只有张量用这些类型，加上 `bool[]` 的谓词数组。
- 红线（缓冲区只放数字）由两件事保证：`storageFor` 是唯一入口；`Array::create` 对 `Compact`
  断言元素类型是数字、且 `bytes = numel * elementSize(elem)`。

### 2.4 生命周期：GC 与引用计数怎么接

三层（设计稿 4）：

1. **编译期规划**：不在 H17（需要执行计划的使用者索引，H18/协调者）。H17 只在 `OperatorTable`
   上留一个"这个算子可以复用第 0 个实参的缓冲区"的声明（2.12），由建图器当天已有的
   "不共享"标记喂给它；第 1 层落地后，同一处改用使用者索引，声明不变。
2. **缓冲区引用计数**：2.1。只对数字缓冲区；`count == 1` 且归 Camel 拥有的原地更新是保证（2.12）。
3. **其余一切**：分代追踪 GC，不变。数组值仍是可移动的 GC 对象；`updateRefs` 不变（缓冲区不在堆里，
   没有指向堆内对象的指针）；`RefRelocator` 不碰缓冲区。

两个计数器的分工写进 `include/camel/core/array/buffer.h` 的注释（这也是红线的强制机制之一：
设计稿 7 要求"红线各有一条强制机制"，缓冲区"只放数字"的机制是 `storageFor` + 构造断言）。

### 2.5 DLPack：两个方向都零拷贝

新文件 `include/camel/core/array/dlpack.h`（由 `modules/tensor/dlpack.h` 移入并扩展：
设备、`strides`、`byte_offset` 都有），函数放在 `src/core/array/dlpack.cpp`：

```cpp
DLManagedTensor *toDLPack(const Array *array);              // 出：零拷贝
Array *fromDLPack(DLManagedTensor *managed, IAllocator &);  // 入：接管所有权
```

**出（Camel → 外部）**：

- 缓冲区 `retain()`；`manager_ctx` 指向它。
- `DLManagedTensor`、`shape[]`、`strides[]` 用**普通堆**（`new` / `delete`）分配——capsule 可能
  活到运行时结束之后，不能是 GC 内存，也不能是永久空间。
- `data = buffer->data() + offset`（缓冲区不动，指针永远有效）；`device`、`dtype`、`ndim`、`strides`、
  `byte_offset` 从数组值填。
- `deleter`：`buffer->release()`，再 `delete[]`、`delete`。只碰原子计数与普通堆，任何线程、任何时刻都安全。
- 视图（`x[i]`、`reshape`）导出时 `retain` 的是**源缓冲区**，偏移与 strides 表达视图。
- 零拷贝的判据进自检：导出后的 `data` 与数组值的数据指针**相同**（不 memcpy）。

**入（外部 → Camel）**：

- 按协议**接管** capsule：不支持的设备/元素类型/`strides`（H17 接受行主序：空或连续）时，
  **先调 `deleter` 再抛**（协议要求最后持有者负责），这条也进自检。
- 接受 `byte_offset`（今天被拒绝）。
- `Buffer::adopt(data, bytes, {deleterThunk, managed})`；`deleterThunk` 调
  `managed->deleter(managed)`，保证**恰好一次**（缓冲区的 `release()` 减到 0 才调）。
- 外部缓冲区 `camelOwned() == false` ⇒ **永不原地写**（见 2.12 第 3 条）。
- 元素类型与形状构成核心数组类型；导入的值与 Camel 自己造的值在类型上无区别。

**范围**：`modules/python` 不动（设计稿第 9 节第 6 步）。H17 的验收是 C++ 自检 + 一条
"导出→导入→读写一致、deleter 只跑一次、指针相同"的用例。

### 2.6 类型：核心数组类型带秩与维度

`include/camel/core/type/composite/array.h`、`src/core/type/composite/array.cpp`：

```cpp
class ArrayType : public CompositeType {
  Type      *elemType_ = nullptr;      // Void = 元素类型未定（`[1, 2, 3]` 字面量）
  StorageClass storage_ = StorageClass::Slots;
  bool       anyRank_  = false;        // `..s`
  std::vector<ExtentForm> extents_;    // rank 个；ExtentForm::Unknown 即 `?`
public:
  static ArrayType *create(Type *element, std::span<const ExtentForm> extents);
  static ArrayType *createAnyRank(Type *element);
  bool anyRank() const noexcept;
  size_t rank() const noexcept;                 // anyRank 时为 0
  const ExtentForm &extent(size_t i) const;
  std::span<const ExtentForm> extents() const;
  // 形状规则在核心（今天在 modules/tensor/type.cpp）
  CastSafety castSafetyFrom(const Type *) const override;   // 细化格
  slot_t castSlotFrom(slot_t, IAllocator &) const override; // `as` 的运行时形状检查
  bool assignableFrom(const Type *) const override;         // 协变 + 维度
  bool equals(const Type *) const override;                 // 名字不参与
  std::span<const ExtentForm> extentForms() const;          // 由 OtherType 下沉
  Type *withExtentValues(const ExtentValues &) const;       // 由 OtherType 下沉
  std::string toString() const override;                    // f32[3]、f32[n, 3]、f32[]、f32[..s]
};
```

- 驻留：元素类型指针 + `anyRank` + 每个 extent 的 `toString()`。名字参与驻留、不参与 `equals`
  （与今天 `TensorType` 的规矩一致，H15 已按这条评审过）。`_` 表示元素类型未定（`_[3]`），
  避免与 extent 的 `?` 撞车（今天 `Tensor<?, [3]>` 的 `?` 是元素类型，H17 之后 `?` 只能是 extent）。
- `castSafetyFrom` 用今天 `TensorType::castSafetyFrom` 的细化格（`type.cpp:238`）：
  同元素类型、同秩、逐维"更具体 → 更一般"为 `Safe`，已知与未知之间为 `Checked`，秩不同或元素类型
  不同为 `Forbidden`。
- `castSlotFrom` 用今天 `type.cpp:269` 的运行时检查（形状与元素类型），错误种类沿用
  `TensorDimensionMismatch`（Q5）。
- `assignableFrom`：协变 + 维度的"少信息可赋给多信息"方向，与 `castSafetyFrom` 用同一张表
  （今天两处各写一遍，H17 合成一处）。
- `extentForms`/`withExtentValues` 从 `OtherType` 的虚钩子**下沉**到 `ArrayType`；`OtherType` 上的
  两个虚函数删除（实施时确认没有别的子类实现它们）。这是"形状检查搬进核心"的收尾：
  绑定维度名不再要求类型是 `OtherType`。
- 打印与 mangle：`src/core/type/text.cpp`（`typeParts` 的 `Array` 分支、mangle 分支）重写；
  `docs/language` 与诊断文本随之。

### 2.7 语法与 AST

语法（`src/parse/syntax/parser_type.cpp:185` 重写，`docs/language/08_top_level_and_grammar.md` 与
`docs/language/en/` 同步，`check-grammar` 生成）：

```
arrayType : specType dimGroup* ;
dimGroup  : '[' ']'                          // 即 `?`（今天 T[] 的写法，语义不变）
          | '[' extent (',' extent)* ']' ;
extent    : '?' | '..' ident | expr ;        // expr 是线性形式
```

- `..s` 只能作为某一组里**唯一的**维度，且它后面不能再有维度组：`f32[..s]`、`f32[n, ..s]` 合法，
  `f32[..s, 3]` 报错（新诊断 `DimensionAfterAnyRank`）。
- AST：`ArrayTypeLoad`（`src/parse/ast/type.cpp:103`）改为携带 `std::vector<ExtentLoad>`；
  一次 `[n, m]` 产生**一个**节点（今天一个 `[]` 一层）。`T[][]` 仍是嵌套（`T[?][]`）。
- 格式化器：`[n, m]`、`[..s]` 原样输出；`?` 输出成 `[]`（与今天 `T[]` 的写法一致，不再出现 `?`）。
- `node_kinds.def`、语法快照、`docs/language` 的 4 处文本。
- `Tensor<f32, [n, m]>`（设计稿 2.1 的长写法，不带 `const`）：`parseSpecType` 增加"以 `[` 起头的
  实参是形状"（见 2.9 与 Q6）。

### 2.8 维度绑定规则（在 H15 之上加，不重做）

**保留不动**：`ExtentForm`（`?`/常数/线性、`bind`、`evaluate`、`operator==`）、`ExtentValues`、
`visitExtent` 的线性运算、调用点的 `withExtentValues` 与 `DimensionMismatch`
（`src/compile/gir/builder_operators.cpp:1140–1219`）、`DimensionNameAlreadyBound` /
`DimensionNameNotBound` 两种诊断。

**改**：

1. **自动绑定**：形参类型（含 `with` 形参）里首次出现、且不是常量名的标识符，在解析签名时绑定
   （`dimNames_` 的插入点从 `ConstAs` 挪到"未绑定的名字"）。作用域 = 该签名；返回类型只能引用。
   这就替掉了 `constas`（`src/compile/gct/builder.cpp:3810–3827` 的绑定分支与
   `dimBindAllowed_` 的"只有 constas 能绑"语义删除）。
2. **只出现一次警告**：新警告 `DimensionNameUsedOnce`（"`n` 在签名里只出现一次；如果这里只是长度
   未知，写 `?`"）。诊断种类 `0x000144`，`docs/diagnostics.md` 重新生成。
3. **`?`**：不绑定、无名字的未知 extent（今天内部就是 `-1`/`Unknown`）。
4. **`..s`**：任意秩（`anyRank_`），见 2.7。
5. **线性运算下沉**：`builder.cpp` 里 `visitExtent` 的缩放/加减函数搬到
   `core/type/extent.h`（`ExtentForm::scaled/add/sub`），建图器只调用（一处事实）。
6. **校验时机**：签名解析完立刻做"只出现一次"与"返回类型引用了未绑定名字"；调用点绑定不变。
7. **`constas` 退役**：形状位置写 `constas n` 报新诊断 `ConstasRetired`，提示直接写 `n`
   （关键字保留，5.6 仍列它；见 Q2）。11 个 `named_dims` 用例改写。

### 2.9 `Tensor` 别名怎么落

保留 `modules/tensor` 里的一个**类型函数** `Tensor`（今天 `OtherType` 的 `cloneWithArguments` 就是
这个位置）：

- 实参解析沿用今天的代码（`Tensor<T>`、`Tensor<T, [dims]>`、`Tensor<const [dims]>`、
  `const [-1, 3]` 里的 `-1` 读作 `?`）。
- 解析完只做一件事：**构造核心 `ArrayType`**（`ArrayType::create` 或 `createAnyRank`）。
  因此 `Tensor<f32, [3]>` 与 `f32[3]` 是同一个 `Type *`（指针相同、`equals` 相同）——"别名"落到
  实现上就是这个函数；张量不再有自己的 `typeCode`。
- `Tensor<f32>` → `f32[..s]`：549 处程序写的 `Tensor<f32>` 覆盖 1/2/3 维，映射成 `f32[?]`（只有一
  维）会全部报错，所以必须映射成"任意秩"。
- 删除：`modules/tensor/type.h/.cpp` 的 `TensorType` 类、`asTensorType`（换成 `asArrayType` +
  `isNumericElement`）、`module.cpp` 的 `exportType(Reference("Tensor"), …)`（换成注册类型函数）、
  `entry_args` 与 refiner 两处按 `TensorType::typeCode()` 的注册改按 `TypeCode::Array`。
- **元素类型精确性**：`Tensor<float>` 成为 `float[..s]`，即 f64 存储，不再截断成 f32。这是本任务
  最大的一处用户可见变化，见 Q1。

### 2.10 形状检查搬进核心

今天散在张量模块的三处：

| 今天 | H17 |
|---|---|
| `TensorType::castSlotFrom`（运行时形状/元素类型检查，`type.cpp:269`） | `ArrayType::castSlotFrom` |
| `TensorType::castSafetyFrom`（细化格，`type.cpp:238`） | `ArrayType::castSafetyFrom` |
| `TensorType::equals`/`toString`（形状参与相等与打印） | `ArrayType::equals` + `src/core/type/text.cpp` |

搬完 `modules/tensor/type.cpp` 只剩别名用的实参解析（439 行 → 约 40 行），`modules/tensor/type.h`
只剩 `Tensor` 类型函数的声明与 `isNumericElement`。

### 2.11 多维数组上的 `map`/`filter`/`sort`/`reduce`

形状规则由解析器的 lambda 从实参类型算出（与今天同一套写法：`src/builtin/modules/globals.cpp`
的 `map`/`filter`/`reduce`，`modules/algo/module.cpp:40` 的 `sort` 族，`modules/module/module.cpp`
的 `foreach`）：

| 算子 | 规则（`d` 为第一维） |
|---|---|
| `map<f>(xs: T[d, d1, …])` | `U[d, d1, …]`：形状不变，逐元素 |
| `filter<p>(xs: T[d, d1, …])` | `T[?, d1, …]`：第一维未知，其余保留（保留/丢弃的是第一维的元素） |
| `sort`/`insert_sort`/`quick_sort`/`merge_sort`/`argsort` | `T[d, d1, …]`：按第一维排；rank ≥ 2 时比较的是"第一维的元素"，需要元素可比较（递归比较，今天已有按元素类型检查的前置判断） |
| `sort_by` | 同上，键函数作用于第一维的元素 |
| `reduce<f>(xs, init)` | 按第一维折叠 |
| `len(xs)` | 第一维 |
| `xs[i]` | rank 1：元素；rank ≥ 2：**视图** `T[d1, …]`（共享缓冲区 + 偏移），零拷贝 |
| `slice(start, end)` | 第一维上的视图，零拷贝 |

运行时：`getSlot`/`setSlot` 对紧凑数组按元素类型读写缓冲区；`xs[i]` 走 `Array::view`。
并行：三处 `mark_map_arr`/`mark_filter_arr`/`mark_reduce_arr`（taskflow `:2679/2720/2766`、
nodevm `:880`、fastvm `:598`）的分解单位改成第一维，`elementsIndependent` 与故障语义不变；
一次并行 `map` 里创建/销毁视图 → 2.1 的原子计数（自检要覆盖跨线程 retain/release）。
不变：`T[]` 上的四个算子与今天逐字一致（rank 1 时第一维就是全部）。

### 2.12 原地更新的保证

**判据（三个都满足才原地写）**：

1. **语义上没人看得到旧值**：沿用今天已有的"不共享"机制——`:var/take` + `holdsUnsharedArray`
   + `:op/assn_owned`（`src/builtin/operators/ops.cpp:59–165`、`Builder::inPlaceRebindingOf`）。
   H17 把它从"变量格"扩到"值"：算子在 `OperatorTable` 上声明 `reusesOperand`（一处事实）；
   建图器在 `t = t->relu`、`t += d` 这类重建写法的节点上打 `owned` 标记（与今天 `inPlaceCalls_`
   同一处）。第 1 层规划落地后，这里换成使用者索引，判据与声明不变。
2. **缓冲区唯一**：`buffer->unique()`（计数 1）。视图存在时计数 ≥ 2，自动复制。
3. **归 Camel 且可写**：`buffer->camelOwned()`。导入的 DLPack 缓冲区**永不**原地写——否则改动对
   生产者可见，这就不只是"值不可变"的问题了。

**"计数 1"为什么不够**：计数只数"有几个数组值持有这个缓冲区"。两个 slot 可以指向**同一个数组值**
（`let u = t`），此时计数是 1，但 `u` 能看到 `t` 的旧值。所以判据 1 不可省——它正是今天
`holdsUnsharedArray` 解决的问题（`:var/get` 一读就清标记）。

**实现**：`Array::reusableBuffer(arrayType, extents)`：`unique() && camelOwned() && 形状相同` 时返回
原缓冲区（必要时 `grow`），否则新分配。每个"返回新值"的算子在算之前问一次。

**第一版覆盖**：逐元素一元/二元（`+ - * / relu exp sigmoid …`）、`append`、`sort`/`argsort`、
`reshape`/`slice`/`xs[i]`（视图本来不分配）。其余算子继续"总是新分配"，报告里列清单（见第 8 节）。

### 2.13 打印、相等、克隆、序列化

- **打印统一成一种数组格式**：嵌套方括号 + 11.5 的单一浮点文本（最短往返），取消 `tensor(…)`
  前缀与 `% 7.4g` 补齐（后者与 11.5 的"一种浮点文本"冲突，也与"打印不依赖表示"冲突）。
  影响 21 个文件、约 65 行期望输出（Q3）。
- `equals`：逐元素按元素类型比较（今天 `Array::equals` 的路子）；紧凑数组先比形状再逐元素
  （NaN 规则沿用今天）。
- `clone`：浅克隆 `retain()` 共享缓冲区；深克隆新缓冲区 + `memcpy`。
- `.npy` 与入口参数：读写针对数组值（`numel`/`extent`/`strides` + 元素类型）；`entry_args` 的
  loader/saver 改按 `TypeCode::Array` 注册，于是数字数组成为可读写的入口参数（Q4）。

---

## 3. 数据怎样流动

1. **创建**：`Array::create(allocator, arrayType, extents)` →
   `storageFor(elem)` 决定走 `Buffer::create(bytes)` 还是槽分配 → 值写入。
   现成的调用点（`ones`/`zeros`/`new`/`*_like`/`clone`）只是把 `TensorObject::create` 换成它。
2. **视图**：`reshape`/`slice`/`xs[i]` → `Array::view(source, extents, offset)` → `retain`，不拷贝；
   源数组值死后视图仍有效（缓冲区计数兜住）。
3. **逐元素算子**：`reusableBuffer()` 判定 → 内核读写 `View{data, extents, strides}`（张量内核今天
   收 `TensorObject *`，改成收 `Array *`，访问器同名）→ 结果值（同一个缓冲区或新的）。
4. **DLPack 出**：`retain` → 普通堆的 `DLManagedTensor` → 外部库读 → `deleter` → 计数减 1。
   **DLPack 入**：`adopt` 外部数据（带 deleter）→ 数组值 → 读到死 → `release` → `deleter` 恰好一次。
5. **调用点绑定**：建图期 `withExtentValues` 细化形参类型（今天已如此）；运行期 `castSlotFrom` 在
   "形状未知的数组进入声明了形状的位置"时检查（rank 1 未知 = 不检查，与今天一致）。

---

## 4. 旧结构怎样过渡与删除

| 旧结构 | 新结构 | 什么时候删 | 迁移办法 |
|---|---|---|---|
| `TensorObject`（`modules/tensor/tensor.h:67`，数据在 GC 块里） | 核心 `Array` + `Buffer` | 提交 3 | 44 个文件的 `TensorObject *` 改 `Array *`；`create`/`createView` 换 `Array::create`/`view`；`rank()/dim(i)/numel()/dataAs<T>()/shapeSpan()` 在核心同名保留，改动压到指针类型与工厂 |
| `TensorType`（`modules/tensor/type.h:60`，439 行） | 核心 `ArrayType` + 别名函数 | 提交 7 | 形状/元素类型/运行时检查搬进核心（2.10）；模块侧只剩实参解析 |
| `OtherType::extentForms` / `withExtentValues` 两个虚钩子 | `ArrayType` 的方法 | 提交 7 | 调用点（gct、gir）改成 `ArrayType` 的判断；实施时确认无其它子类 |
| `Array::data()` 直接取槽指针（78 处 / 23 文件） | `getSlot`/`setSlot`/`elementPtr`/`dataAs<T>`；`data()` 只在槽存储或紧凑且元素 8 字节时可用（断言） | 提交 2 | 分族机械改（builtin 算子、stdlib 模块、三个调度器、`Array` 自身的 print/equals/clone）；1/2/4/16 字节元素的取用点改 `elementPtr`/`dataAs<T>` |
| `Array::create(alloc, n)`（29 文件） | `Array::create(alloc, arrayType, extents)` | 提交 2 加新签名，提交 3 删旧签名 | 调用点都已有 `ArrayType *`（`ArrayData` 的声明类型或算子的返回类型） |
| `Array::size()`（含义 = 第一维） | 保持；新增 `numel()` | 不删 | 只改"元素总数"的 10 处（print/equals/clone/大小/npy/DLPack/平面内核） |
| `TensorType::typeCode()` 的两处注册（loader/saver、refiner） | 按 `TypeCode::Array` 注册，钩子可"不接"（Q4） | 提交 7 | 注册点改键；`EntryArgumentLoader` 加"不接这个类型"的返回 |
| `normalizeTensorDType` 的 float→f32 归一化 | 元素类型精确（Q1） | 提交 7 | 内核补 f64；测试给构造算子加 `::<f32>` 或给 `let` 加标注，让字面量按上下文取 f32 |
| `constas` 形状绑定（`gct/builder.cpp:3810`） | 自动绑定 | 提交 6 | 新诊断 `ConstasRetired`；11 个用例改写 |
| `:mark/map_arr` 等签名文本里的 `T[]` | `T[?]`（文本只用于诊断） | 提交 8 | 语义本来由 lambda 定；文本与 `docs/stdlib-reference.md` 同步 |
| `sort` 的 `name_in_place` 静态变体 | 保留（它是判据 1 的实现），判据并入 2.12 | 不删 | 计数判据覆盖"返回值没人接"的情形后，静态变体只剩"显式原地写法"一种用途 |
| 张量打印 `tensor([...], dtype=…)` + `%7.4g` | 数组打印（11.5） | 提交 7 | 21 个文件 ~65 行期望输出改写（Q3） |

---

## 5. 提交顺序

每个提交都能构建、相关测试通过；全套（`npm run test`）在提交 2、3、6、7 之后各跑一次，
基线失败集不得增加（见 6.5）。

| # | 提交 | 内容 | 测试门 |
|---|---|---|---|
| 0 | `docs(handover): the H17 implementation plan` | 本文件 | 文档链接检查 |
| 1 | `feat(core): a numeric buffer with an atomic count and an external release` | `Buffer`、`mm::bufferSpace()`、profiler 一行、延迟释放队列、`std::gc::buffer` 自检 pass | 新 plan `feat/mm/buffers.plan.toml`；`feat/mm` 全绿 |
| 2 | `refactor(core): array elements are read and written through one accessor` | `Array` 两种存储 + `getSlot/setSlot/elementPtr/dataAs`、`data()` 的可用条件；**创建规则暂不变**（全部槽存储）；调用点分族迁到访问器 | 全套（无行为变化） |
| 3 | `refactor(tensor): a tensor is an array value over a buffer` | 数字元素改紧凑创建、张量改用核心数组值与视图、删 `TensorObject`、`.npy`/ONNX/tiles/checkpoint 跟进 | 7 个张量 plan + 全套 |
| 4 | `feat(tensor): DLPack exchange is zero-copy in both directions` | 核心 `dlpack.h`、`toDLPack`/`fromDLPack`、释放回调、自检用例（6.1） | 新 plan `feat/tensor/dlpack.plan.toml`；自检断言指针相同 |
| 5 | `feat(lang): the core array type carries a rank and its extents` | `ArrayType` 加形状/`anyRank`/驻留/`castSafetyFrom`/`castSlotFrom`、打印与 mangle（`T[]` 的拼写不变）；`TensorType` 尚存 | 全套（无可见变化）+ 类型自检 |
| 6 | `feat(lang): T[dims] in the grammar, and dimension names bind where they first appear` | 语法/AST/格式化器/`..s`/自动绑定/只出现一次警告/线性运算下沉/`constas` 退役 | `named_dims` 11 个用例改写；AST 快照；语法文档 |
| 7 | `refactor(tensor): Tensor is an alias of the core array type` | 别名函数、删 `TensorType`、`extentForms` 下沉、re-key 注册、元素类型精确（Q1）、37 个 plan 的类型拼写、21 个文件打印期望 | 张量全套 + `shape_diagnostics`/`shape_types` 重写 |
| 8 | `feat(builtin): map, filter, sort and reduce work along the first dimension` | 四个规则、三个调度器、`array.cml` 的手写 HOF、rank ≥ 2 的 index/slice 视图 | 新 plan `feat/arrays/multidim.plan.toml`；四后端一致 |
| 9 | `feat(core): an array operation that returns a new value runs in place when the buffer is unshared` | 判据 1–3、`OperatorTable::reusesOperand`、各算子接入、分配计数断言 | 新 plan `feat/mm/in_place.plan.toml`；`gc_poison_sweep` |
| 10 | `perf: tensor programs in the performance baseline` | 基线程序 + 前后对比表 + `reports/H17.md` | 基线对比；全套 |

**为什么运行期（3–4）在类型（5–7）之前**：缓冲区与 DLPack 不依赖形状类型，先落地让第 1 项独立可交付；
类型工作的用户可见变化最大（元素类型精确性、打印、别名），放在后面，随时可以停在绿色状态。

---

## 6. 怎样测试

### 6.1 层次

1. **C++ 自检 pass**（照 `std::gc::foreign_resource` 的做法，`src/execute/pass.cpp:245`）：
   `std::gc::buffer` 跑 10 个用例——计数、共享、克隆、`finalize` 后释放、延迟释放队列、
   导出零拷贝、导出的 capsule 在数组值死后仍可读、`deleter` 恰好一次、导入后读值正确、
   导入缓冲区不原地写、跨线程 `retain`/`release`（`std::thread` 起 4 个线程各 10⁵ 次）。
2. **`.cml` 用例 + plan**：`test/cases/…` + `test/plans/feat/…`，行为用例尽量跑四后端
   （`backends = ["nvm", "fvm", "jit", "prl"]`，`backends_agree = ["exit_code","stdout_normalized"]`）。
3. **一致性**：`feat/consistency` 与 `feat/para` 覆盖"多后端同一个程序同一个输出"。
4. **GC 压力**：`feat/mm/gc_poison_sweep.plan.toml`（`CAMEL_GC_POISON=1`）覆盖"视图在源值被搬走/死后
   仍有效"；`gc_soak`、`gc_elder_exhaustion` 覆盖缓冲区的延迟释放不漏。
5. **快照与生成物**：AST 快照（`parsecheck`）、`docs/diagnostics.md`、`docs/language` 语法、
   `check-doc-links`、`check-std-includes`。

### 6.2 新增测试

- `test/plans/feat/mm/buffers.plan.toml`（自检 pass + 引用计数行为）
- `test/plans/feat/tensor/dlpack.plan.toml`（导出→导入→读写、deleter 一次、指针相同）
- `test/plans/feat/mm/in_place.plan.toml`（每种操作一条分配计数断言）
- `test/plans/feat/arrays/multidim.plan.toml`（`f32[2,3]` 的 `map`/`filter`/`sort`/`reduce`/`xs[i]`，
  四后端一致；rank ≥ 2 的 `as` 形状检查）
- `test/plans/feat/arrays/shape_types.plan.toml`（由 `modules/tensor/shape_types` 迁出核心的部分）
- 迁移：`named_dims`（11 用例）、`shape_types`（30+ 条期望）、`shape_diagnostics`、
  21 个文件的打印期望、37 个 plan 文件的类型拼写。

### 6.3 分配计数断言（第 3 项的验收）

**问题**：`test/tools/allocshim/allocshim.c` 只在 Linux/glibc（`LD_PRELOAD`），Windows 上跑不了；
而第 3 项要求"每种操作一条分配计数断言"，必须每个平台都能跑。

**办法**：计数器放 `mm::profiler`（缓冲区分配/释放/字节），并加一个内建算子
`std::mem::buffers()` 返回 `{allocs: int, frees: int, live: int, bytes: int}`（调试设施，
像 `std::gc::*` 那组 pass 一样记进 `docs/stdlib-reference.md` 一行）。用例这样断言：

```camel
let before = std::mem::buffers()
let y = xs->relu          // xs 的缓冲区计数为 1 且归 Camel 所有
let after = std::mem::buffers()
println(after.allocs - before.allocs)   // 0：原地
println(len(y))                          // 形状不变
```

四后端调用同一个 C++ 计数器，所以 `backends_agree` 直接可比。对照用例（`let u = xs` 之后再
`xs = xs->relu`）断言 `1`，证明"有人看得到旧值时复制"。

### 6.4 性能基线的对比

- 方法照 `docs/performance/graph-store-baseline.md`：同一台机器（i7-14700K / 28 逻辑核 / 64 GiB /
  Windows 11）；`std::stats std::null` 编译、`std::opt::inline` 优化、`--timeit` 执行、峰值工作集
  `GetProcessMemoryInfo`；`python3 test/tools/perfbaseline/baseline.py --rounds N --json`。
- **现有 6 个程序里没有张量程序**，H17 要加 5 个（`baseline.py` 的 `PROGRAMS` +
  基线文档的表）：
  `tensor.elementwise`（逐元素链）、`tensor.matmul`（gemm）、`tensor.reshape`（视图与拷贝）、
  `tensor.mlp`（训练一步，固定种子，不依赖仓库外的输入）、`array.append_loop`（`T[]` 的追加循环，
  让"计数 1 原地"在时间上看得到）。
- 判据：小型逻辑程序（`fib.deep`、`alloc_small`、`map_chain`）的时间与峰值内存**不升**
  （多轮中位数，容差 ±3%）；张量程序的峰值内存**下降**（视图零拷贝 + 原地写），时间不升。
- 记录：基线文档加"H17 前 / H17 后"两列，`reports/H17.md` 给表。
- 计划阶段**不跑**重负载（本轮遵守"计划任务不跑测试"的规矩）；实施阶段每个提交只跑相关 plan，
  全套在提交 2、3、6、7 后各一次，基线在提交 1 之前与提交 10 各一次。

### 6.5 基线失败集

开工第一件事：量一次全套，记录实际失败集（H8 报告的 19 项是 2026-10-19 的），写进 `reports/H17.md`；
完成后逐项对比，**不得增加**。

---

## 7. 拿不准的地方

三个最大的风险先说：

- **R1 元素类型精确性（Q1）**：它决定第 2 项能不能成立（`Tensor<float>` 与 `float[..s]` 是不是同一个
  类型），牵动 f64 内核与 99 个张量用例。
- **R2 零拷贝的所有权边界（2.5）**：两个方向、`deleter` 可能在运行时退出后跑、外部缓冲区不得原地写。
- **R3 原地更新的判据（2.12）**："计数 1"不等于"没人看得到旧值"，必须叠加既有的"不共享"标记与
  "归 Camel 所有"。

### Q1：元素类型是否精确（`Tensor<float>` = `float[..s]`，还是仍归一化成 f32）

例子：`reshape([1.0, 3.0], [2])` 今天给 `Tensor<f32, [2]>`（f64 字面量被截成 f32）；若元素类型精确，
它是 `float[2]`，`sum` 得 f64。

建议：精确。理由：5.1 禁止隐式数字转换；别名要求 `Tensor<float>` 就是 `float[..s]`，否则"别名"名存
实亡。做法：张量内核补 f64 存储（`dispatchDType` 加一支，模板已经统一）；测试的迁移办法是给第一个
构造算子加 `::<f32>`（或给 `let` 加类型标注），让字面量按 H15 的规则取 f32——这样**期望值基本不变**，
只有少数真的在测 f32 舍入的用例要逐条核对。

影响：不定下来，第 2 项只剩"核心数组类型 + 语法"，`Tensor` 别名做不了，`T[dims]` 与 `Tensor<…>` 会
是两种数组类型（违反"一个事实一处"）。

### Q2：`constas` 是否就此退役（形状位置）

例子：`func f(x: Tensor<f32, const [constas n]>)` → 建议报错"形状位置直接写名字"。

建议：退役，新诊断 `ConstasRetired` 指向 `n`；关键字本身保留（5.6 仍列 `constas`）。
影响：会改写 H15 刚合入的 11 个 `named_dims` 用例，需要协调者确认"同车道可以改写自己上一个任务的
用例"。

### Q3：打印格式是否在 H17 统一

例子：今天 `tensor([1.0000, 2.0000], dtype=float32)`（`%7.4g` 补齐）；建议 `[1.0, 2.0]`。

建议：统一（11.5 一种浮点文本；打印不依赖表示）。影响 21 个文件 ~65 行期望值；若要留到后面，
就得保留一个与 11.5 冲突的格式。

### Q4：数字数组是否成为可读写入口参数（`.npy`）

例子：`func main(xs: float[]): int` 今天报"这种类型的值不能写进文件"；按 `TypeCode::Array` 注册
loader/saver 之后就变成 `.npy` 可读写。

建议：是（一处注册，同时服务所有数字数组）。影响：用户可见的扩展；`EntryArgumentLoader` 要能
"不接"，否则会把非数字数组也吞进去。

### Q5：形状检查的诊断名字（`TensorDimensionMismatch` → ?）

建议：保留名字（运行时错误种类已公开，`0x000009`），只改消息里的类型拼写。
影响：`docs/diagnostics.md` 与 `shape_diagnostics` 用例。

### Q6：`Tensor<?, [4]>`（元素类型通配）怎么办

建议：核心类型里"元素未定"用 `_` 表示（`_[4]`），但**签名里不允许写**——它只是字面量
`[1, 2, 3]` 在元素类型定下来之前的占位。`shape_types.plan.toml` 里 2 条期望改写成
`_[4]`（或改用 `as` 的写法）。

影响：与 Q1 同一条线；需要语言作者确认"签名里能否写元素未定的数组"。

### Q7：`Tensor<f32, const [-1, 3]>` 的兼容读法

建议：`-1` 读作 `?`（H15 之前的未知标记），18 处改写成 `f32[?, 3]`；若要求"负数一律报错"，
同样改写成 `?`。

影响：只影响这 18 处与 `shape_types` 的文本。

### Q8：分配计数的观察面用 `std::mem::buffers()` 可以吗

建议：可以（调试设施，四后端共用同一计数器，`docs/stdlib-reference.md` 记一行）。
备选是 `CAMEL_BUFFER_STATS=1` 退出时打印一次——粒度不够（拿不到"某个算子之后"的数）。
影响：没有它，第 3 项无法按任务书"以分配计数断言覆盖每种操作"验收。

### Q9：常量数组是否自动细化形状（`let xs = [1, 2, 3]` 的静态类型）

建议：**不细化**，仍是 `int[]`。否则 `xs = xs->append<4>` 会因为"对 `T[3]` 追加"变成编译错误，
破坏"现有 `T[]` 行为不变"。形状细化只发生在显式写法（签名、`as`、张量构造算子的推断）。
影响："`append` 只用于 `T[]`"这条规则的可实现性（不细化时它就是今天的行为）。

---

## 8. 规模估计与可以裁掉的部分

| 提交 | 主要文件数 | 估计 |
|---|---|---|
| 1 缓冲区 | 8 | +600 行 |
| 2 访问器 | ~45 | +700 −500（机械） |
| 3 张量改为数组值 | ~30 | +900 −1200（删 `TensorObject`） |
| 4 DLPack | 6 | +450 |
| 5 核心数组类型 | 12 | +800 −250 |
| 6 语法与绑定 | 14 | +700 −200 |
| 7 别名 | ~60（含 37 个 plan、99 个用例） | +400 代码 −600 删除；测试 ~1500 行改写 |
| 8 第一维规则 | 8 | +500 |
| 9 原地更新 | ~20 | +600 |
| 10 基线 | 4 | +300 |
| 合计 | | 代码约 +6,000 −3,500；测试改写约 2,000 行；新增 5 个 plan、约 45 个用例 |

**可以裁掉的部分（按先后）**：

1. **第 3 项的广度**：第一版只接逐元素、`append`、`sort`、视图（提交 9 的 60%），其余算子列为跟进。
   机制、声明与断言都在，后加算子只是接线。
2. **非连续视图**：`strides` 只进表示，H17 只产生连续行主序视图（`transpose` 仍拷贝）。
   省提交 8 的一部分；接口不变。
3. **`std::cpp` 的 rank ≥ 2**：保留"定位拒绝"（今天张量在 `std::cpp` 就是拒绝的，
   `src/passes/trans/cpp/cpp_types.cpp:385` 只认 `rt::Array<T>` 的元素类型），不实现多维。
4. **`..s` 与任何秩**：只在"第 2 项整体后移"时才可裁——它与别名同进退，单独裁会让
   `Tensor<f32>` 无家可归（**不建议**）。
5. **元素类型精确（Q1）**：裁掉等于裁掉别名与第 2 项的后半，**不建议**。

**分界**：提交 1–4 是一个完整可交付的"第 1 项 + DLPack"，随时可停；提交 5–8 交付第 2 项；
提交 9 交付第 3 项，提交 10 交付第 4 项。

---

## 9. 附录：要改的文件清单

**核心（新）**

- `include/camel/core/array/buffer.h`、`src/core/array/buffer.cpp`：`Buffer`、`BufferRelease`、`BufferOwner`。
- `include/camel/core/array/dlpack.h`、`src/core/array/dlpack.cpp`：DLPack 结构、`toDLPack`、`fromDLPack`。
- `include/camel/core/array/view.h`：`View{data, extents, strides, elem}`，供内核接收（薄壳，避免每个内核
  自己拆 `Array`）。
- `src/core/mm/buffer_space.cpp`（或并入 `src/core/mm/mm.cpp`）：`mm::bufferSpace()`、上限的 env。
- 测试见 6.2。

**核心（改）**

- `include/camel/core/rtdata/array.h` + 新 `src/core/rtdata/array.cpp`：两种存储、`numel`、视图、`getSlot`/
  `setSlot`、`finalize`、`noteFinalizable`。
- `include/camel/core/type/composite/array.h` + `src/core/type/composite/array.cpp`：形状、`anyRank`、驻留、
  三条类型规则、`extentForms`/`withExtentValues`。
- `include/camel/core/type/extent.h`：`ExtentForm::scaled/add/sub`（从建图器下沉）。
- `include/camel/core/type/other.h`：删两个虚钩子。
- `src/core/type/text.cpp`：数组打印与 mangle。
- `src/core/rtdata/{print,equality,collection}.cpp`：按 `numel` 与元素访问。
- `include/camel/core/mm.h`、`include/camel/core/mm/gen.h`、`src/core/mm/gen.cpp`：延迟释放队列统一到
  `runDeferredForeignReleases`，`finalize` 路径。
- `include/camel/core/mm/profiler.h`、`src/core/mm/profiler.cpp`：缓冲区计数。
- `include/camel/core/error/diagnostics/messages/{semantic,runtime}.*`：`DimensionNameUsedOnce`、
  `ConstasRetired`、`DimensionAfterAnyRank`、`AppendToKnownExtent`、数组形状错误。
- `include/camel/core/operator_table.h`：`reusesOperand` 声明。
- `src/execute/pass.cpp`：`std::gc::buffer` 自检 pass 与别名。

**前端**

- `src/parse/syntax/parser_type.cpp`（`parseArrayType`、`parseSpecType` 的形状实参）、
  `include/camel/parse/syntax/node_kinds.def`、`src/parse/ast/type.cpp`（`ArrayTypeLoad`）、
  `src/parse/ast/builder.cpp`（`visitArrayType`）、`tools/format`（格式化规则）。
- `src/compile/gct/builder.cpp`（`visitArrayType`、`visitExtent`、`dimNames_`、自动绑定、
  `ConstasRetired`、只出现一次警告）、`src/compile/gct/builder.h`。
- `src/compile/gir/builder_operators.cpp`（调用点绑定改 `ArrayType`、就地标记）、
  `src/compile/gir/builder_diagnostics.cpp`（非保留读的标记与原地判据的接线）。

**张量模块**

- `modules/tensor/tensor.{h,cpp}`（改：内核访问器用 `Array`/`View`，删 `TensorObject`）、
  `type.{h,cpp}`（改：只剩别名实参解析）、`module.cpp`（注册类型函数、re-key loader/saver/refiner）、
  `dtype.{h,cpp}`（元素类型映射、删归一化）、`interop.{h,cpp}`（走核心 DLPack）、`npy.{h,cpp}`、
  `ops/*`（创建/视图/逐元素/归约/索引/布局/线性代数/梯度/支持）、`kernels/*`（`View`、f64）、
  `passes/index_eval.cpp`、`passes/fuse.*`。

**其它模块与后端**

- `modules/algo/module.cpp`、`modules/algo/operators.cpp`（按第一维、原地变体）。
- `modules/tiles/*`（`asArrayType` 与数字元素判断）、`modules/tree/src/checkpoint.cpp`、
  `modules/onnx/emitter.cpp`。
- `src/builtin/modules/globals.cpp`（`map`/`filter`/`reduce`/`slice` 的形状规则）、
  `src/builtin/operators/{ops,struct,higher_order}.cpp`、`src/builtin/modules/array.cml`（手写 HOF 的形状）。
- `src/passes/sched/{nodevm,fastvm,taskflow}/*`（三处 `mark_*_arr`、字节码、视图）、
  `src/passes/trans/cpp/*`（`rt::Array<T>` 的拒绝规则）、`src/execute/entry_args.{h,cpp}`（按数组码注册）。
- `modules/tensor/kernels/parallel.cpp`（与 `std::prl` 的计数一致性检查）。

**文档与生成物**

- 本文件；`docs/performance/graph-store-baseline.md`（张量程序 + 前后两列）；
  `docs/language/08_top_level_and_grammar.md` 与 `docs/language/en/` 同名文件；
  `docs/diagnostics.md`（生成）；`docs/stdlib-reference.md`（`std::mem::buffers`、别名的写法）；
  实施完成后 `reports/H17.md`。

**测试（新增/改写）**

- 新增：`test/plans/feat/mm/buffers.plan.toml`、`test/plans/feat/tensor/dlpack.plan.toml`、
  `test/plans/feat/mm/in_place.plan.toml`、`test/plans/feat/arrays/multidim.plan.toml`、
  `test/plans/feat/arrays/shape_types.plan.toml`。
- 改写：`test/plans/feat/modules/tensor/{named_dims,shape_types,shape_diagnostics,compound_assign,
  bounds,shape_inference}.plan.toml`、`test/cases/modules/tensor/**`（99 个用例中约 60 个要加
  `::<f32>` 或类型标注）、`test/plans/feat/mm/gc_poison_sweep.plan.toml`（把新 plan 纳入）、
  `test/tools/perfbaseline/baseline.py`（`PROGRAMS`）。