# 就地更新：两条口谕不能同时成立
2026-11-19 ｜ 来自 `lane-s2-3-inplace-reuse` ｜ 对象：`new_values.sort_rebinding.graph`、`array_sort_kernels.paths`

## ① 两口谕令，隔一天，指向相反

**2026-11-19 18:45（下发，`96a166fbb` 写进 `internal/reviews/questions-2026-10-29.md`）**：

> S2.3 存储复用丢失（`in-place 0 by-count 0`、`sort_d_in_place` 不再出现）：原地更新退了，
> 按回归修，不改期望；查是哪一笔（嫌疑：今天的访问声明改动把排序内核的读写方式改了）。

「不改期望」= `array:sort_d_in_place` 要回到 `std::rgir` 的那张图里，`in-place 0 by-count 0` 要被改掉。

**2026-11-18（答复，`internal/reviews/2026-11-18-an-in-place-call-now-builds-as-ssa-rebinding.cn.md`）**：

> 修法不是把那条前端路接回来……`sort(v)` 处若这是旧 `v` 的最后一次使用，计划选原地形式
> （`StorageReuse::Input`，证明不了就按计数 `ByArrayCount`）。前端的 `inPlaceRebindingOf`、
> `inPlaceCalls_` 随之删除。

同一页还写明「两条红的断言要按新机制改，不是放着」，并归车道 H。一个要记号回来，一个要产记号的
那条路删掉。

## ② 读数：记号是 `1e8e390b6` 拿走的，与今天的访问声明无关

本树 `9db2972af`（`lane-s2-3-inplace-reuse`，Release，`-Jobs 8`）：

- `new_values.sort_rebinding.graph` → `stdout does not contain 'array:sort_d_in_place'`：
  `:var/take`、`array:sort_l_in_place`、`:op/assn_owned` 都还在，只缺 `sort_d` 这一个；
- `array_sort_kernels.paths` → `COPIES main updates 10 in-place 0 by-count 0 declared 0 copied 10`；
- `[SUMMARY] total: 61, pass: 59, fail: 2`（两个用例的四后端 behavior 全绿）。

`in-place 0 by-count 0` 在 `6e4b48362` 就已经是这个读数（`5c96b09d0` 正文自记），所以不是 11-19 引入的。
拿走记号的是 **`1e8e390b6`（S1，`gir: build a same-region local var as a value, not a cell`）**：同一
region 的局部 `var` 不再是格子，`ssaAssignmentOf` 在 `Builder::visitAssignNode` 里先认领 `x = e`，
`Builder::inPlaceRebindingOf` 永远轮不到，`inPlaceCalls_` 为空。该提交正文把这七条写成「S3b 的验收集」。

`fca7f686c`/`7f7fc4eab` 与它无关：两笔都没碰 `src/builtin/operators/array.cpp`（`array:sort_*_in_place`
的注册处），也没碰 `Builder::inPlaceRebindingOf`；改的是访问声明、`requireDeclaredBuiltinAccesses`、
`OperatorSemantics::accessList` 的兜底与 `writesForTheRegion`，都不参与重载解析或 in-place 形式选择。

`sort_l_in_place` 之所以还在，是因为它来自 `sorted_param(var v: int[])` 的 `var` **形参**（仍是格子、
要写回调用方）；`sort_d` 只以局部 `gs` 出现，于是只剩它缺席。

## ③ 两条路与代价

**(甲) 采 11-18**：断言按新机制改（`StorageReuse` 的 `ByArrayCount`，判据是 `camel inspect --copies`
那一行），`array:sort_d_in_place` 不再要求出现在图里；这两条红交车道 H，随 S3b 一起绿。
代价：图里那个记号永久消失，S2.3 在 0.2.45 前不一定收得掉。

**(乙) 采 18:45**：把 `Builder::inPlaceRebindingOf` 的局部 `var` 分支接回来，记号重新产出。
代价：把一条 11-18 已判「要删」的路钉住，而且缺的那个计数仍在——`Array::writableInPlace()` 只看
`block_->unique() && camelOwned()`，看不见另一个活着的名字。lane-sortd 已经量回这条路的结果：
`inspect --copies` 从 `copied 9` 变 `by-count 9`，而 `sort_rebinding.cml` 的 `before` 静默印成
`[1, 2, 3]`（应是 `[3, 1, 2]`），见
`internal/reports/2026-11-17-the-claim-order-is-not-the-root-the-count-is.cn.md`。

## ④ 默认（采甲，本轮按它开工）

**采 (甲)**。11-18 更晚、更具体，且与原则 10.5 一致：就地是执行计划的复用判定，不是前端按语句形状的
改写。一个不该再被产出的记号，让它回来等于把要删的那条路钉住，所以「不改期望」在这里只能读成
「不改**新**期望」。S2.3 的两条红由车道 H + S3b 收口（判据仍是 `camel inspect --copies` 报「原地」），
本车道不改代码、不碰 `src/builtin/operators/array.cpp`。