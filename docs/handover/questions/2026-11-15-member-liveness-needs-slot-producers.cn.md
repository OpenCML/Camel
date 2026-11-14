# 成员级活性需要的是「槽位 → 生产者」，不是「成员 = 输入」

日期：2026-11-15。分支 `lane-deadwrite`。相关页：`2026-11-15-the-acceptance-assumes-the-chain-starts-at-the-sweep.cn.md`。

## 为什么停下

「摘最后一个成员」这条规则的判据，建立在**一个错的读法**上：元组的成员就是它的普通输入，按顺序一一对应。
造一条真能走到判据的用例（`(x, h)`，`x` 是**参数**所以折不掉），读数立刻推翻了它：

```
AGGREGATE graph=probe node=12 arity=2 inputs=1 members=11:0, verdict=mapping-mismatch
```

`arity=2` 而 `inputs=1`。也就是说：**`Fill` 的成员不是它的输入**。
`GCFillBody` 的载荷是 `slotCount` 加一个变长的 `slots()`，而 `slots()` 的类型是
`std::span<const gc_slot_idx_t>`（`include/camel/runtime/graph.h:506`）——**帧槽号，不是节点引用**。
元组的成员住在帧里，聚合节点只是列了「我由这些槽构成」；那个唯一的普通输入是别的东西（数据/形状那一层）。

所以 `arity != normInputsOf().size()` 是**常态**，不是异常。判据里那道 `MappingMismatch` 守卫这轮
**第一次被真的触发**——它的存在是对的，但它触发的原因说明判据的**主语**取错了层。

## 按约定停在这里

这正好落在事先约定的停手条件上：「若『成员是否有人读』需要一个新概念，停下写页。」

它确实需要：要回答「第 k 个成员有没有活读者」，得先回答**「哪个节点写这个槽」**，也就是
**槽位 → 生产者**的反查；再顺着读者（`Accs` 的 `value` 是成员号，`value` 之上还有「读的是哪个聚合」）
回到「这个成员的值还有没有人看」。这不是元组一种容器的问题：`struct` 的成员、数组的元素、`Ref` 的内容
都是同一个形状——**成员住在存储里，容器只说布局**。

## 默认建议（等作者裁）

**先只在元组上落，但判据写成不依赖容器种类的一层，容器侧只提供「成员号 → 成员值」的映射。**
具体地：

- 一层 `memberProducer(draft, aggregate, index) -> gc_node_ref_t`：由容器给出「第 k 个成员的槽号」，
  再在图上反查写这个槽的节点。元组给 `slots()[k]`；将来 `struct` 给它的字段槽，数组给元素槽。
- 判据只问「这个生产者有没有活读者 / 这个聚合有没有整体逃逸 / 是不是根」，**不出现任何容器名**。
- 反查槽的写入者本身可能不唯一（多份写 = `WritingUnion`），那就必须**先抽取再判断**，
  这一层归 `writings_extract`；`select` 不改。

**不做这一步的后果**：现在这条规则在任何输入上都只会得到 `mapping-mismatch`，
它是一段**有守卫、没有主语**的代码；比没有这段代码更坏，因为它看起来像覆盖过了。

## 这一轮的附带读数

- `aggregate_two_members_reading`（新增，绿）：`probe node=12 arity=2 inputs=1 members=11:0`，
  把它留成一条**读数**而不是断言某个 verdict——错误的 verdict 不该被写成期望值。
- 另外五个 verdict 依然没有任何用例读到（见上一页的附表）；在判据换到槽位这一层之前，
  补守卫用例没有意义：守卫拦不住它看不懂的东西。
## 裁定的落法（2026-11-15，作者裁决）

**一、四条守卫已撤。** `droppable` / `aggregate-is-root` / `member-has-readers` / `last-member-is-read`
连同「摘最后一个成员」那一步一起删掉，`AggregateTailVerdict` 只剩两个**真的触发过**的值：
`not-an-aggregate` 与 `mapping-mismatch`。理由写进了枚举自己的注释：
**这四条守卫的前提（成员住在输入上）被 `arity=2 inputs=1` 证伪，留着只会让下一个人以为这里覆盖过了。**
撤代码不丢脸，留下看不懂的守卫才丢脸。

**二、`memberProducer` 这一层先不建。** 依赖是硬的：

> **上游「哪条规则负责删一个只被某张图持有的写」没答之前，这一层没有第一个调用方。**
> （见 `2026-11-15-the-acceptance-assumes-the-chain-starts-at-the-sweep.cn.md`）

那一页的读数已经说明：那条写第一次打印就 `dead=1`，闭包早已是一张图，清扫里没有任何对象可摘。
在「谁负责删」定下来之前建反查层，只会建出一层没有使用者的东西——这正是今晚反复出现的那种「看起来像绿」。

**三、新用例留成读数。** `dead_write.aggregate_two_members_reading` 只断言 `AGGREGATE graph=probe` 这一行存在，
**不写成断言某个 verdict**：错的 verdict 不该被写成期望值。
