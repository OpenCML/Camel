# P0-2 的三个数，在回退之后的合并树上重数

树：`origin/develop` @ `d3d9590c2`，工作树 `D:\Projects\Camel-wt\p02n`，分支 `lane-p02n`。

原读数从哪来：`b3d1ea5ea`（"Take back the chain-tail rewrite…"）在正文里写明 **三个数在回退之后没有重数**，
并说"until it is run the three numbers stand as the earlier lane reported them -- a gap, not a reading"。
它同时给出了探针 `D:\Temp\p02-probe-site.patch`（11,172 字节，给五个调用点标号 2/3/4/6/7）。
本页就是把那个缺口补上：**同一棵树、同一个判据**，重数一遍，然后把重数发现的死件删掉。

## 一、`place_nowait`：这个数从没有过对象

```
git grep -n 'place_nowait'                  # 全树 1 处，在 docs
git grep -n 'place_nowait' -- src modules   # 0 处
git log --all --oneline -S'place_nowait'    # 只命中 bde4db933
```

| 位置 | 是什么 |
|---|---|
| `docs/handover/questions/2026-11-03-is-the-unused-sync-block-warning-reachable-at-all.cn.md:49` | 只有这一处，是一句**待办叙述**，不是代码 |

全历史只有一笔含这个字面量：**`bde4db933`**（docs，11-03 17:21）。也就是说 `place_nowait`
**从来不是代码里的标识符**——没有声明、没有调用点、没有注释提到它；它不是被删掉的，是从来没进过 `src/`。

它指代的量是"造出来了、却没有 wait 的 `GATE`"，原读数是 **196 / 3089 places**，只留在一句注释里
（`builder.cpp:48`，`git blame` → **`bd7a12fbe`**）。那句注释是这条链最后的存在痕迹，本页重数后它随
`chainPlaceForNode` 一起删掉了，数留在本页。

## 二、`chainPlaceGates_` / `chainPlaceForNode`：5 处调用点 → 0 处

重数时的读数（删之前）：

| 名字 | 新读数 | 每一处是什么 |
|---|---|---|
| `chainPlaceGates_` | **4 处** | `builder.h:3170` 声明；`builder.h:3172` 注释；`builder.cpp:54` **唯一写点**；`builder_diagnostics.cpp:496` **唯一读点** |
| `chainPlaceForNode` | **3 处** | `builder.h:3174` 声明；`builder.cpp:43` 定义；`builder_diagnostics.cpp:493` 注释。**调用点 0 处** |

原读数：**调用点 5 处**——`b3d1ea5ea` 自己写的是 "this reverts its five chain-tail sites"，探针 patch
给这五处标的号是 `buildBranch`=4、`visitLoopNode`=2、`buildStreamLoop`=3、`readVariableThrough`=6、
`builder_operators`=7。

**这是真的变了，不是测法不同**：回退撤了那五个调用点，却把函数体留了下来（`chainPlaceGates_.insert`
仍在）。于是 `chainPlaceGates_` 的**唯一写点在一段没人调用的代码里**，集合恒空，
`builder_diagnostics.cpp:496` 的 `contains` 恒假 —— 也就是 `inUnusedSyncBlock` 里"走过位门就继续走"
那一支不可达。这就是作者那句"走不到的守卫比没有守卫更糟"的实例：

```
git grep -n 'inUnusedSyncBlock'                # 删前 4 处（定义 + 两个调用 + 一句注释）
git grep -n 'the sync block it is in is not used'   # 删前只在 builder_diagnostics.cpp:540-542
```

## 三、GATE / `:op/keep` 计数（`std::jgir` 打图数节点）

