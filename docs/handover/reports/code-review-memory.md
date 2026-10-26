# 运行时内存的代码评审

范围：`src/core/mm/`（`block.cpp`、`deferred.cpp`、`foreign_handle.cpp`）、`src/core/rtdata/`（`array.cpp`、`view.cpp`）、`include/camel/core/mm/block.h`、`include/camel/core/rtdata/view.h`；原地更新的决定（`src/execute/exec_plan.cpp:440–610 planStorageReuse`、`src/compile/gir/builder_diagnostics.cpp:1063 markNonRetainingReads`、`src/builtin/operators/ops.cpp:49–250` 的 `var` 算子、`set_value.cpp`、`struct.cpp`、`array.cpp` 里的原地形式）；`src/core/array/dlpack.cpp`；`modules/python/operators.cpp` 的 Bytes 采纳。读的是代码，没有编译、没有运行；"失败场景"是从代码推出的，写明"需先写用例确认"的除外。行号是当前树上的。没有逐行审 `gen.cpp`（1575 行的分代收集器）与四个后端各自的槽位复用，下面"没有覆盖到的"一节列出。

## 总评

数据块的计数（原子、`retain/release`、终结器经延迟队列释放）写得干净，`shareForSecondName` 用"第二个名字就是共享同一块的另一个数组对象"来让计数诚实，这个思路是对的。问题在于：**"没有别的观察者"这个证明被三个不同的地方各判各的**（编译期计划、数组自己的计数、各个原地算子自己的条件），三者之间的假设并不一致；其中两处会在代码里读出可见的值语义破坏。

## 严重

### 1. 带引用元素的数组（`string[]`、结构体数组、嵌套数组）的 `xs[i] = v` 原地形式无条件原地写
`set_value.cpp:173`：`const bool ownStorage = array->isSlots() || array->canWriteInPlace();`。而 `Array::canWriteInPlace`（`array.cpp:1007`）对槽数组明确返回 `false`（"`!isCompact()` → false"），`shareForSecondName`（`array.cpp:1019`）也以此为前提——对槽数组直接 `return this`，注释写着"a slot array (canWriteInPlace is false for it) … hands itself out"。也就是说：**第二个名字拿到的是同一个槽数组对象**（不复制、不计数），而原地写的算子却把"槽数组"当成"自己独占"。同文件的 `__append_owned_arr__`（`struct.cpp:575`，只用 `canWriteInPlace()`，槽数组走复制）与 `sort`（`array.cpp:285`）则是对的——三个原地算子对同一问题给出了不同的答案。
计划这一层在这里也帮不上：`planStorageReuse` 对不在本图里声明的单元（`var` 形参、被捕获的单元，`exec_plan.cpp:556–565`）判 `ByArrayCount`，意思是"由数组自己的计数回答"；数组对槽数组的回答是"可以"。
失败场景（需先写用例确认）：
```
func poke(var xs: string[]): void sync { xs[0] = 'z' }
func main(): int sync {
    let a = ['a', 'b']
    var b = a        // :var/new → shareForSecondName(slot array) → 同一个对象
    poke(b)          // ByArrayCount → set_index_in_place → isSlots() → 原地写
    println(a[0])    // 值语义应是 'a'；这里是 'z'
    return 0
}
```
修法：`set_index_in_place` 只问 `canWriteInPlace()`；槽数组要么像紧凑数组一样走"第二个名字复制出一个对象、计数在对象上"（给槽数组也做 `shareForSecondName`），要么在 `ByArrayCount` 时一律复制。同时把"能不能原地"收成**一个**成员函数（`Array::writableInPlace(plan)`），三个算子都调它，剖析的 `arrayCountAnswer` 记录也放在里面（现在 `set_value.cpp:174–176`、`struct.cpp:576–579` 各写一份）。

### 2. `cellEscapes` 的遍历上限让"查不完"变成"没有逃逸"
`exec_plan.cpp:463`：`while (!pending.empty() && seen.size() < 256)`，循环因为 `seen` 到 256 而退出时，`escapes` 仍是初值 `false`，函数返回"不逃逸"，于是 `planStorageReuse` 可能判 `Input`（"证明了没有别的观察者"，不再问计数）。上限本意是限制编译时间，但**越过上限必须保守地当作逃逸**。门节点（GATE）是透明转发的，一个单元后面跟 256 个以上的门（长的 `sync` 函数里每条语句都可能带门）并不罕见。
修法：`if (seen.size() >= 256 && !pending.empty()) escapes = true;`，并用一个具名常量；更好是用已有的用户列表一次性算出每个单元的逃逸位，不做逐次遍历。

