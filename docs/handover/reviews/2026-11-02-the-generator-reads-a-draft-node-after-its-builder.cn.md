# 生成器的图留着一个属于已结束 builder 的草稿节点——Linux 读不到，Windows 崩

**2026-11-02，发布闸门，树 `5b3747492`（v0.2.40 的 tag）。CI 的 win-x64 腿。**

## 问句

`stream.*` 的 11 条用例在 Windows 上崩在同一处不变量——`src/compile/gir/builder_support.h:125`
的 `nodeGraphOf()` 说"这个草稿节点不属于任何 compile graph builder"——而**同一棵树在 Linux 的两条腿全绿**。
该由谁去把那个节点的 owner 找出来，以及在它修好之前，Windows 腿要不要继续红？

## 让它是问句的读数

1. **它是真崩，不是宿主抖动**：CI 的 win-x64 腿 11 条（`stream.generators_shapes_consumed.{fvm,jit,nvm,prl}`、
   `stream.generator_loop_two_states.{fvm,jit,nvm,prl}`、`stream.generator_loop_two_states_gir`、
   `stream_fib.default.{nvm,prl}`），退出码 3；本机同一棵树同样复现：

   ```
   Camel Invariant Violation:
       Location : src/compile/gir\builder_support.h(125)
       Function : DraftGraphBuilder *nodeGraphOf(node_handle_t)
       Guarantee: builder != nullptr
       Reading  : Draft node is not owned by a compile graph builder.
   ```

2. **不是调度器的错**：`test/cases/compile/stream/generators_shapes_consumed.cml` 在
   `std::fvm / std::nvm / std::prl / std::jit / std::gir` **五个后端上全部崩**，`std::gir` 根本不执行程序。
   ⇒ 崩在建图，不在跑图。`generators_loop_two_states.cml` 上 `std::fvm` 能过（打出 `1021 / 0`）、`std::nvm` 崩，
   说明还取决于后面哪个 pass 先碰到那个节点。
3. **回归窗口是 v0.2.39**：CI 的 win-x64 腿 `stream` 失败数 **v0.2.38 = 0、v0.2.39 = 10、v0.2.40 = 11**，
   生成器→状态机 lowering 落在这段窗口里。
4. **不变量本身很老，判它变严的一条很近**：`fce5e4218`（10-14，"build all modules into one root graph"）
   就是这句话的来源；`c12eb2281`（10-31 19:37，"Make the compiler's graph checks live"）把检查从只在
   Debug 生效变成常驻——它在 0.2.40 的窗口里，所以只解释"0.2.40 为什么还红"，不解释 0.2.39 的 10 条。
5. **Linux 绿不等于对**：这是一个读已释放对象的问题，Linux 上那块内存还能读，于是值恰好是对的。
   Windows 上它被填成了别的 builder 的地址，`!= nullptr` 就直接判死。**Linux 腿永远不会替我们报警。**

## 三条路与代价

| 路 | 代价 |
| --- | --- |
| **A. 找出那个节点的 owner 并让它活到最后**（推荐） | 要读生成器 lowering 怎么分段建草稿图：若是"临时 builder 建完、把 handle 塞进另一个图"，就是所有权问题，改动小；若是 builder 被提前销毁，要改的是它的生命周期。**先决条件是一条确定的复现**（见下） |
| **B. 在 Linux 上用 ASan/UBSan 复现，再按栈修** | `ci.yml` 已有 ASan+UBSan 腿，但它只跑 `smoke`；要让这两条用例进那条腿（或在那台 Linux 主机上单独跑一次）才能拿到栈。**这是把"只在 Windows 崩"变成"确定栈"的最省事一步** |
| **C. 11 条标 `known_fail` 撑到修好** | Windows 腿因此变绿，但**一个真缺陷被记成债务**；`known_fail_note` 会写清理由，Linux 腿照常绿，所以这只会让"腿绿"失去信息量。只适合限时兜底 |

## 默认建议

先走 **B**：用 ASan Debug 构建跑 `generators_shapes_consumed.cml` 与 `generators_loop_two_states.cml`
（都用 `std::nvm`，那是本机必崩的后端）。拿到栈就是 A 的入口。在拿到栈之前，**不要**把 11 条标成
`known_fail`：这张页存在的意义正是"腿绿不等于树对"，标掉它等于把这句话反着写。

## 结案：11 条在必崩守卫下全绿（2026-11-02 21:55，`lane-draftown` @ `f5a455f62`）

认图的那一笔是 `3609d015a`（草稿节点按 `(地址, epoch)` 认图，不再按回收地址），见证用例是
`test/plans/feat/runtime/draft_node_owner.plan.toml`。判据不是"Linux 绿"——那正是这张页不信任的东西——
而是"守卫必崩的前提下仍然绿"。**`nodeGraphOf()` 的 `GUARD(builder != nullptr, ...)` 本来就是常驻的**
（`include/camel/utils/assert.h#GUARD` 没有 `NDEBUG` 分支，失败走 `[[noreturn]] fail_invariant`），
它不返回 null，也不只是报诊断，所以没有"把它改严一点再试"的余地：**它今天已经是"问到没有答案就死"。**

读数取自 Debug 构建（`ASSERT` 与 safepoint 草稿根不变量都开着）：

- `test/plans/feat/compile/stream.plan.toml`：9 条全 `[PASS]` —— `stream.generator_loop_two_states.{nvm,fvm,jit,prl}`、
  `stream.generator_loop_two_states_gir`、`stream.generators_shapes_consumed.{nvm,fvm,jit,prl}`；
- `test/plans/feat/showcase_s1.plan.toml`：`stream_fib.default.nvm`、`stream_fib.default.prl` 两条 `[PASS]`。

**11 条在必崩守卫下全绿，故 Linux 的绿不是运气。** 修复后这个形状不是"地址被复用、恰好读到对的图"，
而是那个 handle 根本不会带着旧 epoch 被解引用：epoch 对不上就在 `nodeDraftOf` 停下，并打印两个 epoch 与地址
（本线上一次实测：`... made in epoch 48 at 0x55e420207380, and that address now holds epoch 49`）。

**未跑，两条腿，理由是硬的**：Windows 腿——本地禁止构建（蓝屏），远端 wrapper 只跑 Linux 计划；
ASan 腿——`camel-remote.ps1` 只有 `Release`/`Debug` 两种 `-Config`，没有 sanitizer 配置，`ci.yml` 的 ASan 腿只跑 `smoke`。
所以"Windows 上不再崩"是由"必崩守卫下不崩"推出来的，不是直接读数。

**顺带（与本条无关，Debug 腿新发现）**：同一张 `stream` 计划在 Debug 下有 25 条红，全部崩在
`std::vector<GCRegion>::operator[]` 越界，`GraphDraft::region()` 拿到空 regions 的 `id=0`，
调用点是 `src/compile/gir/builder_linearity.cpp#settleStreamLinearity`。这是 Debug 才有的既有缺陷
（Release 的 `vector::operator[]` 不做边界检查），与草稿节点的身份无关，归到别处修。
