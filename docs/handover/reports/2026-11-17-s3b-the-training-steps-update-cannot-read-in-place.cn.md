# S3b：训练步的更新读不到 `in-place`，因为在图里它没有「原地形」也没有可证明的单元

> 一页一结论。量的是 `camel inspect --copies`（`include/camel/runtime/storage_reuse.h#dumpStorageReuse`），
> 在 `D:\Projects\Camel-wt\s3b` 的 `lane-s3b`（基线 `3f7debc78`，含 `5512e644b` 的复用声明、S1、S2，不含 S3）上，
> 构建与计划都在构建主机上跑（`camel-remote.ps1`，Release）。**源码没有一处改动**（工作树只多这一页、一个用例和
> 它的计划条目）：结论就是"改不出来"，以及"要改哪里才可能出来"。

## 1. 改之前的读数（逐字）

训练步程序：`examples/case-study/train.cml`，更新在 `step` 第 17 行 `return sgd(layer, g, rate)`。

```
COPIES train.cml:17:12 sgd declared
COPIES step updates 1 in-place 0 by-count 0 declared 1 copied 0
```
其余五张图（`__root__`、`loss`、`train`、`main`、……）都是 `updates 0 in-place 0 by-count 0 declared 0 copied 0`。
即：`updates 1 / in-place 0 / by-count 0 / declared 1 / copied 0`。

S3b 的另一条判据（`v = v->append(x)` 循环，`test/cases/consistency/new_values/rebinding_loop.cml`）同期读数是
`collect 1 by-count`、`push_all 1 copied`、`snapshots 2 copied`、`boxes 1 by-count`——与
`docs/handover/questions/2026-11-16-what-counts-as-an-update-for-a-training-step.cn.md` 记的一致，没有漂移。

## 2. 为什么 `in-place` 到不了：三道门，每一道都在别的文件里

1. **计划根本不问这个节点。** `planStorageReuse`（`src/execute/exec_plan.cpp:760`）只在
   `table.plainForm(uri).has_value()` 为真时走进原地分支——也就是说，只有**本身就是某个算子的原地形**的节点才被判定；
   其余的节点在 `storageReuseAt` 里留 `None`（`exec_plan.cpp:673` 的初值）。`nn:sgd` 不是任何算子的原地形：
   `setInPlaceForm` 只登记了 `:op/set_index`、`:struct/append_arr` 与四个排序（`src/builtin/executors/builtin.cpp:740,745`、
   `src/builtin/operators/array.cpp:431`），而且它的契约是"改它的第一个普通参数（一个数组）并交回它"
   （`include/camel/core/operator_table.h:421–425`）——`sgd` 交回的是树，不是那个数组。
2. **即使走进原地分支，它要 `:var/take`。** 那个分支要求第一个 norm 输入是 `:var/take` 的格子（`exec_plan.cpp:769`），
   而全树只有两处写 `:var/take`：`Builder::inPlaceRebindingOf`（`builder_operators.cpp:1436`）与路径赋值
   （`builder_operators.cpp:1639`），**两处都先要求该算子有原地形**。所以没有任何建图路径会把 `sgd` 的第一个实参包成 `:var/take`。
3. **那个 operand 本身也不是本图的格子。** `step` 里它是形参 `layer`；`training.cml` 里 `update`/`adam` 的 operand 是形参或循环状态。
   `cellDeclaredHere` 对"本图不声明的单元"为假（`exec_plan.cpp:587`），计划能给的诚实答案最多是 `ByArrayCount`。

## 3. 而且今天的核函数没有复用，`by-count` 也会是说谎

`modules/nn/optim.cpp#sgdLeaf` 每个叶子都 `newLike(p)` 新分配（`optim.cpp:248`），`adam` 的 `adaptiveUpdate`
更要求输出缓冲不与输入重叠。声明（`modules/nn/module.cpp:86` 的 `declareReusesOperand`）是算子对读者的承诺，
**没有任何读者据此复用**（唯一读它的运行时位置是尺子自己与 ONNX 导出器）。所以 `declared` 是今天唯一真话：
它是"声明"，不是"测量"。

