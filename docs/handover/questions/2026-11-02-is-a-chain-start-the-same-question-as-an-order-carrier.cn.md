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

## 2026-11-02 晚：两次实测把问题从"谓词"挪到了"块撞上 join"

**`JOIN` 是怎么站到链上的。** `builder_control.cpp` 建完分支后：

```cpp
if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, joinNode)) {
    markSyncStatement(lastSyncedNode_);
    markSyncStatement(joinNode);
    linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);
}
lastSyncedNode_ = joinNode;
```

它上面那段注释说明为什么这条边非写不可（"the layout reads the join's sync predecessor as the statement written before the branch"），`lastSyncedNode_ = joinNode` 则让分支之后的下一条语句从 join 起链 —— 于是链的起点是一个 `JOIN`，`nodeTakesAnOrder` 说它是（能排序），`isChainStatement` 说它不是（不做事）。最小片段是 `docs/tutorial/examples/10_sync_block.cml`：

```cml
func report(second: bool): int sync {
    println('the body is a sync block')
    if second then { println('three'); println('four') }   // 这条 if 产生 join
    if second then sync { println('five'); println('six') } // 它的起点就是这个 join
```

**实测一：让值退出链位（`writeSyncLink` 里先从值手里收回位置，再用 `setSyncInputs(id, {})` 清掉）。** `linear.cml` 的违例 3 → 2（`14 -> 16` kind 11 消失）；`feat.trans.cml` 0/3 → 1/3（`print_then_run_negative_swap_is_seen` 转绿）；全量 8517 / pass 5398 / fail 3099（基线 8515/4736/3759，**fail −660**），**`sync chain forks` 全量为 0**。但 `tutorial.examples.10_sync_block.{nvm,fvm,jit,prl}` 四条转红：`report` 拒 `sync edge 3 -> 17 … kind 7`，链位被交给了 join。已回退（`b3fa14bae` 留档）。

**实测二：让 seal 把 `JOIN` 也当链语句（`nodeIsChainStatement` 加一个 `case`）。** 全量 8519 / pass 4701 / fail 3798，**比不放开更差 39 条**；所以"是不是语句"这一问 seal 拒得对，缺的不是放宽，而是建图侧在块撞上 join 时该把链位交给谁。

**还缺的那一句**：一个块的分支之后如果还有语句，链位应延续到那些语句；如果 join 之后没有语句，链位应停在 join 之前最后一条语句 —— join 自己是控制流的汇合，不是"做过一件事"。按这个说法，`lastSyncedNode_ = joinNode` 就是这次实测暴露的那一行；但它是上面那段注释刻意写的，改它之前要先回答"分支的块值由谁当"，所以仍归作者。
## 2026-11-02 夜：默认 (b) 试过了，代价是分支不再执行

按"join 不是语句、链位停在 join 之前"（默认 b）改了一行 —— 保留 `linkNodes(LinkType::Sync, lastSyncedNode_, joinNode)`（布局要读它），但**不再** `lastSyncedNode_ = joinNode`、也不再给 join 打语句标记：

```cpp
if (lastSyncedNode_ != nullptr && linkCheek(lastSyncedNode_, joinNode)) {
    markSyncStatement(lastSyncedNode_);
    linkNodes(LinkType::Sync, lastSyncedNode_, joinNode);   // 边照写
}
chainedNodes_.insert(brchNode);                              // 链位不动
```

读数（在"值退出链位"那笔之上）：全量 **8521 / pass 5518 / fail 2983**，比只有前一半时的 8521/5377/3124 **再少 141 条红**，`sync chain forks` 仍为 **0**。`tutorial.examples.10_sync_block.{nvm,fvm,jit,prl}` **不再被 seal 拒**，程序跑起来了 —— 但输出只剩 `the body is a sync block` 和 `0`，而 golden 要的是分支里的 `three/four/five/six`，编译器的警告正是 `The branch has side effects but is never executed`。

⇒ **这是"链位不动"的直接代价：分支里的语句再没有谁等它们，副作用被丢掉。** 比 seal 拒绝更坏（拒绝是明显的，丢弃是安静的），所以这一行已回退（`git checkout --`）。默认 (b) 不成立，问题回到作者那句："分支的块值由谁当" —— 分支必须继续有东西等它，而那个东西现在只能是 join。
