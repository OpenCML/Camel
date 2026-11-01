# 重写会话为什么会一直"有变化"

> **问什么**：S1 的 `124`（删掉 `builder_control.cpp:2864` 的 BRCH→臂入口 ctrl 边后，`export_loop.cml`
> 在 `std::macro` 下跑不完）已经定位到函数了：不是导出器，也不是"某处在求值一条臂"，是**优化器的运行时
> 重写会话不收敛**。这一页问的是：**让这个会话收敛，靠什么？**
> **堵着谁**：S1 的收尾（这条边删不删）。
> **为什么我不自己定**：这是"每轮都算有变化"的口径问题，改法有两种，代价不同。

## 一、现场（实测，删边的树，`-vv`，`timeout 25`）

`camel -vv test/cases/modules/onnx/export_loop.cml std::macro`：25 秒内

- `Runtime rewrite session started` **214 次**；
- `Runtime rewrite collected reachable closure with 4 graphs` 428 次，每次解码 `iterate`、`halve`、`model`、
  `__g_52__` 四张图；
- 每轮都 `Graph '…' collected 0 inline candidates`（**0 个**）、`Specialize FUNC … bindings=0`（**0 条**绑定）、
  `Devirtualized`（**0 次**）；
- `constructInPlace … name='__g_52__'` 反复重建同一张图，`__g_52__` 的 FUNC 节点号一轮加 1
  （`2038 → 2039 → …`），即**每轮多一个节点**。

**函数与行**：

| 位置 | 做什么 |
|---|---|
| `src/passes/opt/inline/engine.cpp:1301`-`:1335` `applyOptimizeRewritePass` | 最多跑 `kMaxSessions = 32` 个会话，每个会话返回非空就继续；注释自己写着"the bound only guards against a rewrite that never settles"——**今天就是那个 never settles** |
| `src/passes/opt/inline/engine.cpp:780` `applyRuntimeOptimizeRewrite`、`:802` 的 `while (true)` | 每轮 `collectReachableRuntimeGraphs()`（`:804`）后把整张闭包重新解码、重扫 |
| `src/passes/opt/inline/engine.cpp:1124` `if (!roundChanged) break;` | 会话内退出的唯一条件 |
| `src/passes/opt/inline/engine.cpp:919`-`:957` | 置 `changed` 的三处：特化（`:927`）、去虚拟化（`:949`）、内联（`:1083`） |

**作者点名的假设（"宏求值器把分支的递归臂当成无条件求值"）：没有成立。** 读数里没有任何一处求值一条臂——
整段是**解码 + 重写**，图从未被执行；每轮的内联候选、特化绑定、去虚拟化全是 0，而图仍在长。所以那条边
今天撑着的不是"要不要算这条臂"，而是"这一轮算不算有变化"。

## 二、两条路的代价

- **甲：让会话真的收敛。** 把"这一轮算什么变化"找出来——三处置 `changed` 的地方今天都没做事，说明
  仍有第四处（或 `commit()` 的编码差异）让图每轮多一个节点；定位到那一处并让它不再改，`kMaxSessions`
  就不再被顶到上限，`124` 自己消失。代价：一笔独立的定位 + 修复，读数判据是"会话数 ≤ 1，且
  `export_loop.cml std::macro` 打出 `exported`"。
- **乙：接受这张图每轮长一个节点，把超时变成一句诊断。** 会话撞上 `kMaxSessions` 时给出明确的
  "这个程序的重写不收敛"而不是挂死。代价：那条边仍然删不掉（`Loop` 还是变 `If`），只是不再挂死。

## 三、我方默认建议

**甲**，而且下一刀很小：**在 `applyRuntimeOptimizeRewrite` 返回 `session.commit()` 之前，打印
`changed`、`appliedInlineCount`、`growthSpent` 和 `program->graphs()` 的节点数**，看是哪一处把它推成
"有变化"——三处日志全是 0，所以答案必然在别处（我怀疑 `commit()` 每次都产出新图，但没有读数，不作数）。

## 请作者答一句

**甲（继续定位那第四处变化，直到会话数 ≤ 1）还是乙（把它变成诊断、先不删边）？**
若选甲，请确认判据是"`export_loop.cml std::macro` 打出 `exported`、会话数 ≤ 1、`ort_loop_loaded_weights`
仍打出 `Loop`"。