# 存活副本：新内联规则之后 `std::opt` 不再留下任何特化副本

## 我量到的

同一个用例 `test/cases/opt/generic/specialization_survives_the_round.cml`，同一条管线 `std::opt std::stats std::jgir`，唯一的变量是 `src/passes/opt/inline/engine.cpp` 里"调用点只有一个"与"返回闭包"这两条豁免的条件：现在看**被调方自身是否在递归环里**，此前看的是**它不"到达"递归**。

| | 图数 | `walk$spec*` | `specializations` | nodes | edges | opers | direct_calls | bytes |
|---|---|---|---|---|---|---|---|---|
| 改前 | 17 | 12 | 12 | 72 | 63 | 23 | 3 | 2692 |
| 改后 | **5** | **0** | **0** | **60** | **63** | **23** | **3** | 2356 |

改后存活的图只剩 `__root__`、`main`、`spin`、`tick`、`walk`；`walk`／`tick`／`spin` 仍在，因为它们在递归环里，不被内联。

**这组读数里最要紧的是一行相等**：`edges 63`、`opers 23`、`direct_calls 3` 两档**完全相同**，而 `nodes` 少了 12、`graphs` 少了 12——差值正是 12 个副本图各带 **1 个节点**。也就是说**执行到的程序没有变**，消失的只是 12 个每个只带一个节点的副本图，不是被内联掉的代码。所以这不是"内联让程序执行到别的副本"那一类缺陷。

连带两条用例失效：

- `specialization_survives_the_round.copy_is_reused_in_a_later_round`：它断言存活副本的名单是写死的 12 个 `walk$spec*`，并要求其中某个副本在文档里被命名两次（"这个值第一轮已经绑过，后来的请求拿回同一份副本"）。现在表里没有副本可数，这两条都失败；而"`stats.specializations` 等于名单长度"（0 等于 0）与"不保留 `tick$` 副本"两条仍然成立。**它要问的那句"存活下来的副本正是程序执行到的那些"因此变得不可观测**：副本在造出来的那一轮就被内联掉了。
- `shape_unroll.simplify.rgir`：它断言 `forward` 里没有 `BRCH`。现在 `std::opt` 会把一层展开内联进 `forward`，`forward` 的节点序列里出现了 `BRCH`（与 `FUNC` 相邻），这条前提失效。

## 要问的一句

新规则之下，**特化副本是否还该存在**：

- **接受这个效果**：我们把上面两条用例改成断言"该程序不再产生存活副本，且 `edges`／`opers`／`direct_calls` 三数不变"。代价是"特化属于程序而不属于会话、后续轮次拿回同一份副本"这条保证在测试里**不再有人盯着**，`forward` 不分叉那条结构前提也一并按新形状重写。
- **特化副本应当豁免**：副本不因"调用点只有一个"或"返回闭包"被内联。代价是入口内联在这类程序上的一部分收益退回，且需要给出一个能判"这是特化副本"的判据——`src/passes/opt/inline/engine.cpp#returnsClosure` 与 `src/passes/opt/inline/engine.cpp#InlineDecline` 现有的分类里都没有这一项。

我们两个数都量到了，不替这条判断做决定。

## 没量到的

- 那 12 个副本各自的**那一个节点**是什么种类、指向谁（只量到总数少 12）；
- `walk$spec*` 是被**内联掉**还是被**直接丢弃**：两种情形下 `edges`／`opers`／`direct_calls` 都不变，本页没有把两者分开；
- `shape_unroll` 里 `forward` 新出现的那一格 `BRCH` 来自哪一层展开；
- 其它带特化的程序（本页只量了 `specialization_survives_the_round.cml` 一个）。

本页只报测量与一句询问：不改代码、计划、断言或生成物。