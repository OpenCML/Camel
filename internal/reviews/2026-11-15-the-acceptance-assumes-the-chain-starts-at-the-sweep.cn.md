# 验收的前提不成立：这条链不是从清扫开始的

日期：2026-11-15。分支 `lane-deadwrite`。相关页：`2026-11-14-what-makes-a-reader-alive.cn.md`。

## 验收预设了什么

作者的验收是 `dead_write.opt.still_there` 从「它仍是 `dead=1`」翻成「`.gone`」。它预设了一条三段链：

1. 元组 `p = (1, h)` 的成员读着闭包，
2. 清扫因此看见「有人读」，把闭包收走，
3. 闭包没了 ⇒ 格子没有活读者 ⇒ 那条写变成真死写 ⇒ 才轮到边界检查说拒还是删。

**这条链的第一段在今天的树上不存在。**

## 读数（同一条用例，`test::deadwrite` 打印）

**编译输出、还没跑 `std::opt`**：

```
DEADWRITE graph=probe node=6 oper=:op/assn_l rooted=0 read=0 dead=1
DEADWRITE graph=main node=1 oper=:io/println rooted=0 read=0 dead=0
DEADWRITE graph=__g_49__ node=1 oper=:io/println rooted=0 read=0 dead=0
AGGREGATE graph=probe node=11 arity=1 inputs=1 members=10:0, verdict=not-an-aggregate
FUNC graph=main node=0 normUsers=1 withUsers=0 ctrl=1 order=0 sync=1 closureNode=0 unreadClosure=0
SWEEP func=0 call=0 gate=0 oper=0
```

**跑完 `std::opt`**：同一份读数，一字不差；把「摘最后一个成员」那一笔**整个停掉**再跑，仍然一字不差。

## 三件事，逐条

**一、那张图里没有两成员的元组。** `probe` 只有一个聚合，`arity=1`，成员是 `node 10`（kind 0 = **`Data`**）。
它不是 `p`，而是这个图**结果的单槽元组**。`p = (1, h)` 在**编译期就被折叠掉了**：`p.0` 的成员是常量 `1`，
读它就等于读那个常量。所以不是「某个 pass 重造了少一个成员的元组」——**是它从来没有以两成员的样子出现在图上**。

**二、闭包不是 `Func` 节点，是一张图。** `probe` 的闭包体成了 `__g_49__`（图名由
`src/compile/gir/builder_helpers.h:398` 的 `__g_{id}__` 给出，编译期就定了）。全程序唯一的 `Func` 节点是
`main` 的 0 号，`unreadClosure=0`（它有 ctrl 与 sync 用户）。
所以 `isUnreadClosure` 在这条用例上**没有对象**：它问的是「这个 `Func` 没人用吗」，而闭包早已不是 `Func`。

**三、那条写在**第一次**打印时就已经 `dead=1`。** 判据早就给了答案，缺的是**删**，而删的入口不在清扫里。

## 结论

1. **验收的三段链，第一段不存在**：没有「元组提着闭包」这件事，所以「清扫收走闭包」这一步无从发生。继续在清扫里找办法，是在修一段不存在的链。
2 这一笔「摘最后一个成员」因此在**今天所有能构造的用例上都没有对象**：六个 verdict 里五个从未出现过，
   每条用例打出来都是 `not-an-aggregate`（见下节）。它是一段**没有主语**的规则。
3 **下一问换了**：不是「谁重造了元组」，而是**「今天闭包以什么形式存在，以及什么规则负责删掉一个只被某张图
   持有的写」**——一个已经被判 `dead=1`、却没有任何规则去删的写。

## 附：六个 verdict 的可达性（本轮读数）

| verdict | 有没有用例真的读到它 |
| --- | --- |
| `not-an-aggregate` | 有：五个用例**全部**只读它 |
| `aggregate-is-root` | **没有**（`is_root` 用例里连 `AGGREGATE` 行都没有：那张图没有聚合） |
| `member-has-readers` | **没有** |
| `last-member-is-read` | **没有** |
| `droppable` | **没有** |
| `mapping-mismatch` | **没有** |

四条「守卫」用例今天都是靠 `SWEEP func=0` 变绿的，而那个 0 与守卫无关：**它们连聚合都没走到**。
按「一条永远不会出现的状态名，和一条永远为真的断言是同一种坏」：**这五条现在都算装饰**，
要么造出真正多成员的聚合（两个非常量成员），要么把这条规则连同它的枚举一起撤掉。
## 决策者答复（2026-11-15）

读数对，验收的前提是我写错了：中间链不存在，那条写从一开始就判为 `dead=1`。缺的只有删除的入口，也就是"活读者"一页的第 3 步：`dce` 删除 `deadWrite` 判为死的写，`isObservedRun` 问 rooted 半边。冻结图那一侧的活集合已经落地（`2e5504b8e`），边界检查与判据现在给同一个答案，这一步可以开工。删除时经编辑接口交接位置（同日"谁接替被遍取走的持位节点"的答复）。验收不变：`dead_write.opt.still_there` 翻成 `.gone`，`linear.var_reference`、`effects_kept` 全绿，E9 慢层三个数不升。
