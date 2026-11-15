# `compile.stream.*` 的红不是负载相关的：单跑 40 轮 0 次，全量 7 轮 0 次，红在别族

- 车道 `lane-streamred`，工作树 `D:\Projects\Camel-wt\streamred`，树 `df057ee8a`（`origin/develop`）。
- **每轮同一份二进制**（`build` 只在第一次做，其余 `-NoBuild`，守卫按内容指纹放行；红轮与绿轮 `binary built` 相同）：
  `[provenance] tip df057ee8a8c7526e4e941636e200c0d21635e6b0 | binary built 2026-11-15T03:10:45-05:00 | tree /home/zjwei/camel-work/streamred | tier fast | jobs <1|8>`

## 1. `test/plans/feat/compile/stream.plan.toml` 单跑：40 轮

147 条（102 个 `[[tests]]` 展开）。每轮原文都是 `[SUMMARY] total: 147, pass: 147, fail: 0`。

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

- **`compile.stream.*` 红 0 次 / 40 轮。** 宿主 1 分钟负载 **15.11–27.05**（32 核），两个宽度都在真负载下量。
- 用例墙钟：`-Jobs 1` 10.5–11.3 s；`-Jobs 8` 1.4–1.9 s。

## 2. 全量 `test/plans/feat/compile`，`-Jobs 8`：7 轮（继续在跑）

| 轮 | `[SUMMARY]` 原文 | 红 |
|---|---|---|
| 1 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 2 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 3 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 4 | total: 2490, pass: 2473, fail: 1, known failures: 16 | `ref.boxes_made_per_call_ten.prl` |
| 5 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 6 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |
| 7 | total: 2490, pass: 2474, fail: 0, known failures: 16 | — |

**`compile.stream.*` 在全量里也是 0 次 / 7 轮。** 唯一那条红属于别的族，原文：

```
[FAIL] ref.boxes_made_per_call_ten.prl                       96.36 ms
    [INFO] verification exited 1
    [INFO] verification output: Error: nothing accumulated: the live blocks do not follow the
           calls that are over: FOREIGN_RESOURCES created=11 released=5 live=6 disposed=6 finalized=5
    [stdout] 10 41
    [stdout] FOREIGN_RESOURCES created=11 released=5 live=6 disposed=6 finalized=5
```

## 3. 判定

- 「全量红、单跑绿」这个**形状**在这个 tip 上存在，但承载它的是 `ref`，不是 `stream`。
- `ref` 那一格不是被谁污染的**base rate**：它自己那一格的注释写着 `p≈0.3-0.5 at --jobs 4, ≈0.25 at --jobs 1.
  Do not rerun to close it`（`test/plans/feat/compile/ref.plan.toml`），且 `docs/handover/questions/cc-2026-11-15-can-a-flaky-cell-wear-a-known-fail-mark.cn.md`
  今天在 `--jobs 4` 量到 5 红 / 10 轮、`--jobs 1` 同样红。⇒ 它不需要任何计划前缀来解释自己。
- `stream` 族历史那 8–11 条红有出处：**win-x64 腿的图所有权不变量**（`src/compile/gir/builder_support.h#nodeGraphOf`；
  同一句 GUARD 在 v0.2.40 的树上读作 `builder_support.h(125)`、在 `df057ee8a` 上读作 `builder_support.h(151)`），
  Linux 读已释放内存时"恰好读得到"，从不报警
  （`docs/handover/reviews/2026-11-02-the-generator-reads-a-draft-node-after-its-builder.cn.md`）。

## 4. 二分：**没做**，且原计划不适用

它的触发条件是「全量里 `stream` 红、单跑绿」——47 轮里一次都没出现。替代读数（正在量）：`ref.plan.toml`
单跑 10 轮。若它自己就红，「全量才红」就不需要任何计划前缀；若它单跑全绿，二分才成为必需。

## 5. 若要修，第一个该看的地方

`src/compile/gir/captures.cpp#Builder::captureInto`（`graph->outerGraph() != nullptr`）与
`src/compile/gir/builder_support.h#nodeGraphOf` ＋ `src/compile/gir/draft_graph_builder.cpp#DraftGraphBuilder::fromRef`
（`fromRef` 返回空 = 建图器已析构、而地址和 epoch 都还没被复用）。两条同一个形状：「一个 handle 活过了它的图」，
Windows 的分配器才把它暴露出来。先查 `src/compile/gir/builder.h` 的 `Builder::variables_` 之外哪些 handle 表在跨 body
使用前没有问 `nodeIsCurrent`（今天只有 `builder_state.cpp#cellsOfBody` 与 `#carryOutOf` 问过）。