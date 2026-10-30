> 已由作者答复，裁定原文见 `docs/handover/reviews/questions-2026-10-29.md`（落笔提交 `97654e650`）；本页留档，不再更新。

# 问题：环的守卫走 5 类边，冻结走 4 类——这个差是有意的吗

裁定：C——两处走全部边类、共用一条判据；冻结在同步边上发现环就报 `PassInternalError`；守卫的「静默丢弃会话」另加一个计数，让它在读数上看得见。

## 现象

- `std::opt` 每次改写之后，`src/passes/opt/generic/rewrite.cpp:470` 的 `draftHasCycle` 判刚写出来的
  整图有没有环；调用点在同一个文件 `:552`，成环就丢弃会话、保留原图（这一次优化被静默跳过）。
- 它的遍历走 **5** 类边：`norm` / `with` / `ctrl` / `sync` / `order`（`rewrite.cpp:489`，
  `while (top.group < 5)`）。
- 图交给后端之前的冻结是 `src/runtime/graph_edges.cpp:489` 的 `findCycle`，只走 **4** 类：
  `norm` / `with` / `ctrl` / `order`（`graph_edges.cpp:508`，`while (top.group < 4)`），**不含
  `sync`**。
- 后果：只在 `sync` 边上成的环，守卫会静默丢弃会话（程序照常跑，没有任何诊断），而冻结不会报错；
  反过来，守卫丢掉的 4 类环交给冻结本来也会被拒。也就是说守卫现在比冻结**宽**，宽出来的那一类
  没有对应的诊断。
- 今天两处都碰不到：在 `75e632c8d` 上把守卫那一支拿掉，整套 fast 层仍是 `fail 0`（280 条探针也
  没有一次 `lies on a cycle of inputs`，见
  `docs/handover/reports/opt-cycle-composition-not-found-on-develop.md`）。所以这个差今天不改任何
  用例的行为，只决定「以后加那种改写、写那条用例时按哪一边对齐」。

## 两条路的代价

**A. 有意（守卫更宽，宁可多丢一次会话）**：`sync` 是真实的等待——同步边就是 `sync` 块里语句之间
的先后、链尾连到块的值（`docs/design/effects/edges.cn.md` 第 1 节），所以「环」按 5 类判是对的；
冻结只走 4 类是因为它管的是后端能不能排布这张图（`order` 必须在内），`sync` 上的环由建图与内联
的结构性规则排除。代价：这条不对称没有任何文字记着；「多丢一次会话」也无法从外部观察到（没有计数、
没有诊断），将来有人给冻结加检查时会与守卫的判据打架。

**B. 缺陷（两处应当一致）**：要么冻结也走 `sync`（成环报 `PassInternalError`），要么守卫减到 4 类
与冻结严格一致。代价：若冻结加 `sync`，今天能跑的图可能被拒，需要一次整闸测量，红了的要逐一判是
真环还是判据过严；若守卫减到 4 类，`sync` 上成的环会被写进图并交给后端，而冻结不报，等于把一个
后端排不出顺序的图放过去。

## 已按裁定落地

按 C 做完了，一处判据、三处调用：

- 判环合成一个函数 `camel::runtime::findInputCycle`（`include/camel/runtime/cycle.h`），走哪些边类也只
  列一次（`CycleEdgeKind` 五个值与 `kCycleEdgeKindCount`）。守卫那边读草稿（`DraftCycleEdges`），冻结
  那边读冻结图（`FrozenCycleEdges`，`order`/`sync` 是记录型边，用一块 scratch 转成 span）。原来那两段
  各写各的遍历已删除，所以「把冻结改回 4 类」不再是一处局部改动，而是改共享清单。
- 冻结现在走 `sync`，成环照旧报 `PassInternalError`（`src/runtime/graph_edges.cpp`，`lies on a cycle
  of inputs`）。整闸在新树上跑过：`fail 0`，没有程序在 `sync` 上成环，与作者预期一致。
- 丢弃计数：`GRAPH_STATS` 的 `cycle_dropped_sessions`，在丢弃点那一处加（`recordCycleDroppedSession`），
  由 `std::stats` 读；用例 `test/cases/opt/generic/cycle_drops.cml` 钉住 0，A/B 读数 7 → 0。
- 这个判据现在有两种形态可验：程序侧钉住「今天没有环」，单元侧 `test::edges_sync_cycle` 手工造一对互
  相依赖的 `sync` 边，断言草稿侧找到环且链上含两个节点、提交被拒；把冻结临时退回 4 类时这条用例变红
  （`EDGES accepted`），是它存在的理由。

留下的限制：`cycle_dropped_sessions` 是进程级计数，一次运行里每行 `GRAPH_STATS` 都读同一个数，多个程序
共用一个进程时会累加；`sync` 环仍然没有真实程序能拼出来（只有构造出来的图能验），所以它防的仍是将来。

## 要作者答的一句

环的判据是 **A. 5 类（守卫宽于冻结，保持现状）**、**B. 4 类（两处严格一致，守卫收窄）**，还是
**C. 两处都 5 类（冻结也报 `PassInternalError`）**——圈一个，我按它对齐代码与那条用例。