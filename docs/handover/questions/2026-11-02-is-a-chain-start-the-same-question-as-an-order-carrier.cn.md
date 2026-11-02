# `nodeTakesAnOrder` 与 `isChainStatement`：一个问题还是两个？

## 问什么

建图侧问"次序边能不能落在这个节点上"（`builder_helpers.h#nodeTakesAnOrder`），定型侧问"这个节点是不是某条链的语句"（`graph.h#nodeIsChainStatement`，之前是 seal 里两份拷贝）。两处答案在大约四类节点上不同，其中一类就是今晚量到的 `FUNC`：

| 节点 | `nodeTakesAnOrder` | `isChainStatement` |
| --- | --- | --- |
| `BRCH` / `LOOP` / `CALL` | 真 | 真 |
| 有副作用或可能出错的 `OPER` | 真 | 真 |
| 纯 `OPER` | 假 | 假 |
| `GATE` | 真 | 假 |
| `FUNC` | 真 | 假 |

已经统一的只是"同一个问题的那几份拷贝"：seal 里的两份（它们早就漂了——没有算子体的 `OPER` 一份答 `false`、一份答 `true`）、census、以及草稿上的 `GraphDraft::isChainStatement`，现在都读 `graph.h` 里那一处定义。建图侧那份**没有**并进去，它问的是另一个问题（`4.6`：次序边只排序，两端要是有副作用或可能出错的节点——所以链的**终点**可以是 `GATE`/`FUNC`；链的**起点**不行）。

## 为什么不能就这么并

今晚量过一次（`8e3eac8d5` 之前的 `55f8ae7cd`）：把 `linkSyncStatement` 的起点也按窄的那个谓词走，`FUNC` 起点那 63 条边没了，但重新锚定会跨过一个位置已经被占的语句，于是：

```
graph 'main': sync chain forks at node 19: statements 21, 23, 24 follow it,
              and a block's statements are one chain (edges.cn.md 5.3)
```

`fail` 3497 → 4313（+816）。所以问题不在"该问哪个谓词"，而在**"链的起点是个值"时，链该挂在哪儿**：跨过值去找它等的那个语句，可能撞上已经有 user 的语句；不跨，就写出一条被 seal 拒的边。

## 现在堵着什么

- 被拒的 `FUNC` 起点 63 条（`tutorial.examples.09_dependency`、`10_var_order`、`05_effect_order` 一类）；
- 起点侧剩下的 1112 条distinct 被拒边（`FUNC` 380、纯 `OPER` 370、`JOIN` 348、`GATE` 7、`CAST` 7）；
- `std::opt::inline` 之后 `JOIN` 成为链起点的那 17 条（含义还没读，"值越过 limit"那一族）。

## 要作者裁一句

链的起点是"必须是链上语句"（那就得回答：值那一格的位置由谁补，`closeChainOver` 够不够），还是"可以是一段值的上游"（那就得改 `edges.cn.md` 1/4.6，并说明 seal 为什么不拒）。这一句不落地，`kGCNodeFlagSyncStatement` 那 13 处也删不掉。