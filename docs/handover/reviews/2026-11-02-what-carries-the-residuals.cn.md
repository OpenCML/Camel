# 残差用什么承载？

> **作者 2026-11-02 08:30 裁定（C：闭包能携带运行期值）**：函数值 = callee 的图 + 闭包元组。
> `split` 的运行期 kernel 在拿到图之后才造 `lin`；callee 的图就是后半段，残差是它的捕获口；
> 闭包元组装残差的值，它的元组类型由 kernel 从后半段图里读出来（"图在手里，类型是确定的"）；
> 收集器按闭包元组自己的类型扫描；在调用点，`lin` 的静态类型是普通函数类型 ⇒ 没有"arity 未知"
> 的类型，也不需要放宽守卫（**B 被否**）。`split.cpp` 与 `split_operator.cpp` 的两张空成员表在
> 同一笔里删掉；不变量：`TupleType::residuals({})` 从 2 降到 0。
> 下面"三条死路"是裁定之前的现场，保留为证据：它说的是**没有现成写法**，不是**不可能**——
> 机制一直都在，见「裁定 C 落在哪三个机关上」。

## 一句话

作者 05:10 的读法是「`split` 的前半交出 `(y, lin)`，残差成为 `lin` 的 `with` 参数」。这句话在**源码层**和**运行期内核层**遇到了同一堵墙：**两者都造不出"带运行期值的闭包"**。

## 三条死路（都量过，不是推的）

1. **图里造不出带运行期闭包的函数值。**
   - `Func` 节点的 `type` 是**这次调用的结果类型**，不是函数类型（`include/camel/runtime/graph.h#GCNodeKind`；边不变量原话：`calls 'X', whose value is a float, and is a <(float, float)> (float) => float pure`）。
   - lambda 是**编译期展开的嵌套图 + 捕获端口**（`graph.h:342` `kGraphRefNested // written inside this graph (a lambda, a branch arm)`，`graph.cpp:1773`）。
   - 图里的函数值只以**静态值**出现：`draft.h:856` `materializeStaticValue(slot_t, Type*, …)`，闭包随静态值落盘（`pullback.cpp:369` 就是这么用的）。
2. **源码侧 `f<values>` 绑不了运行期值。** `let lin = linear<residuals>` 之后 `linear` 仍是 `(float, float) => float`，报
   `Function of type '(float, float) => float' cannot be called with argument types: <> (float)`。
3. **静态闭包的类型是空元组**（`TupleType::create({})`，`transpose_operator.cpp` 造静态函数值时如此），放不进运行期残差。

## 三条路与代价

- **A：`split` 变成建图期算子。** 类型由建图结果回填（调用点先建图、再类型化）。代价：`split` 从运行期内核变成图变换，1b 提前。
- **B：给残差一个"按值的实际成员数追踪"的类型。** 代价：GC 侧要接受"声明成员数可小于实际"，等于放宽 `reportTupleHasNoTupleType` 那条守卫（`include/camel/core/rtdata/tuple.h#rtdata`）。
- **C：闭包能携带运行期值。** 代价：新机制，图模型多一种节点，所有遍要看它。

## 裁定：C（08:30）

**C：闭包能携带运行期值。** 本页原来把它的代价写成"新机制，图模型多一种节点，所有遍要看它"——这句是错的：机制在图模型里已经有了，少的只是"谁去用"。C 是三条路里唯一不需要动类型、不需要放宽守卫的一条。

## 裁定 C 落在哪三个机关上（都在本树里量过）

1. **建值：`GCFillKind::FunctionClosure`**（`include/camel/runtime/graph.h#GCFillKind`）。一个 `Fill`，`norm` 输入是**静态函数值**（`Data` 节点，`dataIndex < 0`，命名要跑的图），`with` 输入是**捕获的值**，按闭包槽序排。写方是建图器（`src/compile/gir/builder_calls.cpp#Builder::createFuncDataNode`），读方是 `modules/transforms/rule_expand.cpp#chooseRuleAmong`——它原话写着：`normPorts` 是参数，`closureNodes` 是捕获。也就是说"图里造一个带运行期闭包的函数值"不是新节点，是**已有一类 Fill**。
2. **进图：`closureNodes`**。callee 的图用 `draft.appendClosureNode(port)` 声明捕获口（`include/camel/runtime/draft.h#appendClosureNode`），用 `appendNormPort(port)` 声明参数口；两者的类型都是 `Port` 节点自己的 `dataType`。运行期调用时，函数值的闭包元组按槽序灌进 callee 的捕获口（`populateFunctionClosureFrame`，`src/passes/sched/fastvm/fastvm.cpp#FastVMSchedPass::populateFunctionClosureFrame（以及同文件的 `FastVMSchedPass::populateIndirectCallFrame`、`FastVMSchedPass::populateMarkedCallFrame`）`）。
3. **扫描：闭包元组自己的类型**。函数值的布局是它被创建时给的那个元组类型（`include/camel/core/rtdata/func.h#updateRefs`），GC 就按它扫（`src/core/rtdata/func.cpp#Function::print`：`closureType()`，注释明确写着"不是程序里的图会给的那个"）。所以"类型由 kernel 从后半段图里读出来"= kernel 用 `TupleType::create(残差各自的类型)` 造 `lin` 的布局，而不是任何 `TupleType::residuals`。