### 3. `Array::canWriteInPlace()` 对内联数组恒为 `true`，成立的前提没有任何东西强制
`array.cpp:1008`：`if (block_ == nullptr) return true; // inline: this array object is the storage, so nothing else can see it`。这句话只在"每出现第二个名字就复制内联数据"时才成立，而复制发生在 `shareForSecondName`——只被 `:var/new`、`:var/get`、`:op/assn` 三个算子调用（`ops.cpp:52,220,231,249`）。其他产生第二个持有者的路径不经过它：元组/结构体字段（`(xs, xs)`）、函数实参与返回值、闭包捕获的 `let`、`tee` 这类库里的共享。它们之所以今天没出问题，靠的是"能被原地写的只有 `var` 单元里的值"以及 `markNonRetainingReads` 等一串条件，但这个不变量没有写在任何一处可检查的地方：谁新增一个会把数组放进两个位置的算子，就会静默破坏值语义。
修法：给"数组被第二个持有者看到"一个统一的入口（任何把数组槽写进容器、实参、闭包、返回的地方都过 `shareForSecondName`，或者让内联数组带一个计数），并加一条调试期断言：`set_index_in_place` 的数组在写之前不能被图里另一个节点引用（`StorageReuse::Input` 时用计划里的使用者数核对）。

### 4. DLPack 导出交出可写指针，值语义被外部破坏
`dlpack.cpp:167–171`：导出的 `DLManagedTensor` 指向数组的数据块，块计数加一（所以 Camel 一侧不会再原地写，这点是诚实的），但 NumPy/PyTorch 拿到的张量是**可写的**（旧版 DLPack 没有只读标志），Python 一侧 `a[0] = 1` 就写进了其他 Camel 持有者仍然看到的块里。设计稿把"零拷贝"列为红线，红线的另一半——外部不能改——没有机制。
修法：用 DLPack 1.0 的 `DLManagedTensorVersioned` 并设 `DLPACK_FLAG_BITMASK_READ_ONLY`；对只能给旧接口的消费者导出一份拷贝；在 `python.notes` 里写明，Python 方向 `to_py` 的数组默认只读，要可写的显式要 `copy`。

## 高

### 5. 元素视图是线程局部环里的裸指针，不持有数据块
`view.cpp:43,73–95`、`view.h` 头注释：视图是 128 格线程局部环里的一个 `Struct`/`Tuple` 头加 `bytes` 裸指针，"不是 Camel 的值，不能存"；`reusedStructView`/`reusedTupleView` 更是整个线程**一个**静态格。安全靠两条纪律：存储路径（帧槽、`Struct::set`）物化（`frame.h:230`、`struct.h:126`、`tuple.h:108`），以及来源数组在视图用完前不被回收、不被原地写。
- 视图不 `retain` 块：回调里数组若只被视图可达（`make()[0]`），收集后块被释放，视图悬空。调用方（`map` 的源）通常根住了数组，但这是调用约定，不是类型保证。
- 原地追加会让块 `grow`（数据搬走，旧缓冲被释放，`block.cpp:grow`）。只有 `unique()` 时才会 `grow`，而视图不增加计数，所以**持有视图时数组仍可 `unique`**：回调里通过 `var` 往正在被遍历的数组追加，视图读到已释放的缓冲。
- 并行后端：环是线程局部的，值若在线程间传递（任务结果），B 线程读 A 线程的环格，A 在继续覆盖它。
- `reusedStructView` 的单格：同一线程里同时存在两个槽形元素的视图（`xs[i] == ys[j]` 这类二元算子的两个操作数）会是同一个对象——注释说"一个循环里一次只读一个字段所以够"，二元算子不满足这个前提。需先写用例确认（元素是槽形的纯数据结构体）。
- 发布构建里，传给外部句柄的视图检查被编译掉：`view.h:98` 在 `NDEBUG` 下 `CAMEL_ASSERT_NOT_VIEW` 是 `((void)0)`，`ForeignRootedHandle` 的构造（`foreign_handle.cpp:19`）因此在发布构建里接受一个会被覆盖的视图。
修法：视图带一个 `Block*` 并 `retain`（环格的析构/覆盖时 `release`），或者干脆在越过函数边界处一律物化，不做环；至少把发布构建的断言换成物化（物化是便宜的）。

