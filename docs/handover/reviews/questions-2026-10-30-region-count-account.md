# 区域数的那本账：10 是在哪张图上量的

日期：2026-10-30　车道：`lane-eff-rule`　不是求裁，是可核对的一本账

`tiles` 那 22 条期望已按授权改成 **12**（`b27d2e4f2`）。改动前先把账记在这里，方便作者核对：

| 状态 | `TILES_OUTLINED` | 区域成员（`region_execution.cml`） |
|---|---|---|
| base 建图 + **旧**资格 | **10** | 本轮没有逐一点名（base 图未单独导出） |
| 规则 5 笔的建图 + 旧资格 | **5** | `chain{1,2}`、`detour{1}`、`detour{3}`、`shared_values{1,2}`、`constant_axis{3}` |
| 修好的建图 + **旧**资格 | **11** | 上面 5 个 + `main{5}`、`main{8}`、`main{12}`、`main{14}`、`main{17}`、`main{20}` |
| 修好的建图 + **新**资格 | **12** | 上面 11 个 + **`ordered{1,2}`** |

三句话：

1. **12 = 修好的建图 + 新资格。** 同一份旧资格在 base 建图上是 10、在修好的建图上是 11，
   **所以 10 与 12 不是同一张图上的两个读数**——中间那 1 是建图那一半改来的，最后那 1 才是
   资格这一半改来的。
2. 同一张图上，新资格**只多一个**区域：**`ordered{1,2}`，就是 `sync` 函数体
   `y = x + x; return y + y`**——正是 `ordered → host` 那条子句当初为之而写的东西。
3. `main` 那六个单节点区域，是五处 `println(sum(...))` 与 `reshape` 的值节点；旧资格因链边 /
   `Ctrl` 把它们挡在 host 外，新资格放行。

**请确认的记录口径**：重录按"修好的建图 + 新资格 = 12"。若作者认为应按 base 那张图解释（即 10
这个数本身是要守住的结果），这次重录就记错了，我会按他的说法改回来——这一页就是为了让这个偏差
能在他读到之前被看见。

另：`regions.nvm` 里那条 "control endpoints must remain outside GPU regions" 的断言已随本条改成
相反的方向（`ordered` 体成一个两成员的区域，且不再有 `control_edge` 排除），一并记在此。

## 仍未记、故意留红的两条

- `gpu_preference.no_device.prl`：实际输出把 `cpu effect` 排到了最前面（期望
  `false / 5.0 / cpu effect / …`），而 `nvm`、`fvm`、`jit_hot_threshold0` 三个后端与期望一致。
- `new_values.sort_rebinding.prl`：整段零输出（`exit 0`，不是崩溃），三个后端正常。

**两条都只在 `prl` 上出现**，与"并行后端对效应与顺序的处理"同源，应一起定位，不随这本账重录。