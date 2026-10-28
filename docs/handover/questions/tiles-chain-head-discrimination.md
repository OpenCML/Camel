# 链首成员：补上之后没有一条用例能分辨新旧

## 我量到的

区域扫描判断"这个节点是不是某块的语句"时，判据是"两侧有没有 control 边"，加上"有没有同步前驱"。这两句查的是同一段：同步前驱正好就是以该节点为 `user` 的边。块的**第一条语句**在链上只有出边、没有入边，所以它从前根本不算语句；末一条算。

于是把判据改成按每条同步边的两端建集：`modules/tiles/src/graph_regions.cpp#chainMembers` 每图遍历一次 `syncEdges()`，`user` 与 `pred` 都放进集合，`modules/tiles/src/graph_regions.cpp#inChain` 只做下标查。`modules/tiles/src/graph_regions.cpp#eligibility` 与聚合投影那处共用这份集合，逐节点扫边随之取消。

这一改动在语料上是安全的：`feat/modules` 全绿（`native_region_invocation` 五条不退化），全量 `total 7475, pass 7458, fail 7`，红名单与改动前同名同数。

## 卡住的地方

按"设备作用域里只有一条语句的块"和"块首语句就是要分段的那一点"各写了一条用例，用同一台机器、同一条命令 `tiles::test_outline_native`，分别在**有修复**与**把源码回退后重建**的两个二进制上跑：

| 用例 | 有修复 | 回退后 |
| --- | --- | --- |
| ① 单语句块 | `TILES_OUTLINED 1` / `2.7206764` | `TILES_OUTLINED 1` / `2.7206764` |
| ② 块首即分段点（张量算子） | `TILES_OUTLINED 2` / `9.998035` | `TILES_OUTLINED 2` / `9.998035` |
| ③ 块首是效应、其后才运算 | `TILES_OUTLINED 2` / `9.998035` | `TILES_OUTLINED 2` / `9.998035` |

③ 是设备作用域里**第一句是 `println(sum(x))`、其后才写 `tanh` 与算术**的块：留主机的那一句正是块首，分段点在它身上。它的驻留报告在两棵树上也一样：

```
TILES_RESIDENCY {"blocks":{"device":0,"host":8},"body":"host",
  "calls":[{"callee":"cut","callee_at":"device","caller":"host","transfers":1}],…}
```

**三种互相独立的形状、同一个二进制对、三次读数逐字节相同**。冻成用例只会锁住一个与这个修复无关的值，所以没有提交。

## 读代码得到的解释

- `modules/tiles/src/graph_regions.cpp#eligibility` 里 `ordered` 只在 `if (ordered && !device) return "control_edge";` 这一句起作用。**设备作用域内 `device` 为真，`ordered` 不影响返回值**，所以三类用例都走不到差异。
- `modules/tiles/src/graph_regions.cpp#analyzeRegions` 里聚合投影那处用 `inChain` 是为了**跳过**（让节点不可外提）。方向同样是"减少可外提"。
- 两处方向都只会减少可外提，与"补上链首成员会让节点从留主机变成被分段"方向相反。此前那次读数（同一个用例的 `TILES_OUTLINED` 从 6 变 10）来自**同步前驱**那一半，不是链首这一半。

## 候选：还能影响"留主机／被分段"的地方

在 `modules/tiles/` 里按链、顺序、驻留三组关键词通读后，除上面两处，**还可能决定这一点的有四处**：

1. `modules/tiles/src/region_outline.cpp#outlineRegions` —— 段被调用节点取代时，`resident`／`uploadControl` 两张表决定哪些节点留在设备侧；同一函数还把被取代语句的 `ctrlInputsOf`／`ctrlOutputsOf` 接给调用（sync 与 control 两种边都接）。
2. `modules/tiles/src/region_extract.cpp#extractRegion` —— 建段成员时才去读 `ctrlInputsOf(ref)`，段成员集合就是在这里定的。
3. `modules/tiles/src/graph_regions.cpp#gpuRefusal` 与 `modules/tiles/src/graph_regions.cpp#deviceCapable` —— 逐节点给出"为什么不能上设备"的理由与算子可设备化判定。
4. `modules/tiles/src/residency.cpp#residencyReport` —— 只读：把每个块的 `device`／`host` 数报出来，本页 ③ 的读数就是它。

## 要问的一句

**设备作用域内，让一个"块首效应语句"被分段（而不是留在主机上）的那条判据，在哪个函数里**——是上面两处已读的之一（哪一支），还是候选清单 1–4 里的哪一个？照那一点造用例，一次到位；在此之前这个修复只有"判据本身更准、复杂度更低"这一层保证，没有能分辨的用例。

## 没量到的

- 链首成员缺失在真实程序里造成的段数差（本页只量到三种自造形状不分辨新旧，没有量"少切了多少段"）；
- 设备作用域为真时 `ordered` 是否在别的判据上被读取（只读了 `eligibility` 与那处投影判据，没有逐个追 `reasons` 的其它使用者）；
- 候选 1、2 两处是否真的读取"语句顺序"，还是只读取值依赖（没有逐行读完）。

本页只报测量与一句询问：不改代码、计划、断言或生成物。