### 6. `ForeignPinnedHandle` 的成败取决于对象碰巧分配在哪
`foreign_handle.cpp:30–32`：`!allocator.isObjectAddressStable(object)` 就抛 `runtime_error("foreign pinned handle requires an object in a non-moving allocation region")`。年轻代的对象不是稳定的，所以"钉住"一个刚创建的对象会失败，而晚一点（已晋升）就成功——同一段代码是否抛出取决于回收时机。"钉住"本身应当能把对象提升/固定，而不是要求调用者先知道它在哪个区域。
修法：`pin` 内部先晋升到不动区（或用固定块承载），接口永不因位置而失败。

### 7. 延迟释放队列：全局互斥、终结器里分配、失败时在锁内释放
`deferred.cpp`：每个被终结的数据块都在**全局** `std::mutex` 下 `push_back` 到 `std::vector`（并行后端每次小收集会有大量块同时终结，串行化在一把锁上；`vector` 增长在分配器锁内分配）。失败的分支（`catch (...) { release(context); }`）在**分配器锁仍被持有**时直接释放——而这一队列存在的全部理由正是"不要在分配器锁内释放（会死锁/反转锁序，如 Python GIL）"；内存吃紧时（最容易走到这条分支的时候）正好违反它。
修法：用块本身做侵入式链表（`Block` 加一个 `next_` 指针，无锁入栈，不分配、不失败），`runDeferredReleases` 一次取走整条链。

### 8. 程序结束时队列里的释放回调不运行
`runDeferredReleases` 只在"收集即将开始"的三处被调用（头文件注释）。最后一次收集里被终结的块，如果之后不再有收集，其释放回调永远不运行：DLPack 采纳的块（`dlpack.cpp releaseImport`）的生产者 deleter、Python bytes 的 `releasePythonBytes` 不会被调用，生产者一侧的资源（例如设备缓冲）直到进程结束都不释放。进程结束时操作系统回收内存，但外部库的清理钩子没有运行。
修法：运行时关闭时（`Context` 析构的最后一步）显式 `runDeferredReleases()`，并在测试里数 deleter 调用次数。

## 中

### 9. 同一段"建块—拷贝—换块—释放旧块—登记"写了五遍
`array.cpp` 里：`createCompact`（三个分支）、`growCompact`（两次）、`reusableBlock`+`adoptBlock`、`ensureBlock` 都重复：`Block::create(bytes)`、`memcpy(used)`、设 `block_/dataPtr_/offset_/capacity_/view_`、`old->release()`、`noteFinalizable()`、`noteBlockHeld(block)`。其中任何一步漏掉（比如换块后忘了 `view_ = false`，或者忘了 `noteBlockHeld` 让收集器少算字节）就是泄漏或计数偏差，而五份各自演化。`Block::create` 与 `Block::grow` 也各自重复了"取对齐空间—对齐—记账"（`block.cpp:78–95`、`132–160`）。另外 `Block::create` 把 `capacity_` 设成 `bytes`（而不是实际分配的 `usable`），零字节块分配了 1 字节却报告容量 0。
修法：一个私有的 `Array::installBlock(Block*, size_t offset, bool view)` 做后四步；`Block` 里一个 `allocateAligned(bytes)`。

### 10. 并行后端读写竞争没有机制，只有约定
`planStorageReuse` 在**一个线性化**（`plan.topo`）上推理"单元里的值有没有被读过"，而 `std::prl` 按偏序执行（`taskflow.cpp:1978` 同样用 `operatorUriFor`，即同一份计划）。`markNonRetainingReads`（`builder_diagnostics.cpp:1063`）的 `peek` 判定是它能成立的关键：它用**节点编号的大小**（`for (between = id + 1; between < lastUser; ++between)`）近似"在读与最后一个使用者之间"，并依赖"同步链按创建顺序运行这些节点"。这在 `nvm` 上等于顺序，在 `prl` 上只有同步链上的控制边在保证——条件 1、3、5 要求读、使用者都在链上，所以大多数情形是安全的，但"节点编号即创建顺序"在 `rewriteNode`/后续插入节点之后不一定成立（该函数在建图结束时运行，之后的遍可以改图，而 `peek` 已写死）。设计稿 4.3.1 也把"`std::prl` 并行读者与原地写的先后"列为要核对的项，仓库里没有对应用例。
修法：把"读与写之间有顺序边"作为 `peek` 的**必要条件**在计划构建时核验（顺序边可达），不靠编号；加一个 `prl` 的压力用例：多个并行读者（`len(xs)`、`xs[i]`）与同一变量的原地追加交错，用 TSan 或结果核对。

