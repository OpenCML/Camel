# P0-2 的三个数，在回退之后的合并树上重数

树：`origin/develop` @ `d3d9590c2`，工作树 `D:\Projects\Camel-wt\p02n`，分支 `lane-p02n`。

原读数从哪来：`b3d1ea5ea`（"Take back the chain-tail rewrite…"）在正文里写明 **三个数在回退之后没有重数**，
并说"until it is run the three numbers stand as the earlier lane reported them -- a gap, not a reading"。
它同时给出了探针 `D:\Temp\p02-probe-site.patch`（11,172 字节，给五个调用点标号 2/3/4/6/7）。
本页就是把那个缺口补上：**同一棵树、同一条命令、同一个判据**，重数一遍。

## 一、`place_nowait`：src/ 与 modules/ 下 0 处

```
git grep -n 'place_nowait'                  # 全树 1 处，在 docs
git grep -n 'place_nowait' -- src modules   # 0 处
git log --all --oneline -S'place_nowait'    # 只命中 bde4db933
```

| 位置 | 是什么 |
|---|---|
| `docs/handover/questions/2026-11-03-is-the-unused-sync-block-warning-reachable-at-all.cn.md:49` | 只有这一处，是一句**待办叙述**，不是代码 |

全历史只有一笔含这个字面量：**`bde4db933`**（docs，11-03 17:21，"the unused-sync-block warning is
unreachable…"）。也就是说 `place_nowait` **从来不是代码里的标识符**——没有声明、没有调用点、没有注释提到它，
它不是被删掉的，是从来没进过 `src/`。

它指代的量是"**造出来了、却没有 wait 的 `GATE`**"。这个量的原读数在代码里留着一句：
`src/compile/gir/builder.cpp:48`——"196 of this corpus's 3089 places were exactly that"，
`git blame` 指向 **`bd7a12fbe`**（链头守卫那一笔）。**原读数 196 / 3089 places。**

新树上的读法：同一个判据（`lastSyncedNode_ == nullptr` 时直接 `return node`，不造门）仍在
`builder.cpp:49-51`，所以这条路径上"无 wait 的 GATE"是**构造上不可能**的；而"3089 places"这个分母
现在也没有对象了——见第二节，问链位的地方已经是 0 处。**新读数：0 处标识符，0 个可数的 place。**

## 二、`chainPlaceGates_` / `chainPlaceForNode`：5 处调用点 → 0 处

```
git grep -n 'chainPlaceGates_'      # 4 处
git grep -n 'chainPlaceForNode'     # 3 处（其中调用点 0 处）
```

| 名字 | 新读数 | 每一处是什么 |
|---|---|---|
| `chainPlaceGates_` | **4 处** | `builder.h:3170` 声明；`builder.h:3172` 注释；`builder.cpp:54` **唯一写点**；`builder_diagnostics.cpp:496` **唯一读点** |
| `chainPlaceForNode` | **3 处** | `builder.h:3174` 声明；`builder.cpp:43` 定义；`builder_diagnostics.cpp:493` 注释。**调用点 0 处** |

原读数：**调用点 5 处**——`b3d1ea5ea` 自己写的是 "this reverts its five chain-tail sites"，
探针 patch 给这五处标的号是 `buildBranch`=4、`visitLoopNode`=2、`buildStreamLoop`=3、
`readVariableThrough`=6、`builder_operators`=7。

**这是真的变了，不是测法不同**：回退既撤了那五个调用点，又把函数体留了下来（`chainPlaceForNode` 的
`chainPlaceGates_.insert(place)` 仍在 `builder.cpp:54`）。后果是死的，而且可以推：

- `chainPlaceGates_` 的**唯一写点**在 `chainPlaceForNode` 里，而 `chainPlaceForNode` **没有调用点**，
  所以这个集合**永远为空**；
- 于是 `builder_diagnostics.cpp:496` 的 `chainPlaceGates_.contains(...)` **永远为假**，
  `inUnusedSyncBlock` 那一支里"走过位门就继续走"的分支不可达。

**为什么不删**：`b3d1ea5ea` 的正文把这对名字明确列为"保留"——"the two names the old path asks to
decide where a chain's tail may sit … Those are the condition, not the service"——是为下一次尝试留的脚手架。
读数说它现在没有读者，但删它是作者的决定，不是重数这一步该顺手做的。本页只把"没有调用点"记成事实，
把"删或再接线"记成待办。

## 三、GATE 计数（两个程序，`std::jgir` 打图数节点）

见下一笔：本页先落第一、二节的读数，第三节在构建完成后按同一页补。