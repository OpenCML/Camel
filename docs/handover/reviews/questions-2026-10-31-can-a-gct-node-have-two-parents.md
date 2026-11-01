# 一个 GCT 节点能不能有两个父亲：`Tree::append` 的“未挂载”不变式被 `match` 守卫用例打破

**问题**：`src/compile/gct/tree.cpp:284` 的 `Tree::append` 要求 `(child->flags_ & Node::kAttached) == 0`（“Node already has a parent”），而 `test/cases/compile/.../match_guard` 那条守卫用例（`poison:compile.match_guard:match_guard.first_match_wins.nvm`）在**合法程序**上让一个已挂载的节点被再次 append。

**为什么要问**：这棵树在代码里叫 `Tree`，`append` 还会把 `kAttached` 置上、拒绝第二次挂载——形状是“一棵树”。但守卫用例走到这里说明要么构建器**有意共享子树**（那它其实是 DAG，标志位和名字都该改），要么这是**构建器的缺陷**（同一个节点被两个父亲拥有，之后遍历、改写、销毁都会看到它两次）。release 档里这条 `ASSERT` 是死的，程序一直跑；把它变活之后立刻中止，所以这是“检查第一次真正被执行”暴露出来的问题，不是这次改动引入的。代码没动。

**实测**（`ccbc54293` 之后，`camel match_guard 用例 std::nvm`，`feat/mm` 的 fast 层）：

```
Camel Invariant Violation:
    Location   : ../src/compile/gct/tree.cpp(284)
    Function   : void camel::compile::gct::Tree::append(Node &, Node *)
    Guarantee  : (child->flags_ & Node::kAttached) == 0
    Reading    : Node already has a parent
```

用例 stdout 为空，退出码不是 0——也就是说这条不变式不成立时，**那一刻之后什么都没跑**。

**两种可能，判不了是哪一种**：

- **A. 构建器错**：`match` 守卫的降级里某个节点被重复挂载，`kAttached` 和 `Tree` 的名字都在说“这不该发生”，抓到了一条真缺陷。
- **B. 树其实是 DAG**：共享子树是设计（例如同一个字面量/同一段 `match` 分支体被两处引用），那么 `kAttached` 这个一次性标志就该换成父计数（或干脆去掉），`Tree` 的名字也要说清楚。
- **C. 别的**：例如再挂载时应当先克隆（谁克隆谁负责销毁），由作者定。

**问作者的话**：GCT 里一个节点可以有几个父亲？如果是 A，我留下这处 `ASSERT` 就不该再留，应跟着修构建器；如果是 B/C，`append` 该换成什么条件才说得清“谁会让它到达”。

**堵着谁**：`src/compile/gct` 这一族检查的收尾——63 处里 62 处已经搬完（54 处 `GUARD`、8 处内部错误），剩这一处等裁；不裁的话它在 release 里继续是死的，“0 处”这个读数也就报不出来。