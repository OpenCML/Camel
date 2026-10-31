# 一问：`GraphDraft` 要不要压到 0 处？——b6 的天花板是 **188** 处

> 状态：**等作者裁决。**
> 相关：`docs/handover/reviews/graph-store-step2-b-ledger.cn.md` §53（三档的天花板）；§44（`GraphDraft` 本体不是 find/replace 的对象）；`questions-2026-10-31-gir-handle-names-a-transaction-or-a-draft.md`（同一层剩下的另一个洞）。

## 数

`GraphDraft` 今天 **356** 处（36 个文件）：**188** 处降不掉、**154** 处能降但有代价、**14** 处是注释。

那 **188** 处 = 类外成员定义 **175**（`<ret> GraphDraft::name(`，类型自己的体）+ `draft.h` 里类型自己的声明 **12**（类头、`decode` 工厂、构造、析构、拷贝删除 ×2、赋值删除 ×2、`const_cast<GraphDraft *>(this)`、`DraftNodeHeader::owner`、`WritingScope` ×2）+ `graph.h` 的 `friend class GraphDraft;` **1**。**它们拼的都是类型自己的名字，不改名就没有一处能降。**

## 三条路，各一句代价

- **① 改名**（`GraphDraft` → 语义更准的名字，比如 `DraftGraph`／`EditableGraph`）：**代价 = 那 188 处的名字本身要一起动**——拼写归零而代码结构一行不变，但 diff 铺满每一个 `draft*.cpp`，且合并时每条在飞的车道都要在这几行上解一次冲突。
- **② 删类型**（引用归零）：**代价 = 154 那档里靠 `borrow` 活着的 59 处、以及会话自己的门 27 处，要另找宿主**——"谁能拿到这张可写的图"这一问要从头答一遍，等于 b3／b4 的所有权模型重做。
- **③ 接受 188 处当边界**：**代价 = 设计稿里要写明"这一层到此为止"**，`GraphDraft` 从此是一个有名字的边界类型，账本的 `files`／`references` 就停在 **36 / 356**，不再往下降。

**默认无建议。** 三条都不是缺陷，是三种不同的账。

**要作者答的一句**：`GraphDraft` 压到 0 处（①改名或②删类型），还是**接受 188 处当边界**（③）？

**判据**：**"一层降不动的终点不是 0 处，是'我们知道它到哪儿为止'——然后拿这个数去问要不要改名。"**