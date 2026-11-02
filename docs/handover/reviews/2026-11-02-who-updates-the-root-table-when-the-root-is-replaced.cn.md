# 问：宏重写换根时，谁负责更新根表？

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

1. **根表是"由五个清单重建"，不是一个可以直接加条目的表**（`src/runtime/program.cpp:344-357`）：
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

## 两条路

**A（建议）：把新程序在 `reserve()` 就登记成根，不等到它成为 `current_`。**
即 `running_ != 0` 时也进 `provisional_`（它本来就能被 `rebuildGcRoots` 收进来），由
`settle(keep)` 决定：被 `keep` 接住的留下，没被接住的连同它的图一起释放。**代价**：一次运行中造出的
程序要活到该 pass 结束 —— 就是一次重写造出来的那些图，内存上限是这一次重写的产物，不是整个程序。

**B：根表存句柄（`RootHandle`）而不是裸 `Program *`。**
句柄被收集器搬动时自动更新，`gcRoots_` 就不再是"一串裸指针"。**代价**：`Program` 现在住在
`mm::graphSpace()`（`program.cpp:175`），而按 `func.cpp` 的注释，图空间的对象不搬；为一个"十有八九
不搬"的空间给根表换一套表示，是拿核心表示的一条不变式去换一处窗口的保险 —— 应当先做 A。

**为什么不选"在 `replaceRootIfCurrent` 里加一句 `isAllocated` 检查"**：那只会把崩溃换成一句诊断，
程序和图都已经释放，pass 没法继续；它能加，但它是 A 落地之后的一道断言，不是替代。