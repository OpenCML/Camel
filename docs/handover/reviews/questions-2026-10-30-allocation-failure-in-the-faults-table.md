# 分配失败算不算 `faults` 表该声明的故障

**裁定（2026-10-30）：（b）不算。** 作者原文：「`faults` 只记算子看着自己的操作数就能判定的失败。
内存耗尽是进程级的界限，和栈溢出同一类，由运行时在进程边界上报 `MemoryExhausted`，不属于任何
算子的语义。由此，优化在两个效应之间移动一个分配，不算改变效应顺序。我 16:50 写的判据里
'分配失败'那一项，按这条更正。这句话写进 4.6 那条注释：`faults = []` 说的是算子不会自己判定失败，
不保证进程不会耗尽内存。」落地见下面的"已按裁定落地"。

**要作者答的一句**（已答，原文留在页首）：分配失败算不算一个算子"自己的"故障、因而该写进它的
`faults` 表？

## 现象

下面三条都是读码与实测得到的事实，不是推测。

**① 今晚用的判据是"没有出错路径"= 不抛运行时故障 **且** 不分配。**
内核分配不到内存时会 `throw std::bad_alloc()`，例如
`src/core/rtdata/collection.cpp#Table::create`（第 40–42 行）与
`include/camel/core/rtdata/string.h`（第 22 行）；
`src/execute/pass.cpp` 在跑的循环里接住 `mm::MemoryExhausted`（第 2572 行），
报成 `RuntimeDiag::MemoryExhausted`（第 2577 行），于是**程序看得见**——它是运行时故障的一种。
按这条判据，"无故障路径"的算子还必须不造值。

这里有一处细节值得写清，因为它决定下面第 ③ 条的读数：`mm::MemoryExhausted` 是
`std::bad_alloc` 的派生类（`include/camel/core/mm/alloc/growable.h` 第 43 行），而
`pass.cpp` 只接 `mm::MemoryExhausted`。所以**库容器**（`std::vector` 等）自己 `operator new` 失败时
抛的是裸 `std::bad_alloc`，它**不会**被那一条 catch 接住，也就不是程序看得见的故障
（更可能是进程直接结束）。两种分配失败在这个运行时里的地位并不相同。

**② 按这条判据，会分配的算子一个都没补。** 今晚补的是两族（`fault_free_behavior` 与
`read_only_helpers` 钉住的那些），都是只读的；会分配的一个都没进表：
`op/add_s`（字符串拼接）、`struct/concat_arr`、`struct/append_arr`、`op/add_bytes`、
`op/slice_bytes`、`struct/zip`、`struct/range`，以及 `coll/` 里造值的
`map_put`、`set_add`、`merge`、`set_union`、`set_intersect`、`set_difference`、`keys`、
`map_values`、`map_items`、`set_items` 和会给可空值装箱的 `map_idx`。
理由写在 `docs/handover/reports/faults-table-audit.md` 的"两条判据，先说在前面"一节。

**③ 这条判据若被推翻，`declared_traits` 要减的行数：**

- 按"`mm` 分配失败才算"（也就是第 ① 条的实际写法）：**0 行**。那 30 行探针调用的算子
  （整数/浮点/半精度算术与比较、复数乘加、`pow`、三个 `len`、五个集合谓词、两个 `contains`、
  `Bytes` 的 `len` 与 `==`/`!=`）没有一条走 `mm` 分配器。整张表 104 行也一样：**0 行**。
  换句话说，这条判据今晚是**朝前**生效的（它决定下一族补谁），不是回顾性的——它没有赶走任何一个
  已经声明的算子。
- 按"任何堆分配都算"（含库容器）：**1 行**，`declared_traits` 里的 `arr_contains`。
  `:struct/contains_arr` 的内核走 `src/core/rtdata/equality.cpp#valuesEqualByContent`（第 314 行），
  那里用 `std::vector<EqualPair> pending` 做工作栈，也就是一次库容器的堆分配。
  同族的 `map_eq`/`map_ne` 不走它（`coll/eq` 走 `Table::equals` → `valuesEqual`，
  `src/core/rtdata/collection.cpp` 第 261 行，只用 `Cursor`，不建容器），所以是 1 行而不是 3 行。

## 两条路

