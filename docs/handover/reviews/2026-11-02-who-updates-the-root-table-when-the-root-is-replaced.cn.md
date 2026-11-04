# 问：宏重写换根时，谁负责更新根表？

> **先纠正一句话，免得下一个人照着改错地方**：派工时说的"在 `replaceRootIfCurrent` 里同步更新根表"
> **已经是事实** —— `src/runtime/program.cpp:237-252` 在那里就先 `current_ = program; rebuildGcRoots();`
> 再出作用域 `collectIfGrown()`。**不必补，补了是重复劳动；要补的是另一处，见下面"实测"。**

`feat/modules/nn.plan.toml` 的两条用例（`poison:modules.nn:gru_lite_large_bytecode`、
`poison:modules.nn:residual_mlp_large_bytecode`）在 `--gc-check poison` 下 abort，rc=134，唯一的消息是

```
[camel] GC poison: a pointer to a Camel object was used after a safepoint moved the object
(a raw pointer was kept across a safepoint; see docs/technical/36_gc_handle_rules.md)
```

**元组这条仪器在这两条上没有任何读数**：为排除 `reportTupleHasNoTupleType` 只对非空类型报的缝，
我在 `Tuple::updateRefs` 里临时加过一条探针（`type == nullptr && size() > 0` 就打印），用同一程序、
同一流水线、同一 `--gc-check poison` 跑：**`[probe]` 行数 0，成员数守卫也没响**（探针已删）。
所以这两条与残差、闭包元组都无关，栈如下（gdb，逐帧原文）：

```
#0 GenerationalAllocatorWithGC::markObject(rtdata::Object*, type::Type const*)
#1 GenerationalAllocatorWithGC::markPhase()
#2 GenerationalAllocatorWithGC::collectGraphSpace()
#3 camel::runtime::GCGraphManager::collectIfGrown()
#4 camel::runtime::GCGraphManager::replaceRootIfCurrent(Program const*, Program const*)
#5 camel::runtime::RuntimeGraphDraftSession::commit(bool)
#6 MacroRewritePass::apply(Program const*, std::ostream&)
```

## 三处原文：根表在哪儿建、换根时改了哪儿

1. **根表是"由五个清单重建"，不是一个可以直接加条目的表**（`src/runtime/program.cpp#GCGraphManager::rebuildGcRoots`）：
   `rebuildGcRoots()` 先 `gcRoots_.clear()`，再依次 `add(current_)`，然后 `detached_`、`pinned_`、
   `provisional_`、`runRoots_` 四个清单。
2. **换根时确实重建了**（`program.cpp:237-252`）：`replaceRootIfCurrent` 在 `NoCollectionScope` 里
   `current_ = program; rebuildGcRoots();`，**然后**出作用域才 `collectIfGrown()`。
   ⇒ **"换根时没更新根表"这个假说不成立，那一处是对的。**
3. **新程序不是在 `reserve()` 被登记的**（`program.cpp:175-187`）：`reserve()` 把它放进 `programs_`，
   只有 `running_ == 0` 时才同时进 `provisional_` 和 `gcRoots_`；**`running_ != 0` 时它只属于
   `programs_`**，注释写的是"由为它做的函数值到达"。而清扫器（`program.cpp:426-442`）把
   **`programs_` 里所有没被标记的程序都释放**，标记集只来自 `gcRoots_`。

## 一句结论

**根表本身没漏，漏的是 `reserve()` 与 `replaceRootIfCurrent()` 之间那段窗口**：在一次运行中造出来的
程序，在成为 `current_` 之前不是根；这段窗口里如果有一次收集落到 safepoint 上，它就被当成死程序释放，
随后 `replaceRootIfCurrent` 把**已释放的指针**装成 `current_` 并加进根表，`collectIfGrown` 的 mark 阶段
拿它去标记 —— 正是上面那 6 帧。（`collectGraphSpace` 只是 `markPhase()` + 清扫，不搬对象，所以
"这一轮搬走了它"不可能，指针在进入这一轮之前就已经死了。）

## 这个结论被实测推翻了两次，以下是实测

作者 2026-11-02 按上面那句裁了 A（`reserve()` 时就登记成根）。落了之后量了一遍，两件事都不成立：

1. **窗口真的存在，但不是红的原因。** 在 `reserve()` 里加探针（`running_ != 0` 就打印），跑
   `gru_lite.cml` + 同一流水线 + `--gc-check poison`：**81 行**（`residual_mlp` 是 **84 行**），
   即一次运行里确实有 81 个程序是在"不是根"的状态下造出来的；**照 A 全登记成根之后，两条用例仍然
   `rc=134`、毒检查那同一句**。⇒ 窗口是真的，**它不解释这两条红**。