### 11. 计划构建的二次复杂度
`exec_plan.cpp:511–516,525–533`：对每个节点（`idx`）都扫一遍全部循环（`plan.loops`，两次），复杂度 O(节点 × 循环)。`loopEntry[li] = owned` 每个循环拷贝整个 `owned` 向量（O(节点)），总计 O(循环 × 节点)。大图（生成代码、展开后的静态循环）里是编译期的瓶颈。按位置建一个 `begin → 循环列表` 的映射即可。

### 12. 运行时分派类型
- `Array::setSlot/getSlot(…, arrayType)` 在每个元素上按元素类型分派（`array.cpp` 里的 `set<slot_t>`、`get<slot_t>`）；紧凑数组的整块拷贝（`memcpy`）应当是常态，`__slice_sub__`、`__idx_take_arr__`、`scatter*` 都逐元素走槽（见 `code-review-indexing.md`）。
- `appendSlot` 对 `isSlots()` 与紧凑各一支，`growCompact` 内部又按 `block_ == nullptr`、`unique()`、共享三支；每次追加走一遍分支树。

### 13. 自测代码在生产源文件里
`foreign_handle.cpp:45–150`：`CountingResource`、`selfTestDescriptor`、`debugRunForeignResourceSelfTest` 占整个文件的三分之二，随运行时库发布。`view_selftest.cpp` 同理放在 `src/core/rtdata/`。应移到 `test/` 下的单元测试目标。

### 14. 注释叙述历史
- `exec_plan.cpp` 的 `planStorageReuse` 注释："(the runtime fact the cell used to carry as a mark, now decided once, here)"。
- `ops.cpp:49–60` 等处的注释很长，但在讲"为什么现在这样"而不是"规则是什么"（规则只有一句：第二个名字拿到的是共享同一数据的另一个对象）。
- `block.h` 里 "Device blocks get their field filled by the Device lowering later; the field exists now because DLPack carries it"：写着未来；`device_` 字段目前没有任何读者（`grep device()` 在 `src/core`、`modules/tensor` 无命中）。

## 没有覆盖到的（需要另派人或另一轮）
1. `gen.cpp` 的分代收集（晋升、写屏障、卡表、终结器顺序、`claimPacingBytes`/`claimSweepGeneration` 的"在分配器锁下调用"约定是否在所有路径成立）。
2. 四个后端的帧与槽位复用：`fvm` 的寄存器重用与"最后一次使用处释放"的叠加、`jit` 的栈映射（设计稿 5 节写明尚未补齐）、`prl` 的任务帧池（`taskflow_frame_pool.cpp`）。
3. 设备显存的所有权：`Block::device_` 目前只是一个未被读取的字段，`fromDLPack` 只接受 CPU（`dlpack.cpp:177`），所以"数据块拥有设备内存、设备侧原地更新"还没有实现；`Tiles` 的驻留值是否各自持有释放责任需要单独审。
4. `collection*.cpp`（映射与集合的树）的写时复制。

## 缺的测试（按上面的发现）
1. `string[]` 的 `var` 形参原地写不得被另一个名字看到（第 1 条）。
2. 一个单元之后跟 300 个门的函数里，原地更新必须在有别名时复制（第 2 条）。
3. 两个名字经由元组/实参/闭包得到同一个内联数组，再原地写（第 3 条）。
4. 导出到 NumPy 后在 Python 里写，Camel 一侧另一个持有者不变（第 4 条）。
5. 回调中追加并读取元素视图；二元算子的两个槽形元素视图；视图跨线程（第 5 条）。
6. 最后一次收集里终结的外部块，其 deleter 在关闭时被调用（第 8 条）。
7. `prl` 下的并行读者对原地追加（第 10 条）。