**（a）算：`faults` 表要覆盖分配失败。**
那么"无故障"只在"不分配"时才可说，这一族永远补不进来；要给它们写故障记录，还得写明是哪一种
（今天 `FaultKind` 的六种——`DivisionByZero`、`IndexOutOfRange`、`EmptyReduction`、
`ConversionRange`、`ShapeMismatch`、`Other`——没有"分配失败"，得先加一种或落到 `Other`）。
代价是三处：表的语义从"算子判断的失败"扩成"这次调用可能发生的失败"；每个造值算子都要多写一行；
`effects(...).may_fault` 对几乎所有造值运算（包括字符串拼接）都变 1，读它的调度与优化能假定的
东西随之变少。

**（b）不算：`MemoryExhausted` 是一条进程级故障，不属于任何算子的语义。**
`faults` 表只管"算子自己判断出来的失败"——除数为零、下标越界、空集合求和、转换超范围、形状不符，
这些都是算子看着自己的操作数就能定的；而"这次 `put` 放得下放不下"取决于此刻还有多少内存、
别人在跑什么，同一个调用换个时刻就会变。它是运行到某一步时整个进程撞上的上限，报的时候带的是
空间、用量、上限和抬它的那个设置（`pass.cpp` 第 2572–2590 行），不是一个算子的判断。
代价是这张表**不覆盖**它，读表的人要知道：`faults` 为空不等于"这次调用不会因为内存而失败"。

## 已按裁定落地

裁定选（b），落地三件事：

1. **判据更正**：`faults` 表只记"算子看着自己的操作数就能判定的失败"，分配失败在表外。
   这句话写进了 `include/camel/core/operator_semantics.h` 的 `faults` 注释（也就是第 4.6 条
   "纯且不会出故障的节点可自由安排"读到的那条声明）：`faults = []` 说的是算子不会自己判定失败，
   不保证进程不会耗尽内存。清单顶部那条判据也随之改写。
2. **两种分配失败按同一规则处理**（这是裁定点出的缺陷）：`mm::MemoryExhausted` 是
   `std::bad_alloc` 的派生类，`pass.cpp` 原来只接住派生类，于是库容器（`std::vector`、
   `std::string` 的 `operator new`）失败时得到的是 `PassInternalError`——诊断说"这是编译器的缺陷、
   不是程序的"。现在同一个 catch 链里也接住裸 `std::bad_alloc`，同样报
   `RuntimeDiag::MemoryExhausted`，只是没有哪个空间可以点名（上限是宿主的）。
   用例：`feat/opt/generic.plan.toml#bad_alloc.diagnostic`，由测试专用 pass
   `test::throw_bad_alloc` 抛出裸类型；不接住时它有 121 条里 1 条红（读到 `PassInternalError`）。
3. **白送的收益**：既然分配不在算子的语义里，"优化在两个效应之间移动一个分配，不算改变效应顺序"
   就成立了——这条解掉了一直绕着的那个约束（此前挪动一次分配的相对次序，要先证明它不会喘不上气）。
   这条写成上面注释里的半句："moving the node past another effect does not reorder a failure"。

按（b）留下的限制，已经写在清单与上面那条注释里：**`faults` 表为空只说明"算子自己判断不出失败"，
不说明"这次调用一定成功"**——分配失败、进程被杀死这一类都在表外。

## 我方原来的默认建议（留档）

**（b）**，理由有三条：

1. 表里已有的六种 `FaultKind` 全是"算子看着操作数就能定"的失败，分配失败不是这一类，塞进去会
   让"空表"这个断言的含义从"这个算子不会说不"变成"这个进程不会死"，后者谁也证不了。
2. 按第 ③ 条的数，今晚的声明**一条都不用改**（0 行）；选（b）不是推翻既有工作，而是把既有工作的
   前提写成一句话。反过来选（a）要给 `FaultKind` 加一种、给造值算子逐条写记录，而收益是
   `may_fault` 多覆盖一件本来就在进程边界上报告的事。
3. 进程级故障今天已经有一条可见的路（`RuntimeDiag::MemoryExhausted` 带位置报出来），不需要
   由每个算子各写一遍。

**若要（a）**（未采纳）：请指明新的 `FaultKind`（或落 `Other`），以及"造值算子"这条边界怎么划——
是否包括 `std::vector` 这类库容器（第 ③ 条的两种读法相差 1 行）。