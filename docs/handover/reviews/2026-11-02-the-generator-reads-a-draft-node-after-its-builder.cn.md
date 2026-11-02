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

## 补充读数（2026-11-02 12:05，树 `fe7740302` 之上，HEAD `3bdd5ee47` 之后）

**在 HEAD 上，这条在这块主机上复现不出来。** 走的是本页建议的 B 的替代：不拿 ASan 栈，而是给
"所有权丢了"装一个不依赖内存内容的探针——`nodeGraphOf`（`src/compile/gir/builder_support.h:123`）
在查到 builder 之后，再问一句"这个 handle 的 id 在那块草稿里是不是活节点"：

```cpp
if (node.id >= builder->transaction().edit().nodeSlotCount() ||
    !builder->transaction().edit().alive(node.id)) { /* print [PROBE-OWNER] ... */ }
```

**为什么需要这一句**：`DraftGraphBuilder::fromDraft` 只比地址（`draftOwners()` 是
`unordered_map<const GraphDraft *, DraftGraphBuilder *>`，`~DraftGraphBuilder` 时擦掉自己那一行），
所以"查到了 builder"和"这个 handle 的 owner 还活着"不是同一件事——owner 走了以后地址可能被同事务里
下一张图复用，查询就会答成邻居。**"答成邻居"这一半在 Linux 上是可测的，Windows 上的 `!= nullptr`
判死只是另一半。**

读数（同一块主机、`-Tier fast -Jobs 8`，手跑 `./out/latest/bin/camel` 直接看 stderr）：

- 本页点名的两条：`generators_shapes_consumed` 与 `generators_loop_two_states`，各跑 `std::nvm`，
  `[PROBE-OWNER]` **0 条**；
- 把 `test/cases/compile/stream/*.cml` 全部扫一遍 × `std::gir`、`std::nvm` 两个后端：**158 次运行，
  `[PROBE-OWNER]` 0 条**；
- `feat.compile.stream` 整条计划 143/143 绿。

⇒ **能拿到的事实**：在本树上，Linux 这一侧每一步都查到"确实持有那个 id 的 builder"，所以 B 的
替代路径拿不到栈，A 也没有入口。**要么 v0.2.39–0.2.40 之后这条已经不复现（`draftOwners` 与
`~DraftGraphBuilder` 自 `5b3747492` 起没有改过，改的是 `src/compile/gir` 的 52 个提交），要么它只在
Windows 的对象寿命下出现**——而这块主机不允许建 Windows 腿，所以这一条我停在这里，不自造修法。

**顺带一条对"一般规则"的核对**：`docs/technical/36_gc_handle_rules.md` 的 §2 里**没有**一条一般规则说
"对象被造出来的那一刻就要有主"；最接近的是 **第 4 条"An address is not an identity"**，而
`draftOwners()` 正是"拿地址当身份"，只是那张表管的是编译期草稿、这份文档管的是 GC 对象。**若要这条
一般规则同时也管编译期对象，§2 需要补一句**；补不补是作者的判断，我没有改那份文档。

**判据里没做到的两条**：① 复现（上：0/158，本机拿不到）；② "修好后该用例 rc=0"——没有可修的入口，
所以没有改代码，也就没有"修好后"的读数。③ 全量 `fail 0` 在 `3bdd5ee47` 上是成立的，但**与这条无关**
（本次唯一改动是探针加删，src 已还原）。