## 4. 测量：真把叶子写回去，程序的值就变了（判据②被违反）

去掉分配、无条件写回叶子（`Array *next = const_cast<Array *>(p);`，一次构建后即回退）：

| 管线 | 改之前 | 写回叶子之后 |
|---|---|---|
| `std::fvm` | `loss before: 3.7500` / `loss after: 0.0000` | **`loss before: 0.0000`** / `loss after: 0.0000` |
| `std::opt std::fvm` | `loss before: 3.7500` / `loss after: 0.0000` | `loss before: 3.7500` / `loss after: 0.0000`（这条管线没变；没查原因，多半是 `std::opt` 之后叶子已经不是同一个对象） |

`main` 在训练之后还要 `loss<w0, y>(X)`，而 `w0.weight` 与 `step` 里的 `layer.weight` 是同一个数组对象；
**叶子自己的计数看不见"装着它的那棵树有第二个名字"**——`shareForSecondName` 只处理 `TypeCode::Array`
（`src/builtin/operators/ops.cpp:53`），结构体/元组、函数实参与返回值、闭包捕获都不经过它
（`docs/handover/reports/code-review-memory.md` 严重 3）。因此"叶子独占"并不等于"树独占"，
而训练步的入参恰恰是树。

另外：本程序的叶子是 `reshape` 出来的**视图**，`Array::writableInPlace()` 对视图恒为假
（`src/core/rtdata/array.cpp:1086`），所以就算核函数改成"计数为 1 就写回"，这个程序也一次都不会复用，
读数只会从 `declared` 变成另一个同样没有发生的词。

**结论**：今天硬让训练步报 `in-place`，换来的是一条**读错的程序**——判据②不是靠改读数能过的。
下面第 5 节的第 2、3、4 步，哪一步都不能在"树有第二个名字时叶子计数不涨"的前提下做。

## 5. 下一步该改哪里（按依赖顺序）

1. **先给树/元组第二个名字的计数。** 这一步有两半，今天要分清：(a) `shareForSecondName`
   （`src/builtin/operators/ops.cpp:53`）只认 `TypeCode::Array`，结构体/元组递给第二个名字时连对象都不换，
   叶子的 `writableInPlace()` 看不见树被第二个名字持有；(b) 更多的第二个名字根本不由它造——实参、返回值、
   容器、闭包捕获都不过这三个算子。(a) 是 `code-review-memory.md` 严重 3 的一半，也是 `declareReusesOperand`
   成立的前提；(b) 和第 4 步的"调用点交接"是同一件事，只是从"计数"这一侧看。
2. **再让核函数复用**：`sgdLeaf`（以及 `adam` 的叶子）在写的目标是自己的块时写回去，并让叶子是紧凑的而不是视图。
   这一步单独做时，判据②会红（上表），所以必须在第 1 步之后。
3. **再让计划判定它**：`planStorageReuse` 为"声明了复用、但没有原地形"的算子加一条分支——它的 operand 来自本图格子的
   `:var/take`（或直接 SSA 值且是该值的最后一次使用）时，用既有的 `owned`/`laterUsersAreOrderedAfter` 证明判 `Input`，
   否则 `ByArrayCount`；同时 `src/runtime/storage_reuse.cpp:169–175` 要在计划判了东西时读 `decided`，
   只在计划什么都没判时留 `declared`（现在 `!plain.has_value()` 把这条路堵死了，计划即使判了也印不出来）。
4. **最后才是"训练步报 `in-place`"**：第 3 步之后，案例训练步的 operand 是 `step` 的形参，计划最多能诚实地说
   `by-count`；要它说 `in-place`，还需要一条**调用点的交接**规则（实参的值在该调用处是最后一次使用，被调者据此可以写）——
   这是跨过程的所有权规则，不是这个尺子能决定的，也不是 S3b 的范围。

**今天能做的只有第 1 步的 (a)**（而且是准备段：它不移动任何读数）。2026-11-17 把 (a) 做了一遍并量了，见 5.1；
(b) 与第 4 步是一件事，不能"顺带"做。

