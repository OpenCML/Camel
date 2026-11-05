# 两级嵌套的切：谁来带第二层的图

## 问句

一个被调函数在**另一次嵌套切**里被切时，外层该不该把第二层的半边图一起带进来（采纳），还是像本轮这样按
名字拒绝？

## 现在的状态（树 `splitports`，base `44300579c`，2026-11-04 03:4x +08:00，主机 load 18–48）

形状：`split(jvp(f))`，`f` 调 `g`、`g` 调 `h`。切 `jvp_of_f` 时要 `jvp_of_g` 的前一半，而切那一半时又要
`jvp_of_h` 的前一半 ——**切的一刀里再切一刀**。原来落点是**崩**：

```
Camel Invariant Violation
    Location   : ../src/passes/sched/nodevm/nodevm.cpp(639)
    Guarantee  : runtimeTarget != nullptr
    Reading    : a direct call names a function whose graph is not in the program's graph table
```

机制（`include/camel/runtime/program.h#adoptMissing` / `#owns`）：`GraphDraftGroup::adopt` 只把
`other.owns(id)` 的图拷进来，而 `owns` 对 satelite 程序只认它**自己造的**图；第二层那两个半边图是那次嵌套
build **采纳**进来的，`owns` 里没有。于是外层程序持有一层、缺一层。

本 lane 改成按名字拒绝（`split.cpp#buildSplitGraph` 的 `calleeCuts` 循环里），句子与位置：

```
transforms: split does not copy a call to `jvp_of_g`, whose own cut stands on `jvp_of_h` yet (...)
   --> test/cases/transforms/split/split_two_nested_cuts.cml:25:8      func f(x: float): float {
note: raised here
   --> ...:30:15   let fwd = split(jvp(f))
```

## 三条路与代价

- **甲（本轮默认，已落地）**：拒绝。代价：**调用链嵌套两层就切不了**（这是常见形状：任何一个「主函数 → 中间
  函数 → 叶子函数」的微分都够两层）。好处：句子说清是哪一层、哪张图。
- **乙**：把第二层也采纳 —— `SplitGraph::nestedPrograms`（本轮新增，已经是「这次切踩过的程序」）改成**传递**
  收集（`built.nestedPrograms = 本层的 nestedPrograms + 每个 nested.program 的 nestedPrograms`），外层
  `for (const rt::Program *p : nested.nestedPrograms) group.adopt(*p)`。代价：约三行；红/绿用例现成
  （`test/cases/transforms/split/split_two_nested_cuts.cml` 从「拒绝」改成期望
  `f y= 15.0` / `f d= 6.0`，即 `(2·2 + 1)·3` 与其切向）。没量过的风险：采纳进来的图与 `group.encode()` 的顺序、以及
  `Program::version` 的可见性；要一条用例钉住数字，不能只钉「不崩」。
- **丙**：把两级以上一律当成「不支持」写在文档里，不拒绝。代价：退回**崩**，按「对 > 明确拒绝 > 错数 > 崩」
  是最差的一档，不取。

## 默认建议

**甲留下，乙单开一条车道**：`nestedPrograms` 字段本轮已经落进 `split.h` 并带上去了（`buildSplitGraph` 末尾
赋给 `SplitGraph`），乙只需要把收集改成传递、把拒绝那一格换掉，用例是现成的红/绿靶子。

## 答（lane-splitnext，树 `splitnext`，base `9c6e41182`，2026-11-04 04:2x +08:00，主机 load 18.35 / 21.89 / 21.6）

**乙落地**：`modules/transforms/split.cpp#buildSplitGraph` 的 `nestedPrograms` 改成传递收集 —— 本层采纳的
每个程序，连同它自己的 `nestedPrograms`，按取的顺序进同一张单子、按指针去重；末尾把整张单子 `adopt` 一遍。
`adopt` 走 `Program::adoptMissing`，只**绑指针**，不复制图，也不重算任何值。

先红后绿：用例先改成期望答案而源码不改，`feat/transforms/split` 跑成 `total: 24, pass: 22, fail: 2`；
改完源码 `24 / 24`。两条读数：

- 两层（本条的靶子）：`f y= 15.0`、`f d= 6.0`，前面三行 `REINTERP split function=jvp_of_h/jvp_of_g/jvp_of_f` 都在
  —— 三个切按由内到外的顺序建，最外层把四张半边图都带上；
- 三层、四层（探针，不入 git）：`chain y= 21.0  d= 6.0`（`f -> g -> h -> k`，`f(x) = 3·(2·(x+1)+1)` 在 2 处是 21）。

**代价（量过）**：每个嵌套 build 有自己的一张 `calleeCuts`（`buildSplitGraph` 的局部变量），所以同一条链的
深层会被**重复切**：`split(jvp(f))` 在 `f -> g -> h -> k` 上实测建了 **8 对图（graph id 340–355，16 张）**，
其中 `k` 切了四次、`h` 两次、`g` 一次、`f` 一对；线性只要 4 对。三函数链是 4 对、四函数链 8 对 —— 按
`2^(n-1)` 长。名字不冲突（id 由 context 分配，重复的是同一张图），内存也不翻倍（图本来就建过，`adopt` 只绑指针）；
真正该收的是把 `calleeCuts` 穿进递归、一次 transform 一张表，那会让它变成线性，但那是另一条车道的事，
本轮没做 —— 今天到得了的深度（两、三层，即 4 和 8 对）代价可以接受。

**附带一句**：上面那张表里「两层」以下的第三层（`split_three_nested_cuts` 之类的用例）本轮**没有**加进 `test/`，
因为新增一个 `.cml` 会牵动 `test/plans/feat/parse/golden/ast_hashes.txt`；三层、四层是探针读的，读数在上。