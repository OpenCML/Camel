# 不在根里的图版本，谁来更新它的静态区？

**状态**：待裁。两个 poison 红的用例不是两处特例：崩溃的是一张「活着但一直不在任何根里」的图版本，它的静态区带着搬动前的旧地址。本轮没有留下代码改动（试过的一条修法撤掉了，见第 5 节），只交这一页。

## 1. 现象

`test/plans/feat/modules/nn.plan.toml` 的 `gru_lite_large_bytecode`（:1233）与 `residual_mlp_large_bytecode`（:1264），pipeline 都是 `std::opt::specialize std::opt tensor::fuse std::opt::dce std::opt::fold std::macro std::opt::devirtualize std::fvm`：

```
[FAIL] poison:modules.nn:gru_lite_large_bytecode rc=134
[FAIL] poison:modules.nn:residual_mlp_large_bytecode rc=134
```

唯一输出就是判据那一句（`src/core/mm/poison.cpp` 的 `onFault`）：`GC poison: a pointer to a Camel object was used after a safepoint moved the object`。不加 poison 两个用例 `rc=0`、数值正确——poison 只是把静默的旧地址变成可观测的崩溃。

判据语义（`src/core/mm/poison.cpp#onFault`）：每次 safepoint 搬空整个新生代并把 from-space 写成指向 2 MiB `PROT_NONE` 保留区的 poison word；页故障落在保留区就报并 `_exit(134)`。它判的是**「跨 safepoint 留了没刷新的裸指针」**（handle 没重取 / 生命周期没钉住），不是「非法指针」。

## 2. 最小复现（Debug 构建，确定性）

```
camel --gc-check poison test/cases/modules/nn/gru_lite.cml \
      std::opt::specialize std::opt tensor::fuse std::opt::dce std::opt::fold \
      std::macro std::opt::devirtualize std::fvm      # rc=134
```

## 3. 崩溃点

`markObject`（`src/core/mm/gen.cpp:1569`）← `markPhase`（:1448，`objectRootSets_` 那一层）← `collectGraphSpace`（:208）← `GCGraphManager::collect`（`src/runtime/program.cpp:413`）← `collectIfGrown`（:215）← `replaceRootIfCurrent`（:249）← `Context::installRuntimeProgramIfCurrent`（`src/core/context/context.cpp:1040`）← `RuntimeGraphDraftSession::commit`（`src/runtime/draft_session.cpp`）← `MacroRewritePass::apply`（`src/execute/macro/macro.cpp:148`）。

即：`std::macro` 的一轮 commit 装完程序后做**图空间**回收，mark 从被装程序的静态区读到一个已搬走的对象（pointer 指向 poison 区）。

## 4. 实测到的三件事

1. **draft 没漏根**：622 次 `encodeDraftGraph`，draft 的静态槽里过期值 **0 处**（`traceStaticSlots` 的 RootScope 生效）。
2. **每次回收后所有根都是自洽的**：72 次 minor GC，每次结束后按 `objectRootSets_` + `externalRootTracers_` 全量扫一遍静态槽，过期值 **0 处**。
3. **崩的那张图从来没被 trace 过**：唯一一次过期出现在崩溃那一刻，是 `train$spec0`（id=62）的某个版本，77 个静态槽里 11 个已过期；该版本在整个 72 次回收里一次都没出现（被 trace 的 `train$spec0` 是别的版本：6/8/13/15/…槽）。它是 `std::opt::specialize` 造出来的专门化图，静态区里存的是运行期造出来的 `Function` 值（`<model: GruLite>` 各层闭包）。

结论：存在一个**「图版本活着、但不在任何被跟踪的根里」的窗口**。窗口里来一次回收，静态区里那些 `Function`/数组指针就全部过期；等它后来经由某个 `Program` 的 `byId_` 第一次被 mark 到，读的就是旧地址。不加 poison 时旧地址上的内存还没被重用，所以静默读对。

## 5. 试过的一条修法（已撤）

顺着最自然的方向，把 session 的 work table 注册成外根（`RuntimeGraphDraftSession` 里新增 `WorkTableRoots`，`runtime.draftWorkTable`，5 行，`work_` 换表时重指）。确认 tracer 被调用（35 次、每次 9 张图），结果**仍然是 rc=134**。所以「work table 漏根」不是全部：commit 期间新编码出来的图版本先挂在 `committed` 上，`work_` 要到 commit 末尾才换成它，这中间的窗口照样漏。

## 6. 为什么这算通用一类，而不是两个用例

任何 pass 只要在 session 里造出（或沿用）一个静态区里带对象值的图版本，在它被 bind 进某个被跟踪的程序之前都可能过期；`Function`、小数组这类**新生代**对象才搬得动，所以只有把闭包绑进静态区的专门化会踩到——今天踩到的是这两个用例，机制本身对所有 pass 一样。

## 7. 要作者定的

1. **谁负责这段窗口的根**：session 自己（编码完就登记）、还是 `Context`/`GCGraphManager` 的 unbound 表（`noteUnboundRuntimeGraph`，今天只用来「pass 结束后释放」）。
2. **标记前是否允许补跟**：图空间是 mark-only（不搬动），所以「mark 前对所有可达 Program 的图版本做一次 relocate」也能挡住，但那等于把 mark 变成 mark+relocate，代价与不变量都要重新说清——不建议。

**默认建议**：把「session 造出、尚未 bind 进被跟踪程序」的图版本统一交给一个根，覆盖 commit 的 encode→install 窗口（`committed` 自己也算一个根），并在 `docs/technical/36_gc_handle_rules.md` 的审计表里补一条；另加一条**不依赖 poison** 的可观测用例，否则这一类问题只能等 poison 层去发现。

## 默认建议（按作者裁定 486c203e1：按 2026-11-06 那页的 B 继续，本页是前史）

后一半在同目录的 [2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md](2026-11-06-who-roots-a-program-a-pass-holds-while-a-call-runs.cn.md)。

按该页 §「默认建议」执行（按 B 继续，逐个持有者 `pin`/`unpin`）；本页只是它的前史，不再单独推进。作者的裁定原文：「这不是待裁的问题，是内存安全缺陷，优先级排在主线之前。规则只有一条：**一个图版本从它造出来的那一刻起，只要静态区里有对象，就必须能从某个根到达，或者被一个已登记的外根跟踪。**」