# 版本边算不算"两次写同一资源的调用之间的排序"

**状态**：等作者裁。

## 一、问题

两条写同一个 `var` 的调用，今天由**控制边**排序。加了版本边之后，第二次调用经由 `:var/after(cell, call1)` 拿到第一次调用之后的单元——**这是一条数据边**。资源检查器随即报：

> `bump: the two calls are not ordered by control: RESOURCES node main 8 FUNC bump: rmw variable (node 7) | RESOURCES node main 9 FUNC bump: rmw variable (node 10)`

**要裁的是**：**数据边（"第二条读到了第一条的产出"）是否已经足够作为这两次写之间的排序凭据，还是资源检查器的"必须由控制边排序"仍然成立。**

## 二、最小程序

```cml
func bump(var t: int): int uses t sync {
    return 0
}

func main(): int sync {
    var cell = 0
    bump(cell)
    bump(cell)
    println(cell)
    return 0
}
```

（取自 `test/cases/effects/declared.cml`；该文件里 `bump` 的两次调用构成同一个单元上的两次写。）

## 三、两种读法与代价

- **数据边算数**：检查器的这条规则要放宽——同一单元上的两次写由"后一次读到了前一次的产出"排序即可。代价是检查器要认"版本边也是排序"，而那正是 3.1 说的"用到"。
- **只有控制边算数**：那么 `:var/after` 在**两次写之间**不能只给数据边，还得同时造一条控制边——即版本边解决"读拿到哪个值"，控制边继续解决"写的先后"。代价是每个 `:var/after` 都要多一条边，且那条边不再是 3.1 的凭据，而是检查器的要求。

## 四、今天量到的事实

- 加了 `isCallNode`（`Call` 与 `Func` 都算调用）之后，`:var/after` 第一次真的出现在图里（`N0C [label=":var/after", type="OPER", value_type="var int"]`）；
- `callback_write_ignored.cml` 仍打 `3`（**但这仍属"没弄坏"栏**）；
- 整闸从 0 红变成 **14 红**，其中 **2 条**是这条规则直接报出来的（`source.declared_resources` 与它的 poison 副本），另有 2 条是"黄金期望里还写着控制边"（`order_graph.before_after`、`order_direct_user.graph`）。

## 五、判定这一页解没解

- **若作者裁"数据边算数"**：检查器这一条要放宽，那 2 条红随之消失，另外 2 条黄金需要重录；
- **若作者裁"只有控制边算数"**：`:var/after` 旁边要补一条控制边，那 2 条红按"检查器仍对"处理，黄金不动。