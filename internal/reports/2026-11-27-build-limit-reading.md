# 构建按进度设限：一次只构建的读数

一次**只构建**（不带计划）的远端读数，用来验"连续 20 分钟没完成任何一步才停、外限 3 小时"这条新口径。

## 用的是哪个提交

`9f3a4dc31`「camel-remote: the build is bounded on progress, not on its total」—— 新工具本身的来源提交。
先在 `8c3c007f19`（`9f3a4dc31` 随它进 integration 的合并提交）上试过，编不过，见末节。

## 读数

```
BUILD_EXIT=0
BUILD_SECONDS=25.3
[1/693] Building CXX object CMakeFiles/camel_layout_kernel_core_objects.dir/Release/src/kernel/abi/camel_module_abi.cpp.o
…（共 692 条 `[k/693] Building …` 进度行，跑到 693/693）
TREE_STATE=clean stage=after-build
TREE_STATE=clean stage=after-plan
[camel-remote] exit 0 (transfer 3.5 s, wait 0.0 s, build 26.1 s, load (min 1m) 29.86; total 30 s)
```

**没有触发任何停止**：既没有"连续 20 分钟没完成任何一步"，也没有到 3 小时外限 —— 构建 25.3 秒就完成了。
因此"停的时候编译器是否一起停"这一问**看不到侧重**：这一次没有停，日志与进程侧都没有可读的证据。
`[provenance]` 也没有：只构建（不带计划）的跑法本来就不打印那一行。

## 两条保留（读这份读数时一起读）

1. **增量构建**。host 上已有邻近提交的 build 目录，这次只重编了几十个目标（693 个目标里大部分是缓存命中）。
   所以它证明的是"这条口径下构建能跑通并给出 `BUILD_EXIT`"，**不证明**"从零开始的 3 小时外限"行为。
   要证后者，得挑一个 host 上没建过的提交，或 `-Clean`（会丢增量）。
2. **忙机**。load 1 分钟 29.7 → 31.2，期间有 5 个 run 重叠；工具自己在 load > 8 时会警告读数不可比。
   这条读数按"忙机"读，不与其他时段的构建用时相比。

## 顺带核到的一件事：`8c3c007f19` 编不过

```
../src/kernel/include/camel/runtime/draft.h:789:9: error: use of undeclared identifier 'noteGraphWideEdit'
…  10 errors generated.   ninja: build stopped: subcommand failed.
BUILD_SECONDS=9.6   BUILD_EXIT=1
```

`draft.h` 里 10 处调用、0 处声明；定义在 `src/kernel/runtime/draft_edit.cpp:131`。
把声明删掉的是「Take back the graph-header channel that arrived without its callers」那一笔。
integration 上**所有**含 `9f3a4dc31` 的提交（从 `8c3c007f19` 到当时的尖端，逐笔核过 9 笔）都是声明 0 处、调用 10 处；
`9f3a4dc31` 自己声明还在（它在那一笔之前分出），所以这次读数只能取在它上面。

修复是把那 9 行（空行 + 7 行注释 + `void noteGraphWideEdit();`）放回 `draft.h` 的私有区、
`/// The regions the named edit open right now has changed so far…` 这段注释之前 —— 与本报告分开，另在一条枝上。