测法（可复算）：探针程序在 `D:\Temp\p02n-probe-{a-chain,b-block,c-loop}.cml`，计数脚本
`D:\Temp\p02n-count.py`，两者都 scp 到主机 `~/p02n-probes/`，用 `~/p02n-probes/p02n-run.sh`
（`CAMEL_HOME=~/camel-work/p02n/out/latest`、`LD_LIBRARY_PATH=~/camel-eval/conda/envs/clang/lib`）
跑 `camel <程序> std::jgir`，把 JSON 里所有 `{"id":…,"kind":…}` 节点走一遍，数 `kind == "GATE"`
与 `label` 尾部为 `keep` 的节点。

| 形状 | 程序 | 节点总数 | `GATE` | `:op/keep` |
|---|---|---|---|---|
| 链式 effect（两个 `sync` 调用串起来） | `p02n-probe-a-chain.cml` | 31 | **2** | **0** |
| 块值（`sync` 体以裸值结尾，值被接走） | `p02n-probe-b-block.cml` | 8 | **1** | **0** |
| 循环体（`loop` 体里带状态和 effect） | `p02n-probe-c-loop.cml` | 22 | **2** | **0** |

`原读数：没有。` `b3d1ea5ea` 只说"三数未重数"，从没落过 GATE 的具体值，所以这三个数只有新读数、
没有并排的旧读数——不是"变了"，是"头一次有"。`:op/keep` 在建图阶段为 0 是预期：它是改写遍
（`rewrite.h` / `generic.cpp`）在删掉一次带效应的取用时才造出来的，建图器不造它。

## 四、按裁删掉的东西（同一笔代码提交）

作者 11-03 裁定（`e44468eaf`）："这个 sync 块没人用"的警告**不可达，删**；
`EffectNotExecuted` 的另一种措辞保留。于是本笔删：

1. `Builder::chainPlaceForNode` 定义与声明、成员 `chainPlaceGates_` 及两处注释——
   **0 个调用点**，唯一写点在它体内，唯一读点在一个恒假判断里。
2. `builder_helpers.h#chainPlaceFor`——它唯一的调用者就是上面那个函数，一并没有调用点。
3. `builder_diagnostics.cpp#inUnusedSyncBlock` 本体与两个调用点，以及它那句
   "the sync block it is in is not used"（`EffectNotExecuted` 与 `FailNotExecuted` 各一处）。
   **保留的原文**：`"nothing depends on it and it is not in a sync block"`（`EffectNotExecuted`）、
   `"nothing needs it and it is not in a sync block"`（`FailNotExecuted`）。

删后：`git grep -n 'inUnusedSyncBlock'` **0 命中**；`git grep -n 'chainPlaceGates_\|chainPlaceForNode\|chainPlaceFor('`
**0 命中**。`git grep -i chainplace` 还剩 `handChainPlaceTo`（`GraphDraft` 的方法）与
`callHasChainPlace`（`draft_inline.cpp` 的局部量）——那是草稿上的另一个东西，与这条链无关，不动。

## 五、同一条裁定点到的 `isChainStatement`

树上**已经没有那份没调用者的第二定义**：`git grep -n 'isChainStatement'` 只有
`graph_edges.cpp:188`（`isChainStatementNode` 定义）与 `:283`（唯一调用）。移除它的是
**`75954d9d8`**（"graph: delete the chain-statement predicate nothing calls"）。本笔不碰。

## 六、闸门（本笔前后同一台主机，`-Tier fast -Jobs 4`）

| 计划 | 删前（`16861ee65` 干净树） | 删后 |
|---|---|---|
| `feat/compile/stream` | 147 pass / 0 fail | **147 / 0** |
| `feat/compile/transforms` | 77 / 2 / 1 known | **77 / 2 / 1 known**（同两条 `transpose_loop_escape.*`） |
| `feat/opt` 一族 | `pass_written.inventory` 红 | 同一格红，其余不变 |

`pass_written.inventory.nvm` 的红是**基龄红，不是本笔的**：在删之前的干净树 `16861ee65` 上
单独跑 `test/plans/feat/opt/effects_kept.plan.toml` 就是同一条 `[FAMILY] RED 2 pinned number(s) moved`。