2. **也不是"已释放的程序还留在根表里"。** 在 `freeDead` 里加探针：被释放的程序如果还出现在
   `current_`/`provisional_`/`detached_`/`pinned_`/`runRoots_` 任一处就打印 —— 同一程序跑下来
   **0 行**。⇒ 根表里没有已释放的程序。

**⇒ 悬垂的不是根，是根里的一个槽。** `markObject` 自己不查毒；它是沿着一棵活的对象的成员往下走的
（`current->updateRefs(markRef, entry.type)`）。所以毒是**某个活对象的一个成员指针**报的：那个成员指向
的对象在一次 safepoint 上被搬走了，而成员所在的槽没有被重定位。程序与图住在 `mm::graphSpace()`，
图空间的对象不搬（`func.cpp` 的注释），但图里的**静态值**可以指向年轻代对象 —— 于是接下来问的是：
**运行期由宏重写造出来的图，往静态值槽里写一个年轻代对象时，有没有记写屏障？**

## 第三个假设也不必跑探针就否掉了：静态值槽的写本来就过写屏障

**写静态值槽的入口一共三处，全在 `include/camel/core/context/frame.h`**：

| 位置 | 现场 |
|---|---|
| `frame.h:243` | `staticArea_->set<T>(idx, value, staticDataLayout());` |
| `frame.h:380` | `staticArea_->set<T>(idx, value, staticDataType_);` |
| `frame.h:704` | `staticArea_->set<slot_t>(static_cast<size_t>(-dataIdx), value, staticDataType_);` |

三处都走同一个 `::Tuple::set`，而 `Tuple::set` 在写之前就调
`camel::core::mm::writeBarrier(this, tupleType, slot, fieldType)`（`include/camel/core/rtdata/tuple.h`，
`set` 的实现第一行）。也就是说**屏障在共享写入路径上，不是调用方选开**。收集器自己的自测就在验这条：
`gen.cpp:505-530` 断言"一个元组上的 old→young 存储被记进了 remembered set"，验不过就抛
`GC remembered-set self-test did not record old-to-young edge` / `lost the young child reference`。

而且静态区本身也不是"收集器看不见的老年代容器"：`gen.cpp:1381-1385` 的注释写着，一个图的静态区
**既作为图的根被追踪、又通过该图的每个活动帧被追踪**（正因如此同一槽会被访问多次，`forwardYoungRef`
才要挡"复制副本"）。

**⇒ 所以"静态值写漏了屏障"这条也否掉了，不用跑计数探针**：探针只能证明"确实有往年轻代的写"，而这些写
在写入点就已经被记下 —— 那不会把假设变成正解，只是把已知的事再数一遍。

## 下一个问题（留给下一轮，探针很小）

排掉"根"、"根表里的已释放程序"、"静态值槽的写屏障"三样之后，剩下的形状是**一个已释放/已搬走的
`GCGraph *` 还挂在一个活的 `Program` 上**：`markPhase` 从 `gcRoots_` 里拿出一个活程序往下走，程序表里的
某个图指针已经指向被清扫掉的图。可测的问法是一个探针：**在 `GCGraphManager::freeDead` 里，逐个被释放的
图问一句"还有没有活程序的名字表里留着它"**，有就打印，没有就也否掉（就像 `freeDead` 那次 0 行一样干净）。

（探针已全部删除；A 没有提交，因为它没有换来任何一条绿，而"多留 81 个程序到 `settle`"是实打实的内存代价。）

## 两条路（在上一轮的基础上，按实测改了）

**A（仍未证伪，但不再是正解）：`reserve()` 时就登记成根。** 它修的是窗口本身（一次运行里 81 个程序短暂
无主），代价多留一些程序到 `settle`。**它不解释这两条红，别当成修好了。**

**B（作废，已被静态证据否掉）：查静态值的写屏障。** 见上一节：三处写入点都走 `Tuple::set`，屏障在
共享路径上，静态区还被当根追踪。

**B：根表存句柄（`RootHandle`）而不是裸 `Program *`。**
句柄被收集器搬动时自动更新，`gcRoots_` 就不再是"一串裸指针"。**代价**：`Program` 现在住在
`mm::graphSpace()`（`program.cpp:175`），而按 `func.cpp` 的注释，图空间的对象不搬；为一个"十有八九
不搬"的空间给根表换一套表示，是拿核心表示的一条不变式去换一处窗口的保险 —— 应当先做 A。

**为什么不选"在 `replaceRootIfCurrent` 里加一句 `isAllocated` 检查"**：那只会把崩溃换成一句诊断，
程序和图都已经释放，pass 没法继续；它能加，但它是 A 落地之后的一道断言，不是替代。