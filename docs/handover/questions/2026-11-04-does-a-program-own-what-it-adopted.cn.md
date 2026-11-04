# `Program` 交出去的图：只有它自己加的，还是连它 adopt 进来的一起？

## 问什么

一句话：`Program::adoptMissing`（`include/camel/runtime/program.h#Program::adoptMissing`）搬 donor 的图时，
应不应该把 donor **自己 adopt 进来**的图一起搬（即传递），还是"每个调用方自己搬全它站着的那条链"？

## 让它是问句的读数

- 今天只搬 donor **own** 的图。satellite 程序（`src/runtime/program.cpp#makeProgram`、
  `src/runtime/draft_session.cpp#GraphDraftGroup`、会话的工作表）的 `own` 只有"它自己加出来的"，
  adopt 进来的一律不 own —— `own_` 一个 bit 装不下"它加的"与"它取来的"两种来历。
- 于是**三级链断在第二层**。工作树 `D:\Projects\Camel-wt\adopt`，base `34b659107`（= 实时
  `origin/develop`），闸门机 88，fast 层，`-Jobs 4`，host 1m load 19.7（不安静，读数只做存在性判断）：
  - `split(jvp(g3))`，三层 cut（`f=2x`、`g=f*x`、`g2=g*x`、`g3=g2*x`）→ **SIGABRT**，
    `src/passes/sched/nodevm/nodevm.cpp#NodeVMSchedPass::call`：`Guarantee: runtimeTarget != nullptr`、
    `Reading: a direct call names a function whose graph is not in the program's graph table`；
  - 少一层的 `split(jvp(g2))`（`g2`/`g`/`f` 三刀；那条车道叫它"两级嵌套"）→ 同一条 SIGABRT
    （`REINTERP split function=jvp_of_g2 ... residuals=4`）；
  - **不经 cut 的都不崩**：`jvp(g3)` 绿（`value=32.0 tangent=64.0`），`grad(g2)`、`grad(g3)` 绿，
    `grad(remat(k))`（k 里有一次普通调用）绿；`grad` 里经过**函数值**的路今天在 `autodiff` 自己那里就被
    拒绝（`cannot differentiate through this call of a function value: which function it calls is not
    evident`），够不到图表。机制上也说得通：`modules/transforms/split.cpp#buildSplitGraph` 是今天
    **唯一**"在一个建图过程里再建一遍图、再把那个程序 adopt 进来"的地方，其余的 adopt 点
    （`src/runtime/definitions.cpp`、`src/runtime/draft_inline.cpp#functionGraph`、
    `modules/autodiff/pullback.cpp#Engine::graphOf`）的 donor 今天都不是 satellite。所以这是
    **编译器缺陷，不是程序的错**：程序里每个被调用的函数都写在那一页源码上，图表的缺口是编出来的。
- **修好之后**（同一棵树、同一个用例）：三层那个不再中止，值的一半按手算给出 32.0（x=2）与 162.0（x=3）；
  但**线性的那一半仍然是常数**（x=2 读 128.0，x=3 读 648.0、且 dx=0/1/2 同值）。这是**另一条缺陷**、
  不是表的事：表里缺图只会撞上面那句 `Guarantee`，不会给数；而且它已经被 cut 自己的
  `split_linear_across_a_call`（`feat.transforms.split`，x=3 处读 12/6/24）钉在**两层**上，
  原因是 `modules/transforms/split.cpp#residualsOf` 把参数的切线口当成参数值口记进残量。
  那条修好后本用例的三个读数应是 216.0 / 432.0 / 0.0，本页与 `feat/runtime/adopt_chain` 的注释都写了。
- 不传递这半条窄**不是**一个有意的契约：`program.h#Program::owns` 的注释只解释"base 那一版可能被后来的
  commit 换掉、可能已经被释放"（这条是对的，必须保住），对"adopt 进来的图"一个字没有；而且
  `adoptMissing` 绑定时**不标记 own**，于是"取来的"和"继承的"在表示上分不开 —— 这是表示缺口，不是决定。

## 三条路，各带代价

**A. 每个 cut 自带它站着的整条链**（`split` 车道已落地：`SplitGraph::standing`）

好处：不碰 `src/runtime/**`。代价：责任落在调用点，下一个会嵌套建图的变换得自己再写一遍，而它不知道该写；
`standing` 里那些程序指针的生命周期是一条没人写下来的假设。

**B. 核心把 `adopt` 做成传递**（**本轮已落地**）

`program.h#Program::bindTaken` 把"取来的图"记进 `own_`（值 2，与"它加的"值 1 并存），
`adoptMissing` 从此搬 `added ∪ taken`。**没有任何递归**：这个集合在 adopt 那一刻就维护好了，所以
A adopt B、B adopt A 也不会栈溢出或死循环；重复 adopt 因为按 id 补缺而幂等。
`src/runtime/draft_session.cpp#RuntimeGraphDraftSession::adoptGraph` 同理改用 `bindTaken`：会话为程序编出来的
特化图也是"取来的"，不是 base 那一版。
代价：`own` 的语义从"它加的"扩成"它能交出去的"，必须写清楚（已写进 `Program::owns` 与 `Program::adoptMissing` 的注释）。

**C. 运行期宽松化**（`runtimeTarget == nullptr` 时扫所有已登记程序）

把编译期的可达性变成每次调用的查表，并且让"图表是编译产物、调用是常量"这条性质不再成立。拒绝。

## 默认（本轮按这个开工，不等回复）

**B**，已落地并过闸门。A 在 B 之上变成冗余：`SplitGraph::standing` 与
`buildSplitGraph` 里那段整条 adopt 的循环**修好之后可以整体回退**，读数一条都不用改（adopt 幂等，
留着也不会错）。但那份工作树不归本车道，所以本页只写"修好后可回退"，不动它。