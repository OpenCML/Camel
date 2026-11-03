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