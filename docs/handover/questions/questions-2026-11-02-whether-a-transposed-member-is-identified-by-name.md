# 被转置出来的成员，有人会"按名字"去找它吗？

**状态：`lane-m4b` 量出来的已知毛病 + 具名待办。今天没有任何代码按名字查成员，但名字是同一张 API 上人读的那一段，将来有人按名字查就会踩。**

## 两条读数（都是读代码读出来的，不是推测）

- **名字不参与任何查找。** 三处 `group.reserve("transpose_of_" + <name>(), 同一个字符串)` 里，名字只被存进 `RuntimeDraftIdentity{ .mangledName, .name }`；
- **`GraphDraftGroup::reserve` 不按 `mangledName` 去重。** 它体内第一行是 `const GraphId id = context_->graphIds().allocate();`，然后 `members_.emplace(id, …)`、`order_.push_back(id)`、`return id;`——**无条件开一张新图，没有一次查找、没有一次"已有就返回"**。

## 一条纪律

**语义位上的形参名不等于行为。** 那个形参叫 `mangledName`，"读名字就知道它按 mangled 名去重"是错的——它是被读了一次才确认的；这一页的第一版就是这么错的。同一条纪律适用于 `state.callees`：它的形参是 `callees`，键却是 `rt::toValue(calleeToTranspose->id())`，也不是名字。

## 今天的现状与风险

两张不同形参的成员可以**同名**（`transpose_of_rulesets.jvp::index_into<float>` 既是 `(float[], int, float) => (float[],)`，也在某处被当成 `(float, float) => (float,)`）。今天没有任何代码按名字查成员，所以两张同名图并存不冲突；**但任何一条按名字取成员的路径（断言、调试打印、将来的缓存）都会取到先建的那一张**——`ArrayEntry`/日志里出现的 `TRANSPOSE function=transpose_of_…` 就只有一个名字可读，分不出是哪一个形参。

## 要作者答的一句

**被转置出来的成员，身份应该只由"那张图的 id"承担（名字纯做人读，允许重名），还是名字也该带形参（`transpose_of_<name>(<形参>)`）？**
今天的实现是前者；4 条不变量正是在这个前提下被记成"同名两次构建"的边界。