# 链首成员：补上之后没有一条用例能分辨新旧

## 我量到的

区域扫描判断"这个节点是不是某块的语句"时，判据是"两侧有没有 control 边"，加上"有没有同步前驱"。这两句查的是同一段：同步前驱正好就是以该节点为 `user` 的边。块的**第一条语句**在链上只有出边、没有入边，所以它从前根本不算语句；末一条算。

于是把判据改成按每条同步边的两端建集：`modules/tiles/src/graph_regions.cpp#chainMembers` 每图遍历一次 `syncEdges()`，`user` 与 `pred` 都放进集合，`modules/tiles/src/graph_regions.cpp#inChain` 只做下标查。`modules/tiles/src/graph_regions.cpp#eligibility` 与聚合投影那处共用这份集合，逐节点扫边随之取消。

这一改动在语料上是安全的：`feat/modules` 全绿（`native_region_invocation` 五条不退化），全量 `total 7475, pass 7458, fail 7`，红名单与改动前同名同数。

## 卡住的地方

按"设备作用域里只有一条语句的块"和"块首语句就是要分段的那一点"各写了一条用例，用同一台机器、同一条命令 `tiles::test_outline_native`，分别在**有修复**与**把源码回退后重建**的两个二进制上跑：

| 用例 | 有修复 | 回退后 |
| --- | --- | --- |
| 单语句块 | `TILES_OUTLINED 1` / `2.7206764` | `TILES_OUTLINED 1` / `2.7206764` |
| 块首即分段点 | `TILES_OUTLINED 2` / `9.998035` | `TILES_OUTLINED 2` / `9.998035` |

逐字节相同，两条用例都不分辨新旧。冻成用例只会锁住一个与这个修复无关的值，所以没有提交。

## 读代码得到的解释

- `modules/tiles/src/graph_regions.cpp#eligibility` 里 `ordered` 只在 `if (ordered && !device) return "control_edge";` 这一句起作用。**设备作用域内 `device` 为真，`ordered` 不影响返回值**，所以"单语句块"和"块首"这两类用例都走不到差异。
- 聚合投影那处用 `inChain` 是为了**跳过**（让节点不可外提）。方向同样是"减少可外提"。
- 两处方向都只会减少可外提，与"补上链首成员会多出区域"方向相反。所以此前的读数（同一个用例从 6 条变 10 条）来自**同步前驱**那一半，而不是链首这一半。

## 要问的一句

**链首成员本应在哪个判断上生效**——是 `modules/tiles/src/graph_regions.cpp#eligibility` 里某一支（哪一支），还是 `modules/tiles/src/region_outline.cpp` 里另有判据？照那一点造用例，一次到位；在此之前这个修复只有"判据本身更准、复杂度更低"这一层保证，没有能分辨的用例。

## 没量到的

- 链首成员缺失在真实程序里造成的区域数差（本页只量到两条自造用例不分辨新旧，没有量"少切了多少段"）；
- 设备作用域为真时 `ordered` 是否在别的判据上被读取（本页只读了 `eligibility` 与那处投影判据，没有逐个追 `reasons` 的其它使用者）；
- `modules/tiles/src/region_outline.cpp` 一侧是否也有同类判据（没有读）。

本页只报测量与一句询问：不改代码、计划、断言或生成物。