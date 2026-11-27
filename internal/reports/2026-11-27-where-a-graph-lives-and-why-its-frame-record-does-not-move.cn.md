# 图住在哪、为什么帧手里那个记录不必跟着搬

`camel/runtime/frame_layout.h` 的理由是「记录是堆上一个对象，收集器不搬它，所以帧可以整场持有它的
指针」。这一页把这句话底下的两个事实写成代码依据：**图本身住在哪**，以及**记录里缓存的两个
`TupleType` 会不会搬**。

## 一、图住在 `graphSpace()`，收集器的两次搬动不碰它

`GCGraph` 不是从自动堆里取的：

```cpp
// src/kernel/runtime/graph.cpp:2679-2688（GCGraphBuildAccess::create）
void *mem = mm::graphSpace().alloc(bytes, alignof(GCGraph));
return constructInPlace(mem, bytes, …);
```

`mm::graphSpace()` 是一个 `GrowableFreeListAllocator`（`src/kernel/core/mm/mm.cpp:331`），不是分代收集器
那两个区。收集器按区判断对象归属（`src/kernel/core/mm/gen.cpp:162`：`graphSpace().contains(object) ||
permSpace().contains(object)`）；追踪到图时，它对 graphSpace **只遍历、不搬**：

```cpp
// src/kernel/core/mm/gen.cpp:1533 / 1545
if (marked != nullptr && graphSpace().isAllocated(marked)) { … }
graphSpace().iterateAllocated([&survivors](ObjectHeader *header) { … });
```

⇒ 年轻代/老年代的两次搬动（`gen.cpp:1360-1363` 里 `memcpy` 之后调 `onMoved()` 的那条路）**不会移动图**。
所以 `GCGraph::onMoved()` 里那句 `frameLayout_->graph = this;`（`src/kernel/include/camel/runtime/graph.h:1138-1147`）
是那条不变量的守门人，而不是今天一定会走到的活跃路径。它仍然必须在：它是「图一旦被搬到别处，帧手里的记录
跟着改」的唯一一处。

## 二、记录里缓存的两个 `TupleType` 都不搬

`FrameLayout::runtimeDataType` 指向的那个 `TupleType` 是**就地造在图自己那块内存里的**：

```cpp
// src/kernel/runtime/graph.cpp:2831-2837
// The frame layout is part of the graph (see TupleType::constructFrameLayout); the padding
// planned for it lets it start aligned wherever the arrays before it ended.
const size_t layoutBytes = TupleType::frameLayoutBytes(*frameSlots);
void *layoutMem          = arena.alloc(layoutBytes, alignof(TupleType));
runtimeDataType          = TupleType::constructFrameLayout(layoutMem, *frameSlots);
```

而这段内存在布局账里就是图的一部分（`graph.cpp:546-549`：`layout.frameLayoutBytes` 计入 `layout.totalBytes`）。
⇒ 它与图同生同灭，住在那块**不被收集器搬动**的 graphSpace 内存里。

`FrameLayout::staticDataType` 是程序类型表里的对象（`staticDataType` 参数来自模块的类型层），也不搬。

⇒ 所以「记录里缓存的两个 `TupleType` 指针要不要在搬动钩子里改」的答案是**不需要**：它们不搬。
`onMoved` 只重写 `graph` 那一个字段，其余字段自图造好之后不再变。

## 三、这条不变量的边界（将来若要动 graphSpace）

上面的答案依赖一个前提：**graphSpace 不压实、不搬对象**。今天没有任何代码搬它（分配器是带空闲链表的
可增长分配器，收集器对它只遍历）。若将来给 graphSpace 加压实，那么要一起走同一个钩子的不只记录里的
`graph`：还有图自持有的内部指针 `GCGraph::runtimeDataType_`（`graph.h:1200`，同样指进那块内存），
以及帧在 `acquire` 时另存的一份 `dynamicAreaType_`（`camel/core/context/frame.h:335`，为热路径留的）。
一句话：**那一钩子的作用域是「指进图那块内存的所有指针」，不是「记录里的那一个」。**