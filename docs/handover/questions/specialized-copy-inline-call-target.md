# 特化副本被内联后，调用目标退回基函数

## 我量到的

一个程序 `test/cases/opt/generic/shape_unroll.cml`，四条管线，两版二进制（同一棵树，唯一变量是 `src/passes/opt/inline/engine.cpp` 里"调用点只有一个"与"返回闭包"两条豁免的条件：现在看被调方自身是否在递归环里，此前看它不"到达"递归）。下表是 `forward` 里那个 `FUNC` 节点**指向哪张图**，读自 `std::jgir` 的 `callee`（图 id）与 `label`：

| 管线 | 新规则下 `forward` 调谁 | 对照（旧规则） |
|---|---|---|
| `std::opt` | **`run`** | `run$spec0` |
| `std::opt::specialize` | `run$spec0` | `run$spec0` |
| `std::opt::specialize` 然后 `std::opt::inline` | **`run`** | `run$spec0` |
| `std::opt::inline` | `run` | `run` |

这不是显示截断：`forward` 里那一格节点的原文是 `{"callee":"g2","label":"run"}`，而 `g2` 在同一个文档里就叫 `run`；同一次 dump 里 `g4/run$spec0`、`g5/run$spec0$spec2`、`g6/run$spec4` 都在。**对照行是关键的**：旧规则下"先特化再内联"仍然让 `forward` 调 `run$spec0`，新规则下变成 `run`。

**定位到的一步。** 只跑 `std::opt::specialize`（两版读数相同，因为这一步不涉及内联），各图的调用目标原文是：

```
main->(forward); run->(run); forward->(run$spec0); run$spec0->(run)
```

即：特化副本 `run$spec0` **自己体内的递归调用指向基函数 `run`**，不是指向副本自身。于是把 `run$spec0` 内联进 `forward`，`forward` 里剩下的就是副本体里那条**通用**调用——原来指向 `run$spec0` 的调用点被换成了指向 `run`。旧规则下这个副本不被内联（它到达递归），所以调用点一直保留在 `forward` 里。

两件事因此分开：

- 副本体内的递归调用指向基函数，本身是**特化只覆盖一层**（每次递归都回到通用体）；
- 新规则允许"单调用点、不在环里、但到达递归"的副本被内联，于是**调用者丢掉了原本指向副本的那个调用点**，退化到通用函数。

## 要问的一句

修法取哪一种：

- **特化副本豁免这条内联**：只要被调方带着 `$spec` 的副本身份，就不因"单调用点"或"返回闭包"被内联。代价是入口与特化链上的内联收益在这类程序上退回，且要给出"这是特化副本"的判据——现有的 `src/passes/opt/inline/engine.cpp#InlineDecline` 分类里没有这一项。
- **副本体内的递归调用指向副本自身**：把特化的固定点从"自调用基函数"改成"自引用"，副本才真正是它自己那一层的函数。代价是这一步要落在特化的那一侧（`src/passes/opt/inline/engine.cpp#returnsClosure` 之外的符号），并且会改变 `run$spec0 -> run` 这条现在被当作固定点的边，`std::opt::specialize` 的读数（图数、副本命名）会跟着变。

第一个选项与"特化副本是否还该存在"不是同一个问题：被否掉的是"副本作为中间产物必须留下来"，这里问的是**调用目标能不能退化**。

## 没量到的

- 副本体内的递归调用指向基函数，是特化那一侧有意为之（固定点的写法）还是缺陷（少了一次 `specializeDirectFuncInDraft`）；
- `run$spec0` 被内联进 `forward` 的那一步，`src/passes/opt/inline/engine.cpp` 里替换调用点的是哪一处落点；
- 其它带特化的程序是否同样退化（本页只量了 `shape_unroll.cml`）；
- 退化对执行结果没有影响（`std::opt::inline` 单独跑时 `forward` 本来就调 `run`，程序仍算对），本页只报调用目标，没量性能。

本页只报测量与一句询问：不改代码、计划、断言或生成物。