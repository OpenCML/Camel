# 嵌套两级的 cut 丢掉自己的孙程序：把 `adopt` 改成传递，还是让每个 cut 自带一条链？

## 问什么

`split` 在一个"回答一对 (值, 切线) 的被调者"处切一刀时，会把这个被调者自己的那一刀（下称**子 cut**）
的程序 `adopt` 进自己的程序，这样前半/后半里那条调用才在运行期解析得到图。今天的 `adopt` 只有一层：

- `include/camel/runtime/program.h#Program::adoptMissing` 搬的是 donor **自己 own 的**图；
  `src/runtime/draft_session.cpp#GraphDraftGroup::adopt` 就调它一次；
- 而 donor 自己 `adopt` 进来的图**不在**它的 owned 集合里（satellite 只 own 自己编码出来的成员）。

于是三级链就断了：`g2(x) = g(x) * x`（`f(x)=2.0*x`，`g(x)=f(x)*x`）要切两刀，最外层 cut 的程序里
有 `g` 那一刀的图，却没有 `f` 那一刀的图，运行到那次直接调用时：

```
Camel Invariant Violation ... nodevm.cpp(639) Guarantee: runtimeTarget != nullptr
Reading: a direct call names a function whose graph is not in the program's graph table
```

**这是编译器缺陷，不是程序的错**：程序里每一个被调用的函数都写在那一页源码上，图表的缺口是编出来的。

问题只有一句：**"图可达"的责任在核心（`adopt` 传递），还是在 cut（每个 cut 自带它站着的整条链）？**
`src/runtime/**` 不在本 lane 允许改的目录里，所以本轮先在 cut 侧绕，并把选择交上来。

## 读数（可复核）

闸门机 88，工作树的 base 是 `34b659107`（= 当时的 `origin/develop`）。

- **崩溃是既存的，不是残量修补带出来的**：把本轮 `modules/transforms/split.cpp` 换回 `34b659107`
  的版本重编，同一个 `g2` 程序**同样**死在上面那一行（那次读数 `residuals=4`；修好后 `residuals=3`）。
  所以这不是"少了一个残量"的副作用，是另一条独立的缺陷。
- **探针**（本轮临时加的 `CAMEL_SPLIT_PROBE`，已撤）：崩溃前 `split_of_jvp_of_g_forward node=1
  callee=347 inProgram=NO`——名字里的那个被调者，在最外层程序里查不到；沿 `standing` 链 adopt 之后
  同一行读作 `inProgram=yes`。
- **修好之后的读数**：`grad(g2)(3.0) = 54.0`、`grad(g2)(2.0) = 24.0`（`g2 = 2x³`，`6x²`），
  `lin2(1.0) = 54.0`、`lin2(2.0) = 108.0`（线性），`jvp` 那一对 `value=54.0 tangent=54.0`。
  用例是 `test/cases/compile/transforms/grad_across_a_call.cml`（`feat/compile/transforms` 里
  `grad_across_a_call`）与 `test/cases/transforms/split/split_linear_across_a_call.cml`。
- **不传递这件事是刻意的**：`Program::adoptMissing` 只按 id 补缺、从不覆盖已有 id，而 id 由
  `context_->graphIds().allocate()` 全局发放，所以"重复 adopt 同一个 donor"是安全的——它只是补不齐
  第二层。

## 三条路，各带代价

**A. cut 自带它站着的整条链（本轮默认，已落地）**

`modules/transforms/split.h#SplitGraph::standing` 记下"这一刀站在哪些程序上"（第一个子 cut 的程序，
再并上那个子 cut 自己的 `standing`），`modules/transforms/split.cpp#buildSplitGraph` 在 adopt 循环里
把整张表都 adopt 进自己的程序，然后再把 `built.standing = nestedPrograms` 交给上面一刀。

- 好处：不碰 `src/runtime/**`；任何深度都够（`g2` 是两级，读数在上面）；adopt 的次数是 O(深度²)，
  但 `adoptMissing` 按 id 跳过已有图，实测构建时间没有可测变化。
- 代价：把"图可达"的责任放到了 cut 侧。今天只有 `split` 会嵌套 cut（`split` 是唯一会切同一个图的
  变换），所以够用；但**下一个会嵌套 cut 的变换必须自己再写一遍这条链**，而它不知道该写——这是这条路
  的真正代价：一条规则只在一个调用点成立。
- 还有一条边界：`standing` 里的指针指向那些程序，它们的生命周期由 `SplitGraph` 的持有者保证；
  今天 cut 的程序活得比这个结构长（都在 `context_` 里），但这是一条**没人写下来的假设**，B 落地后才
  完全消失。

**B. 核心把 `adopt` 做成传递**

`Program::adoptMissing` 除了搬 donor own 的图，还把 donor 自己 adopt 进来的那批一起搬过去
（或者 `GraphDraftGroup::adopt` 在调用前把 donor 的 adopted 集合递归展平）。一两行的量级。

- 好处：责任回到核心，任何 donor 都受益，A 的"下一个变换要自己写一遍"和生命周期假设一起消失。
- 代价：`src/runtime/**` 不在本 lane 允许改的范围内；而且 `adopt` 的 ownership 规则是刻意的
  （satellite 只 own 自己编码的成员），改成传递必须回答"同一张图被两条路径 adopt 时谁 own 它"——
  按 id 去重能扛住（id 全局唯一），但要在 `Program` 上说清楚"owned"和"imported"两个集合的语义，
  否则 map 里的图会被释放两次或永远不释放。这笔账不是这一刀该算的。

**C. 运行期宽松化**

`runtimeTarget == nullptr` 时去所有已登记的程序里按 id 再找一遍，找不到才报。

- 好处：一行就能让所有嵌套深度跑起来，A 和 B 都不用做。
- 代价：把编译期的可达性问题变成运行期的查表，每次调用都要扫；更要紧的是它把"图表是编译产物、
  调用是常量"这条性质弄没了——`std::opt::inline`、`std::jit` 这类按图表做决定的 pass 之后会读到
  一个不再成立的假设。不建议。

## 默认（本轮按这个开工，不等回复）

**走 A**，并且已经落地、已经过闸门：它是**唯一**能在本 lane 的目录范围内做完的一条，代价是一个局部
数据结构多一个成员和一段注释，而 B 的收益（把责任放回核心）可以被 A 完整保住——A 的 `standing`
表就是 B 的传递集合在 cut 侧的影子，B 落地时删掉它、把 `built.standing` 那两行一并撤掉即可。

作者若裁 B，本轮的 `SplitGraph::standing` 与 `buildSplitGraph` 里那两段可以整体删除（`adoptMissing`
传递之后 `nestedPrograms` 只需要第一个子 cut 的程序），用例和读数一条不用改；若裁 C，A 保留也无害，
但 `standing` 就只是为了让图表完整而留的注释。