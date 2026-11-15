# `compile.stream.*` 的红不是负载相关的：单跑 40 轮、全量 7 轮，0 次

- 车道 `lane-streamred`，树 `df057ee8a`（`origin/develop`），工作树 `D:\Projects\Camel-wt\streamred`。
- 全程同一份二进制（`build` 只做一次，其余 `-NoBuild`，内容指纹守卫放行；红轮与绿轮 `binary built` 相同）：
  `[provenance] tip df057ee8a8c7526e4e941636e200c0d21635e6b0 | binary built 2026-11-15T03:10:45-05:00 | tree /home/zjwei/camel-work/streamred | tier fast | jobs <1|8>`

## 1. `test/plans/feat/compile/stream.plan.toml` 单跑 40 轮

| 轮 | `-Jobs 1` | `-Jobs 8` | 轮 | `-Jobs 1` | `-Jobs 8` |
|---|---|---|---|---|---|
| 1 | 绿 f0 | 绿 f0 | 11 | 绿 f0 | 绿 f0 |
| 2 | 绿 f0 | 绿 f0 | 12 | 绿 f0 | 绿 f0 |
| 3 | 绿 f0 | 绿 f0 | 13 | 绿 f0 | 绿 f0 |
| 4 | 绿 f0 | 绿 f0 | 14 | 绿 f0 | 绿 f0 |
| 5 | 绿 f0 | 绿 f0 | 15 | 绿 f0 | 绿 f0 |
| 6 | 绿 f0 | 绿 f0 | 16 | 绿 f0 | 绿 f0 |
| 7 | 绿 f0 | 绿 f0 | 17 | 绿 f0 | 绿 f0 |
| 8 | 绿 f0 | 绿 f0 | 18 | 绿 f0 | 绿 f0 |
| 9 | 绿 f0 | 绿 f0 | 19 | 绿 f0 | 绿 f0 |
| 10 | 绿 f0 | 绿 f0 | 20 | 绿 f0 | 绿 f0 |

**红 0 次 / 40 轮**；每轮原文 `[SUMMARY] total: 147, pass: 147, fail: 0`。宿主 1 分钟负载 15.11–27.05（32 核），
墙钟 `-Jobs 1` 10.5–11.3 s、`-Jobs 8` 1.4–1.9 s。

## 2. 全量 `test/plans/feat/compile`，`-Jobs 8`：7 轮

| 轮 | `[SUMMARY]` | 红 |
|---|---|---|
| 1–3 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 4 | total: 2490, pass: 2473, fail: 1, known failures: 16 | `ref.boxes_made_per_call_ten.prl` |
| 5–7 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |

`compile.stream.*` 在全量里 0 次 / 7 轮。唯一那条红在别族，原文：

```
[FAIL] ref.boxes_made_per_call_ten.prl                       96.36 ms
    [INFO] verification output: Error: nothing accumulated: the live blocks do not follow the
           calls that are over: FOREIGN_RESOURCES created=11 released=5 live=6 disposed=6 finalized=5
    [stdout] 10 41
```

## 3. 判定

- 「全量红、单跑绿」这个形状只出现 1 次（`ref`，全量 r4），**一次观测不足以判它被谁污染**：`ref.plan.toml`
  单跑 `-Jobs 8` **0 红 / 10 轮**（52 条，每轮 `fail: 0`）。两边的 p 都在 0.1 附近，这个样本分不开。
- `ref` 自己的注释写着 `p≈0.3-0.5 at --jobs 4, ≈0.25 at --jobs 1`，那是**另一份二进制**（`50fdda557c`）上的读数；
  不同二进制，p 搬不过来 —— 这正是本页每轮都记 `binary built` 的理由。
- `stream` 族历史那 8–11 条红不是宿主能复现的东西：它们是 win-x64 腿的图所有权不变量
  （`docs/handover/reviews/2026-11-02-the-generator-reads-a-draft-node-after-its-builder.cn.md`）。

## 4. `(125)` 与 `(151)` 是源码行号，不是节点数

- `GUARD` → `cml::fail_invariant`，报告里的 `Location : <file>(<line>)` 取自 `std::source_location::line()`
  （`src/utils/assert.cpp#fail_invariant`）；`include/camel/utils/assert.h` 写明它是 "the same report and the same
  abort as an `ASSERT`, in every build"，**没有 `NDEBUG` 分支** —— 宿主跑的是同一个检查。
- Windows 抓到它的方式与 Linux 相同：打印 Location/Function/Guarantee/Reading 后 `std::abort()`，退出码 3。
- 同一句 GUARD：v0.2.40 的树上它在 `builder_support.h` 第 125 行、是 `nodeGraphOf` 的第 3 行；`df057ee8a` 上第 151 行
  —— epoch 修复把 `nodeDraftOf` 及其注释写长了 26 行。**一个位置，行号移动**，既不是节点数，也不是两次读数。
- 宿主可见：`test/plans/feat/runtime/draft_node_owner.plan.toml` 用合成构造逼出这个条件，
  `[PASS] runtime.draft_node_owner.stale_handle_is_refused 67.33 ms`；它的断言要求宿主打出
  `Refused: handle made in epoch N at 0x…, and the address now holds epoch N`。

## 5. 两种形状各自是什么（读码）

- 第 151 行：`DraftGraphBuilder::draft()` 是 `transaction_->edit()`，草稿归事务 —— 所以这是**草稿还活着、建图器已析构**
  （析构里 `draftOwners().erase(&draft())`），是建图器的生命周期缺陷，不是内存坏掉。
- `nodeDraftOf` 的 epoch 拒绝：草稿本身已销毁、地址被后来的草稿占用 —— 那一次读确实落在已释放内存上；epoch 是让它
  可被判据读出来、而不是静默读错的那一半。两种形状共一句结论：**一个 handle 活过了它的所有者**。

## 6. 若要修，第一个该看的地方

`src/compile/gir/captures.cpp#Builder::captureInto`（`graph->outerGraph() != nullptr`，与上面两句同一形状）与
`src/compile/gir/builder_support.h#nodeGraphOf`；再查 `src/compile/gir/builder.h` 的 `Builder::variables_` 之外哪些
handle 表在跨 body 使用前没有问 `nodeIsCurrent`（今天只有 `builder_state.cpp#cellsOfBody` 与 `#carryOutOf` 问过）。