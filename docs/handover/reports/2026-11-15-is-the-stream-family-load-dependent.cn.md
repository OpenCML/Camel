# `compile.stream.*` 的红不是负载相关的；缺陷形状是「句柄活过了它的建图器」

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
墙钟 `-Jobs 1` 10.5–11.3 s、`-Jobs 8` 1.4–1.9 s。全量 `feat/compile -Jobs 8` 另 7 轮，stream 族仍 0 次；
唯一红是 `ref.boxes_made_per_call_ten.prl`（1/7），该计划单跑 `-Jobs 8` 0 红 / 10 轮 —— 两个样本分不开，
不足以判它被谁污染，故未做二分；它注释里的 `p` 量自另一份二进制 `50fdda557c`，搬不过来。那条红的原文：

```
[FAIL] ref.boxes_made_per_call_ten.prl                       96.36 ms
    [INFO] verification output: Error: nothing accumulated: the live blocks do not follow the
           calls that are over: FOREIGN_RESOURCES created=11 released=5 live=6 disposed=6 finalized=5
    [stdout] 10 41
```

## 2. 结论：**甲**——有缺陷，形状是「建图器先析构、句柄后使用」

判据（从历史失败点出发能否构造该调用序）答案是**能**，两条证据都在树里：

- **生产代码自述**：`builder_diagnostics.cpp` 的 `syncedReads_` 循环写着「a synced read names the graph it was made
  in, and that graph is handed over when its body is complete and **released with its builder**」，随后必须用
  `DraftGraphBuilder::fromRef(read) == nullptr` 把它**跳过**。若「建图器必然活过句柄」成立，这段跳过与这句注释都是多余的。
- **判据用例已在宿主上跑绿**：`runtime.draft_node_owner.stale_handle_is_refused` 先 `builderA.reset()` 再把句柄拿去问
  （`[PASS] 67.33 ms`，本页 tip），断言要求打出 `Refused: handle made in epoch N at 0x…, and the address now holds epoch N`。
  「建图器析构后使用句柄」这条路是**通的**，只是没有人把它当成缺陷去修。

## 3. 那个 `(125)` 与 `(151)` 是源码行号，不改变上面的判定

- `GUARD` → `cml::fail_invariant`，报告里的 `Location : <file>(<line>)` 取自 `std::source_location::line()`
  （`src/utils/assert.cpp#fail_invariant`）；`include/camel/utils/assert.h` 写明它是 "the same report and the same
  abort as an `ASSERT`, in every build"，**没有 `NDEBUG` 分支** —— 宿主跑的是同一个检查，不是 Windows 才有。
- 同一句 GUARD：v0.2.40 的树上它在 `builder_support.h` 第 125 行、是 `nodeGraphOf` 的第 3 行；`df057ee8a` 上第 151 行
  —— epoch 修复把 `nodeDraftOf` 及其注释写长了 26 行。**一个位置，行号移动**：既不是节点数，也不是两次读数。

## 4. 两种形状各自是什么（读码）

- 第 151 行：`DraftGraphBuilder::draft()` 是 `transaction_->edit()`，草稿归事务 —— 所以这是**草稿还活着、建图器已析构**
  （析构里 `draftOwners().erase(&draft())`）。
- `nodeDraftOf` 的 epoch 拒绝：草稿本身已销毁、地址被后来的草稿占用。两条共一句：**一个 handle 活过了它的所有者**，
  而 `DraftGraphBuilder::fromRef` 判定时**无条件**读 `node.draft->epoch()` —— 这一读就是本缺陷所在。

## 5. 若要修，第一个该看的地方

`src/compile/gir/draft_graph_builder.cpp#DraftGraphBuilder::fromRef`：它用「读已释放草稿的 epoch」来回答「这个句柄还归谁」，
把登记改成不依赖被读对象（把 epoch 存进 `draftOwners()` 的值里，或让 `Builder::syncedReads_` 在 body 交接时清空）。
其次 `src/compile/gir/captures.cpp#Builder::captureInto`（`graph->outerGraph() != nullptr`，同一形状）；
`src/compile/gir/builder.h` 的 `Builder::variables_` 之外，哪些 handle 表跨 body 使用前没问 `nodeIsCurrent`
（今天只有 `builder_state.cpp#cellsOfBody` 与 `#carryOutOf` 问过）。