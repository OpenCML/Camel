# ②的第一块：两个原语，和四个后端共用的那一个写入口

日期：2026-11-15。范围：`lane-refcount`。前置：①（表的描述）与①b/①c（捕获进表、全树平衡）已在
`186bd1700`、`0af49e33f`、`4b572a7b5` 上。

## 1. 这一轮加了什么

`src/core/rtdata/foreign.cpp#retainCountNow` 与 `src/core/rtdata/foreign.cpp#giveBackCountNow`，以及
它们要的 `src/core/rtdata/foreign.cpp#ForeignResourceControlBlock::releaseCountNow`（交回一份并回答
「这是不是最后一份」）。这是作者裁决里「运行时只提供两个原语」的那两个：编译器负责发 dup/drop，
运行时不再猜。**没有任何调用点**：这一轮不动行为，`feat/consistency/values` 144/144、`feat/compile`、
`feat/opt` 与之前逐字节相同。

`release()` 的返回值就是从「交回了几份」到「归零了没有」的全部差别：它本来就知道 `oldCount == 1`，
只是从前不告诉调用者。零释放的时刻因此不需要新的机制，只需要有人问。

## 2. 为什么「一个一个后端改」这件事需要先说清

我原以为四个后端各有一处槽写入，改了可以一个一个量。查下来不是：**槽写入已经是一个共用入口**。

| 后端 | 槽写入的入口 | 位点 |
|---|---|---|
| `nvm` | `camel::core::context::Frame::set` | `src/passes/sched/nodevm/nodevm.cpp` 8 处 `currFrame.set(...)` |
| `fvm` | 同上 | `src/passes/sched/fastvm/fastvm.cpp` 8 处 `currFrame.set(...)` |
| `jit` | 同上（`fastvm` 的编译产物回到同一个 frame 写） | 无自己的槽写 |
| `prl` | 同上 | `taskflow` 走 `taskflow_frame_pool.cpp` 的帧，写仍是 `Frame::set` |

聚合写入同理是 `Tuple::set` / `Struct::set` / `Array::set`（`src/core/rtdata/`）。

所以在 `Frame::set` 里加计数，是**四个后端一起变**，和「一个一个量」直接冲突。三条路：

1. **给 `Frame::set` 一个开关**（帧上一个标记或运行时模式），按后端逐个打开：代码一处，行为逐个放。
   我建议这条，它同时给出「同一个版本里四个后端读数不同」的解释力。
2. 每个后端自己写一份（四份同样的逻辑）：违背「同一入口」，且第 2 处起就会有版本差。
3. 四个一起改：上一版半套计数就是这么红的，红了分不清是哪个后端的哪一处。

## 3. 接上去之前必须先配平的三处（每一处漏一次都是早释放）

计数只在「每个持有者刚好拿一份、刚好交一份」时才是数。写入口一处不漏，还要这三处的对面：

- **帧销毁**：`Frame::set` 拿的那一份，帧结束时谁交？`Frame` 析构 / `FramePool` 回收路径必须逐个
  给出，否则 `live` 只涨不落（安全但不准）。
- **聚合销毁**：元素被 `Tuple::set` 拿的那一份，`Tuple` 死时由谁交。
- **`RefBox` 的根**：`modules/state/ref.h#RefBox` 现在用 `mm::RootHandle` 让内容活着，盒子自己的
  活着靠终结器。这一份是「程序持有」，要算成一份，还是不算，是 ③ 的问题；② 里它必须算**一份固定
  的**，否则盒子的计数永远差一。

**下降比上升危险**：漏交是内存不回收（`live` 偏高，看得见），漏拿是别的持有者交掉之后盒子提前没了
（`live` 偏低，看不见，直到读到野指针）。

## 4. 下一轮的具体动作

1. 在 `Frame` 上加逐后端的开关，默认关；`Frame::set` 对 `TypeCode::Ref` 的槽调用 `retainCountNow`
   （覆盖时先 `giveBackCountNow` 旧的）。
2. 只开 `nvm`，跑 `ref.boxes_made_per_call_ten` 10 轮，读 `live`，与表算出的份数对；不做帧销毁那一半
   就已经能看出「涨到几」——**只开一半时读数必须只涨不落**，落了就是漏拿。
3. 补上帧销毁与聚合销毁的对面，再开 `fvm`、`jit`、`prl`，每个一轮一量。
4. `live <= 3` 这一格②里不动；作者要求「删终结器与根、把宽松断言改回精确值」是 ③。