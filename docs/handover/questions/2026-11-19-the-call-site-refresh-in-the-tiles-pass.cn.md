# 调用点刷新在 tiles 遍里为什么没落到发布程序上

日期：2026-11-19 ｜ 车道：`wide` ｜ 状态：交下一手（`region_outline.cpp` 已划给 order 族车道）

## ① 症状

`gpu_scope.report_reads_the_segments_a_pass_cut`：
`... node 1 of graph '__root__' (a call) declares uses var stdout, tiles::driver, any unknown; the graph it runs touches write tiles::driver`
`literal.tensor_element_type_gpu_scope_compiles`：同上，但声明是 `uses var stdout, any unknown`。
第一条**有**这个名字却仍判不覆盖 ⇒ 差在 **mode**（`sem::covers` 对 ambient 走 `coversMode`，`read` 不覆盖 `write`）；第二条没有这个名字。

## ② 已排除的六类（每条一句）

1. **时机**：`prepared()` 在提交前就给出「提交要判的那几张图」，加宽本来就在提交前。
2. **图集合**：`collectReachableRuntimeGraphs() ∪ madeGraphs()` 补上后读数一条不动。
3. **缓存键**：`prepared()` 的拷贝原先不 bump revision（`footprintOf` 按 `(closureRevision, epoch)` 缓存）—— 真隐患，`4b77d71d0` 已修，**但不是红因**。
4. **造的图没声明**：同第 2 条。
5. **认不出调用点**（我修正过一次）：打点显示第 1 轮的 copy 里有那个值节点（`dataIndex=-2`、`TypeCode::Function`、`names=48`），`wider` 里也有它，草稿被打开。
6. **下游丢值**（我修正过第二次）：`test::value_retype_twice`（`768a6476e`）对同一值连做两次 `materializeStaticValue`，编码后的程序携带的是**第二次**的类型 ⇒ 草稿接口这一侧幂等。

## ③ 剩下的位置

`modules/tiles/src/region_outline.cpp` 的 `declareBodiesOverDrafts`，那段「A value that names a graph whose clause grew」：**认得出、草稿也打开了，但没落到发布程序上**。关键读数：**第 0 轮落得下去，第 1 轮落不下去**。

**一个已被否掉的猜测**：我怀疑过草稿里该值节点的 `dataIndex` 符号与拷贝不同，于是按 `dataIndex < 0` 的扫描第二轮扫不到它。读过 `src/runtime/draft.cpp:2773` 后否掉 —— `materializeStaticValue` 给的也是负的 `dataIndex`。

**下一手该在哪打点**：不要在 copy 上打，要在**草稿内部**打 —— 只有 `found->second == nodeType` 这条判据还能解释「草稿打开了、值却没换」。要看三样：draft 里该节点的 `id`、`dataIndex`，以及 `header->dataType` 与 `found->second` 两个 `Type *` 是否相等。相等 ⇒ 草稿里已是新类型，问题在编码/发布（`verifyDraftDelivery`、`staticDataType` 的落点）；不等 ⇒ 判据短路了，改判据即可。

## ④ 给 order 族车道的一个问题（只看一眼，不必改）

补完 `transferAggregate` 的 `draft.appendOrderDependency` 之后，**这段刷新循环需要跟着动吗**？我预期不需要（它只读 clause 与调用点，不读边），但补边会改变 `bodySignature` 的足迹，请确认。