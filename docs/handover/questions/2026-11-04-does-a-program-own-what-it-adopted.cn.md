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

**B**，已落地并过闸门。A 在 B 之上变成冗余，`SplitGraph::standing` 可以整体回退 —— 回退清单见下一节。
那份工作树不归本车道，所以本页不动它，只给下一手一份能照着删的清单。

## 回退清单：`SplitGraph::standing` 怎么删（照着做，不用再想）

**前提**：`adoptMissing` 传递**先进 develop**（本车道 `ce52f00d0`），再回退。顺序颠倒的话三层用例会重新中止
—— 本车道做过这个 A/B：把 `adoptMissing` 改回 `bind` 之后，同一棵树里的 `split_three_calls` 在打印前就中止。
清单按 cut 车道的树写的（`D:\Projects\Camel-wt\linres` @ `af6d97bd4`，`modules/transforms/`）。

### 删三处

1. `split.h#SplitGraph::standing` —— 成员
   `std::vector<const camel::runtime::Program *> standing;` 与它上面那段注释
   （`The cuts this one stands on, one program per cut below it ...`）。整段删。
2. `split.cpp#buildSplitGraph` 里把**孙辈**程序塞进 `nestedPrograms` 的那段展开 ——
   `for (const rt::Program *onward : nested.standing) { ... }` —— 连同它上面那条注释
   （`The callee's cut and the cuts *it* stands on ... the grandchild of a cut is not carried by its child's program`）整段删。
3. `split.cpp#buildSplitGraph` 末尾的 `built.standing = nestedPrograms;` 一行，以及函数结尾注释里
   `The list is this build's own `standing`, so a cut above this one adopts the whole chain in one step.`
   那一句（改写成"名单是直接子 cut 各自的程序"即可）。

### 留一处（**不要跟着删**）

4. `for (const rt::Program *nested : nestedPrograms) { group.adopt(*nested); }`
   这段循环在 `34b659107` 上就有，**不是 workaround 的一部分**。删掉 `standing` 之后 `nestedPrograms` 只剩
   每个 callee 那一层 cut 自己的程序，而"把直接子 cut 的程序 adopt 进本组的程序"是一层 cut 本来就必须做的事：
   本组的程序是 `program` 的副本，不含子 cut 新建的图。**这段删了，一层 cut 立刻坏。**

### 判据：怎么确认删干净、且没删错

- `grep -n standing modules/transforms/split.h modules/transforms/split.cpp` —— 只该剩 `split.cpp` 里那句
  无关的英文注释 `only refusal standing between this walk and a half that reads a value nothing carries`，
  其余 0 命中。
- **读数一条都不该变**，因为 adopt 只按 id 往图表里补条目，不重算任何值（`#buildSplitGraph` 自己的注释：
  `Adopted, not rebuilt -- the ids the calls name are the graphs those builds made, and no value is recomputed`）：
  - `feat.transforms.split`（本车道树里 20 条）逐条与回退前相同，尤其 `split_call_one` 的
    `g value=10.0 dgdx=6.0` 与 `residuals=3`、`residuals_in_the_closure` 的 `back dx=  12.0  back dy=  9.0`、
    `split_product_chain` 的 12.0 / 32.0；
  - `feat.runtime/adopt_chain` 3/3，`y2=32.0`、`y3=162.0` 不变 —— **这一条就是回退的证据用例**：它只在
    `adoptMissing` 传递时才过，回退后仍然过，才说明 `standing` 确实是多余的；
  - `split_linear_across_a_call` 的 12 / 6 / 24 回退前后同值 —— 那是 `#residualsOf` 的另一条缺陷，
    别拿它的红绿判断这次回退。
- **反证**（可选，一次构建）：把 `program.h#Program::adoptMissing` 临时改回 `bind(id, ...)`，重跑
  `feat.runtime/adopt_chain`，`split_three_calls_chain_resolves` 应当**重新中止**。它若照样绿，说明这条用例
  没在测传递性，上面那条判据也就不再可信。

## 一条硬警告：`known_fail_owner` / `known_fail_reason` 今天不在 develop 上

`34b659107` 的 `test/tools/run-tests.mjs#TEST_FIELDS` 只认 `known_fail`、`known_fail_msg`、`known_fail_note`、
`failure_note` 四个与标记有关的键。`known_fail_owner` 与 `known_fail_reason` 是 `lane-knownfail`
（`4bdf78684`，"a known_fail entry is refused at load unless it names an owner and a wall"）里的东西，
**还没进 develop**。

所以在 develop 上写这两个键，症状是：**整份计划一条测试都不跑**，只出现一条名为 `<invalid plan>` 的失败，
消息是 `unknown key 'known_fail_owner'`（`test/tools/run-tests.mjs#checkFields` 在**读** plan 的时候就抛，
不在跑用例的时候抛，`#invalidPlan` 把它折成一条假用例）。这很容易被当成构建坏了 —— 它不是构建，是 plan 的键。

那条车道合进 develop 之后，规矩反过来：**有标记就必须有 owner 与 reason**（`#checkKnownFailMark`），
缺一样都拒，而且拒的话都是原句：
`known_fail needs known_fail_owner: the mark says this case is red, and only an owner says who ends that -- name the lane or the person`；
`known_fail_owner is "...", which names nobody: name the lane or the person who makes this case green`；
`known_fail needs known_fail_reason: one sentence saying where the case is stuck -- the refusal in backticks, the file#Symbol that refuses, or the page that decides it`；
`known_fail_reason is "...", which says nothing about the wall: name what refuses, not that the case is red`；
`known_fail_reason names no wall: quote the refusal in backticks, name the file#Symbol that refuses, or name the page that decides it`。

一句话给下一手：**`lane-knownfail` 合进 develop 之前，标记只用旧的三个键；之后，标记必须带 owner 与 reason。
两种都写错的后果一样 —— `<invalid plan>`，一条测试都不跑。**