合起来，`split` 的形状是：

- 后半段（`lin` 的 callee）：`normPorts` = **各个切向**，`closureNodes` = **各个残差**（顺序 = `residualsOf` 的顺序），类型 `(tangents…) => tangent`——**残差不出现在任何函数类型里**，这就是"没有 arity 未知"。
- 前半段：出口是 `(value, lin)`，其中 `lin` 是上面对话里的那个 `Fill`：静态函数值（图 = 后半段，布局 = 残差元组类型）+ `with` = 残差的值。
- 于是 `lin(1, 0)` 这样调用：残差来自闭包，实参只有切向。

## 落地要动的地方（按 Commit 顺序）

**一条读数先钉住**：裁定里那两个数不是新编的，就是 `test/cases/transforms/split/split_two_params.cml`（作者为这一层点名的读数程序：`f(x, y) = x*x*y` 在 x = 3、y = 2，今天印 `value= 18.0`、`tangent dx= 12.0`、`tangent dy= 9.0`、`residuals=5`）。裁定里的 `lin(1, 0) = 12` 就是它的后半段在切向 (1, 0) 上的读数，`transpose(lin)(1) = (12, 9)` 就是它的转置读数（`2xy = 12`、`x² = 9`）。所以这一步的验收拿这条用例做，不新写一条。

1. **`split` 的后半段 + 前半段的出口**（`modules/transforms/split.cpp`）：`splitHalvesWith` 不再把残差当 `backwardParams.front()`；两条建图路（直线 `buildSplitGraph` 与分支 `cutBranchGraph`）把 `residualsPort` 换成 N 个闭包口；`Half::projectResidual` 从 `Accs` 改成直接返回那个闭包口；前半段的出口从"填元组 `(result, residuals)`"改成"`Fill(FunctionClosure)` 造 `lin`，再填 `(result, lin)`"；kernel 用后半段的 `closureNodes` 类型造静态函数值。读数：`lin(1, 0) = 12`。
2. **`transpose` 读残差的入口**（`modules/transforms/transpose.cpp`）。这一条是本页新查出来的**耦合**，也是把它算成"三模块一笔"的原因：目前 `transpose` 把线性函数的**第一个普通参数**当残差束读（`:441`、`:527`、`:839`、`:1086-1087`、`:1235`、`:1851` 都是 `normPorts()`）。残差进了闭包之后，它要改成从**函数值的闭包元组**读，并把那些值做成交出来的转置图自己的捕获口。读数：`transpose(lin)(1) = (12, 9)`。
3. **两张空成员表**（`modules/transforms/split.cpp#residualsOf`、`modules/transforms/split_operator.cpp#splitOperatorSignature`）：第 1、2 步落地后残差不再是一个类型，两张表没有存在的理由；不变量 `git grep -c 'residuals(' -- src include` 的"造空表"两处降到 0。本页原来写"在 A 落地前不要删它们"——**那条已经作废**：C 落地后不是"根本没有类型"，而是"残差根本不是类型的一部分"，读数只会更干净。
4. **库**（`stdlib/vjp.cml`）：`let forward = split(jvp(f))`，`let (value, lin) = forward(..xs)`，`transpose(lin)(ct)`——`res` 不再过手。
5. **钉住旧类型串的读数**（这一条是第 1 步之后必然要跟着改的，先数清楚）：`split` 的前半段今天把残差元组写进自己的**类型**里，所以有四处计划条目直接钉着那个串 —— `test/plans/feat/transforms/split.plan.toml#split_two_params`（`type=(float, float) => (float, (float, float, float, float, float)) pure residuals=5`）、`:92`（`=> (float, (float, float, float)) pure residuals=3` 与四项的那条）、`test/plans/feat/modules/remat.plan.toml#remat.split`、`test/plans/feat/transforms/compositions.plan.toml#compositions.vjp_single`（`residuals=2`）。改完之后前半段的类型里只剩 `(result, lin)`，这些串要一起换；`residuals=` 这个计数是图给的，可以留着。

## 现状

截至 2026-11-02 晚：`lane-resid` 建在 `origin/develop`（`fbf0eb479`）上，**只有这一页的记录，没有代码**。上面第 2 条是本页补上的原因：它不是 `split` 内部的重排，`transpose` 那一侧同样要把"残差的入口"从参数搬到闭包，所以这一笔不能按"只改 split"来估。