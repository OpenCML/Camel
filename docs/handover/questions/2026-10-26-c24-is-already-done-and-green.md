# C24 施工方已经做完并全绿了（你那边别重复做）

你在 `238d97fdf` 里写"索引里的 C24（进程级状态按主人登记、按主人注销：GC 根集合、清扫器、导数注册）也由决策者这边做，施工方不做这一组"——**这条来晚了一步**：施工方已经把 C24 做完并跑到全绿了，报告如下，你按这个决定是直接用、还是只挑你要的部分。

- 分支 `handover/process-state`，tip **`d2a9cd1a6`**（相对 develop 31 个提交、每文件一提交、树干净）；全量快层 `total 6737 / pass 6731 / fail 0 / skip 2 / known 4`、`RUN_EXIT=0`、`BUILD_EXIT=0`、`checks 8/8`。
- **做法**：每个 `Context` 各有自己的 root set 与 sweeper（按 owner 键注册/注销：`Context::create` 以 `ctx.get()` 为 owner 注册 `runtimeGraphRoots`/`runtimeGraphSweeper`，`~Context` 注销两者并以 `runDeferredReleases()` 收尾）；`gc_debug`/`profiler` 逐个打印每个 root set 与 sweeper（不再是单槽）。
- **删掉 3 处"后者覆盖前者"**：①单槽 root set／单槽 sweeper（后建的 Context 把先建的挤掉 ⇒ 只扫一个 root set、只跑一个 sweeper）；②`setTangentSpace` 在 `owns==nullptr` 时把同 code 的模块子集项一起 `spaces.clear()`；③每个 Context 重复 `registerBuiltinDerivatives()`（改成 `std::call_once`）。
- **读数（逐字）**：`CONTEXTS before=1 second=1 after=1 alive=1`＋`contexts-1c2contexts-1c3`（`total 2 / pass 0 / fail 2`）→ `before=1 second=2 held=5 released=2 own=1 own_released=0 alive=1`＋`contexts-1-2-3`；STW 让出升级那条 `gave_up=4 collections=8 in_flow=8 capped=1167829 max_wait_ms=509.2`（wall 7.04 s、**无任何升级行**）→ `gave_up=0 collections=64 in_flow=64`（2.66 s），把让出关掉（`--arg blocking_leaves=false`）又能复现 `gave_up=4` 与 `no collection for 3/4 polls in a row` 那条诊断；AD/H1 的 `after_tensor=0`（第二个 Context 抹掉模块切空间）→ `after_tensor=1` 且梯度值不变。
- **一处如实说明**：GC 收集中途抛异常那条（`minorGCUnlocked` 重入改成 `ASSERT(!inGC_)`＋`throw std::logic_error`，`collectAtSafepointUnlocked` 用 try/catch 包住 switch、经 `[[noreturn]] reportFatalCollectionFailure` 打印后 `std::terminate()`）**没有可复现的改前/改后数字**——树里没有任何用例能让收集中途抛异常，要数字得加一个测试注入点，本次没做。
- **交回一件不属 C24 的真缺陷**（施工方另一条车道正在修）：`Module` 强持 `context_ptr_t context_`（`include/camel/core/module/module.h:173`）与 `Context::builtinModules_` **成环** ⇒ 第二个 Context 永不析构（实测 `held=5`＝自身 1＋模块 4：`ExecutorManager::context_`、`GlobalsBuiltinModule::context_`、以及 `globals.cpp:3035/3043` 两个 executor factory lambda），于是 `~Context` 的注销与 deferred drain 只对进程最后一个 Context 生效、`--timeit N` 会泄漏 N 个 Context。
- **未做**（建议留后续）：ME/5–ME/7（视图不持有 Block、`ForeignPinnedHandle` 要求对象已在 elder/large、deferred 队列改无锁栈）。

**需要你一句话**：C24 是你直接用我们这条（我把它合进 develop），还是你那边已有实现、我们这条丢弃？在你说之前**我把它压在队列里不合**。