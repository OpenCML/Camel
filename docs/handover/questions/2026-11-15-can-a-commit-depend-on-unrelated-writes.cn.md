# 同一个图的提交结果，能不能取决于同一事务里有没有别的写入？

**状态**：待裁，**需要作者定方向**。这一页比 `cse` 那条红重要：它动摇的是「提交是否可复现」。

## 1. 事实（两次提交，只差一笔无关写入）

一个独立程序（`/tmp/e9/repair.cml`，不经任何遍）：打开事务、把 `:var/get` 写成 `:op/assn_l`
的 sync 前驱、提交。B 版只多一句 `add_constant`（在一张图上挂一个不用的常量）。

临时读数打在 `RuntimeGraphDraftSession::commit` 的整理循环上（`src/runtime/draft_session.cpp`
的 `normalizeChainPlaces` 那一段）：

```
=== A：只写这一条边
[commit-probe]  graph=written drafted=1 writings_empty=1 normalize=0 sync=2->2 mutations=10
[commit-probe2] graph=written heldBefore=1 drafted=1 mutations=10 stillEdited=1
after %8 :op/assn_l  sync_in=0            ← 边没了
after %13 Gate       sync_in=1

=== B：同一条边 + 一次无关写入
[commit-probe]  graph=written drafted=1 writings_empty=1 normalize=0 sync=2->2 mutations=16
[commit-probe2] graph=written heldBefore=1 drafted=1 mutations=16 stillEdited=1
after %8 :op/assn_l  sync_in=1            ← 边在
after %13 Gate       sync_in=1
```

**两次的整理都没有跑**（`normalize=0`），**两次 `writings()` 都是空**，**两次图都被登记为已编辑**
（`stillEdited=1`），**两次草稿里的 sync 边数都是 2**（原有 1 条 + 新写 1 条）。唯一不同的是
`mutations` 的计数，以及**提交后这条边在不在**。

所以：**同一个图、同一条边、同一段提交代码，结果取决于同一事务里有没有一笔无关的写入。**
「无关」是字面意思：B 里多出来的那个常量节点与这条边没有任何关系。

## 2. 这条事实推翻的两个猜测（都写在现场）

1. **不是 `writings().empty()` 的分叉。** 上一轮怀疑的是这行：

   ```cpp
   // src/runtime/draft_session.cpp:1616
   if (draft.writings().empty() && draft.normalizeChainPlaces() > 0) { edited_.insert(graph); }
   ```

   读数说两次 `writings_empty=1`、两次 `normalize=0`，**这一行两次的判断完全一样**。它不是原因。

2. **不是「整理把边整理掉了」。** 整理两次都没有动作（`normalize=0`），而草稿里的边数在整理
   前后都是 2。

于是嫌疑落到整理之后的编码/取舍：`commit` 里「编码回来与原文一样就沿用旧版本」那一段
（`draft_session.cpp:1697` 起：「An edited graph encoded back to its own content is left as it
is」）。**A 里提交结果没有变化，B 里有变化**——如果 A 的编码把这条边丢了，编码结果就与旧图相同，
于是旧版本被沿用（边自然不在）；而 B 因为有那个多出来的节点，编码结果与旧图不同，新版本被采用，
边就跟着一起出来了。**这只是当前最可能的形状，尚未证实**（要证实它需要在编码之后比对内容）。

## 3. 为什么这件事比 `cse` 大

- **`by_cpp` 的绿可能是个意外。** C++ 的 `cseDraft` 在**同一笔事务**里既折叠又替换，
  那笔无关写入让「编码结果与旧图不同」成立，于是新版本被采用，那条边活了。
  **换句话说：`by_cpp` 绿不是因为那条边合法，而是因为那次提交恰好有别的内容变了。**
  这一点必须先证实或推翻——否则我们是在为「保住一条本来不该存在的边」写代码。
- **提交不可复现**：两个程序若只在无关的写入上不同，提交出来的图会不同。这对任何
  「拿遍的产物做快照 / 做 golden / 做增量」的机制都是隐患。
- 它和另一页（`2026-11-15-what-does-an-edit-promise.cn.md`）是同一枚硬币的两面：
  一面是算子不报告「没落地」，另一面是**落地与否取决于无关的写入**。

## 4. 默认（本页写死）

**提交结果不能取决于同一事务里有没有别的写入。**

- 整理（`normalizeChainPlaces` 一类）必须在**固定点**上无条件执行，与 `writings()` 无关；
- 若某条边在整理后不成立，说明它**不是这个图能持有的边**，编辑算子应当**在整理之后**检查并
  返回「未落地」，而不是让「这条边在不在」取决于别的写入；
- 编码与旧图的比较只应决定「要不要一个新版本号」，**不应当决定要不要保留一次编辑**。

## 5. 两个方向，都不由这一页决定

1. **无条件整理** ⇒ `cse` 里那条 `N0A -> N0E` 的 sync 边**根本不该存在**：`:var/get` 是值不是语句，
   让它当链上前驱这件事本身要被整理掉。那么 C++ 侧的 `placeLostStatements` 与 Camel 侧的
   `place_lost` 都要改写法（不是补边，而是把位置交给**语句**）。
2. **提交永不整理** ⇒ 模块提交保得住这条边，但要解释为什么整理不该在提交时跑、它该在哪跑。

两个方向的代价完全不同，**这是设计决定，不是实现细节**，要作者裁。

## 6. 一句可以带走的话

> **不是「不可判」，是「判错了」——它判的是草稿，而交付的是图。**