### 5.1 第 1 步 (a) 实测：这样做没有读数，也救不了训练步

改动：`shareForSecondName` 认 `TypeCode::Struct`/`TypeCode::Tuple`——第二个名字是一个**新结构体/元组对象**，
逐成员递归分享（成员是数组就按数组计数），全部分支都不含可写数组时原样交出。构建通过（日志里有
`Building CXX object CMakeFiles/libcamel_objects.dir/Release/src/builtin/operators/ops.cpp.o`），然后：

| 读数 | 改之前 | 改之后 |
|---|---|---|
| `test/cases/consistency/new_values/two_names_for_a_struct.cml` 输出 | `a [1, 2]` / `b [9, 2]` / `[3, 4] [7, 4]` / `[5, 6] [5, 8]` | 一字未变 |
| 同一程序的 `camel inspect --copies` | `set_index copied` ×3，`main updates 3 in-place 0 by-count 0 declared 0 copied 3` | 一字未变 |
| 训练步 `std::fvm`（同期把 `sgdLeaf` 改成无条件写回） | `loss before: 0.0000` | **仍是 `0.0000`** |

两条原因，都不在这一步的射程内：

- **计数根本没被问。** 那三处路径写今天全由计划判成 `copied`（计划看到格子里的值有读者，或初值不是本图造的），
  走的不是原地形，`writableInPlace()` 一次也没被调用。也就是说：在"格子 + 计划"这条路上，`shareForSecondName`
  的第二个名字记在对的地方，但**没有一处会读它**；真正读它的是逃逸格子（`var` 形参、被捕获的格子），
  而那里"第二个名字看见写"正是 `var` 的语义，不该被挡。
- **训练步的第二个名字是调用造的。** `main` 把 `w0` 作为实参交给 `train`，`train` 再交给 `step`；
  实参绑定不走 `:var/new`/`:var/get`/`:op/assn` 中的任何一个（`shareForSecondName` 的全部调用点就这三个），
  所以树和它的叶子在调用处根本没有第二个名字可数。第 4 步说的"调用点交接"才是这个案例缺的那一环。

所以：第 1 步单独落地的唯一效果是**多出一批分配**（每次读写一个装着数组的结构体变量都要新结构体 + 每个数组字段一个新
`Array`），没有任何读数收益，故这次**没有提交**它（补丁留在 `D:\Projects\Camel-wt\s3b-step1-share-structs.diff`，
挂在 `src/builtin/operators/ops.cpp` 上，随时可套）；提交的是钉住这条不变量的用例
`test/cases/consistency/new_values/two_names_for_a_struct.cml` 与它的四条后端用例——它今天绿，靠的是计划的
`copied`；等第 3 步开始判原地写，它就是那个会先红的地方。

## 6. 这一页不改变的事

- 没有扩 `setInPlaceForm` 的集合（那会把"原地形"扩到本来不是原地写的东西）；
- 没有把 `declared` 改名成 `in-place`（那是把证据记成结论）；
- 没有动 `modules/nn/optim.cpp`：第 4 节的无条件写回只存在于那次测量里，已回退；工作树里 `modules/nn/optim.cpp`
  与基线逐字节相同，训练步的读数仍是 `COPIES train.cml:17:12 sgd declared` /
  `COPIES step updates 1 in-place 0 by-count 0 declared 1 copied 0`；
- 第 5.1 节的 `shareForSecondName` 改动同样只存在于那次测量里，已回退（补丁在工作树外的
  `D:\Projects\Camel-wt\s3b-step1-share-structs.diff`）；落在工作树里的只有这一页、一个用例和它的计划条目；
- 没有动 `src/compile/gir/builder_expr.cpp#rebindSsa`。
- 顺带读到的一格红：`new_values.sort_rebinding.graph`（`array:sort_d_in_place` 不在 rgir 输出里）在**改动前**
  同一棵树上也红——把计划文件退回 `HEAD` 单跑该计划，49 条里仍然只有它一条红，与本页无关，没有动它。