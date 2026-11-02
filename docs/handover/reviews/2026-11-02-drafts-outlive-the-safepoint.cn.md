# safepoint 上活着的 draft，谁来改写它的静态槽

一个 `GraphDraft` 把静态值放在它自己的 `staticSlots_` 里（malloc 堆上，不是 GC 对象），所以回收器扫不到它们。draft 跨过 safepoint 之后，槽位要么由持有者自己改写，要么留着旧地址——留着，就是在回收器已经把对象搬走之后去标记一个旧地址。

今天这两条路都试过，结果是**两头堵**：

- 让一个外部 tracer（一张"所有活着的 draft"的表）代替持有者去改写槽位——**收 `built`（各遍自己开的那些 draft），任何包含它们的子集，都在启动期 `EncodeSession::run` 的 `collectCompileStaticGraphRefsRecursive` 里崩**；
- 只收 `borrowed`（从别处借来的 draft）——**与不装这个 tracer 时一模一样，缺陷照旧**。对照组证明它对结果影响为零。

而在 safepoint 上活着的 draft，**全部**是编译/优化层自己开出来的（`GraphTransaction::open` / `borrow`），**没有一个是会话的**。会话路径上那张表恒为空，所以这不是"会话层的 draft 没被覆盖"。

**⇒ 改写静态槽的人，只能是持有者自己。**

## 问

**是"静态槽的搬迁必须由持有者自己做（别人无权代劳，因为别人的时机和它自己的 safepoint 不是同一个）"，还是"那些 draft 本不该活到 safepoint"？**

## 两条路

- **A：持有者自己在自己的 safepoint 上搬自己的槽。** 改编译层与各优化遍：谁跨 safepoint，谁在自己的 safepoint 上把自己的静态值搬一遍。改动面大，但责任清楚，也和"一个对象被搬动时，凡是持有它地址的每一处都必须被改写"这条直接接上。
- **B：先让那些 draft 不活到 safepoint。** 查"活着的 draft 数在 safepoint 上只增不减"是不是泄漏；改动面未知，而且"是泄漏"目前没有证据。

## 默认

**A**——B 的前提（是泄漏）还没有证据，而 A 是上面那条一般结论的直接推论。

## 附：这条线上已经查清、不必再查的两点

- `sessionOwned` / `sessionDrafts` 从来不是代码里的名字，只是探针名，探针已随回退消失；`draftRootSessionsWithDrafts` 只出现在 `src/runtime/draft_session.cpp`，且只在会话 `RootScope` 开着时非零——**会话路径上这张表恒为空**。
- 每个 draft 的静态槽里，"按声明类型该扫"的很少、"填了但不该扫"的居多；把这些不该扫的也交给回收器，是把一个旧槽位换成一个崩溃。

（本页不写读数；读数在提出本页的那笔提交信息里。）