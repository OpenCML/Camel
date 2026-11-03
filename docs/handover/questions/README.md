# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：42 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-is-a-chain-start-the-same-question-as-an-order-carrier`](2026-11-02-is-a-chain-start-the-same-question-as-an-order-carrier.cn.md) | `nodeTakesAnOrder` 与 `isChainStatement`：一个问题还是两个？ |
| [`2026-11-02-the-gc-soak-step-is-not-the-eq-rewrite`](2026-11-02-the-gc-soak-step-is-not-the-eq-rewrite.cn.md) | `gc_soak` 的那一跳不是 `!=` 改 `!(==)` 造成的 —— 作者 12:10 的机制假设被读数证伪，预算怎么办 |
| [`2026-11-02-what-carries-the-residuals`](2026-11-02-what-carries-the-residuals.cn.md) | 残差用什么承载？ |
| [`2026-11-02-who-carries-the-chain-place-after-a-branch`](2026-11-02-who-carries-the-chain-place-after-a-branch.cn.md) | 分支之后，链位由谁承接？ |
| [`2026-11-03-a-structural-nodes-chain-position-belongs-to-its-join`](2026-11-03-a-structural-nodes-chain-position-belongs-to-its-join.cn.md) | 结构节点的链位在它的 JOIN 之后，不在它自己身上 |
| [`2026-11-03-does-a-mark-encode-shape-or-history`](2026-11-03-does-a-mark-encode-shape-or-history.cn.md) | 一处标记编码的是"形状"还是"已发生过的事件"？ |
| [`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float`](2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md) | 反传倒着走要能钉住，先要循环状态不是浮点标量 |
| [`2026-11-04-a-case-with-no-plan-entry-and-a-main`](2026-11-04-a-case-with-no-plan-entry-and-a-main.cn.md) | 一个用例带着 `func main` 却没有任何计划条目：删掉，还是给它写一条？ |
| [`2026-11-04-a-draft-node-asked-of-the-source-graph`](2026-11-04-a-draft-node-asked-of-the-source-graph.cn.md) | 余切类型问的是哪张图：造图节点被当成源图节点来问，回退值该不该继续静默说 `float`？ |
| [`2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep`](2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep.cn.md) | `^` 的反向半读不了切向的值：那句守卫留不留，`(-3.0) ^ 2.0` 答 `-6.0` 还是 `NaN`？ |
| [`2026-11-04-a-member-read-is-not-a-parameter-read`](2026-11-04-a-member-read-is-not-a-parameter-read.cn.md) | 读一个参数的成员，不是读这个参数：`transpose` 该跟到端口吗 |
| [`2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction`](2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction.cn.md) | 为什么 `sqrt`/`log`/`divide` 的两腿用例停在“一个参数拿不到余切，因为没有东西用它” |
| [`2026-11-04-a-struct-field-read-refused-or-copied-by-key`](2026-11-04-a-struct-field-read-refused-or-copied-by-key.cn.md) | 一个 struct 字段读（`qq.a`）：切里按名字拒，还是把 key 抄过去 |
| [`2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out`](2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out.cn.md) | `autodiff` 能不能在改写时自己把 `split` 造出来的那个值造出来？ |
| [`2026-11-04-does-a-build-gated-case-count-as-a-name-pin`](2026-11-04-does-a-build-gated-case-count-as-a-name-pin.cn.md) | 只在 no-JIT / arm64 构建里才跑得到的用例，算不算把这条码按名字钉住了？ |
| [`2026-11-04-does-a-closure-members-cotangent-have-a-slot`](2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md) | 闭包成员的余切应不应该有一个槽？ |
| [`2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it`](2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it.cn.md) | 一条转置律没被一阶走查读到，算不算"律已经补上"？ |
| [`2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value`](2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md) | 转置成员要不要给"一个参数自己的值"留一个端口？ |
| [`2026-11-04-does-a-with-value-get-a-tangent`](2026-11-04-does-a-with-value-get-a-tangent.cn.md) | `with` 值该不该有自己的切向？ |
| [`2026-11-04-does-pick-at-need-a-forward-rule`](2026-11-04-does-pick-at-need-a-forward-rule.cn.md) | 问题：`pick_at` 该补一条**前向**律吗？ |
| [`2026-11-04-does-the-engine-need-a-pick-at-rule`](2026-11-04-does-the-engine-need-a-pick-at-rule.cn.md) | 问题：引擎的反向表要不要给 `pick_at` 补一条规则？ |
| [`2026-11-04-does-the-seal-check-judge-the-marker-or-the-statement`](2026-11-04-does-the-seal-check-judge-the-marker-or-the-statement.cn.md) | 同步边的封口判据：问标记，还是问"这个节点是不是语句" |
| [`2026-11-04-does-the-transpose-walk-still-refuse-by-arity`](2026-11-04-does-the-transpose-walk-still-refuse-by-arity.cn.md) | 问题：转置走图还按操作数个数拒一个算子吗？ |
| [`2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape`](2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape.cn.md) | `max_axis` / `min_axis` 的孪生算子：并列极值的选择怎么随形状走？ |
| [`2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal`](2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal.cn.md) | 开秩数组的零，是空数组还是明确拒绝 |
| [`2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape`](2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape.cn.md) | 乘法的伴随要不要读操作数自己的形状？ |
| [`2026-11-04-should-the-compiler-version-invalidate-an-install`](2026-11-04-should-the-compiler-version-invalidate-an-install.cn.md) | 编译器的**版本**要不要进构建指纹？ |
| [`2026-11-04-should-the-library-grad-be-declared-per-arity`](2026-11-04-should-the-library-grad-be-declared-per-arity.cn.md) | 库层 `grad` 要不要按 arity 铺开一族声明，还是等一个"pack 的第一个成员"类型函数？ |
| [`2026-11-04-should-the-library-grow-a-has-aux-form`](2026-11-04-should-the-library-grow-a-has-aux-form.cn.md) | 库层要不要也有 `has_aux`，还是"配对在哪写就在哪拆"就够了？ |
| [`2026-11-04-should-the-library-with-grad-be-an-export-that-refuses`](2026-11-04-should-the-library-with-grad-be-an-export-that-refuses.cn.md) | 库层 `with_grad` 今天就该挂一个一律拒绝的可调用导出，还是等 `jvp` 的 `with` 切向落地再挂？ |
| [`2026-11-04-the-two-tuple-cases-have-no-closure-member`](2026-11-04-the-two-tuple-cases-have-no-closure-member.cn.md) | 那两条元组用例里没有闭包成员：19:55 那条裁决的前提，今天量不出来 |
| [`2026-11-04-two-f32-combination-cases-meet-their-own-call-text`](2026-11-04-two-f32-combination-cases-meet-their-own-call-text.cn.md) | 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字） |
| [`2026-11-04-what-is-a-value-of-extent-of`](2026-11-04-what-is-a-value-of-extent-of.cn.md) | `extent_of::<A>` 当一个值用：答，还是拒绝这个值？ |
| [`2026-11-04-what-shape-a-zero-with-no-stated-extent-has`](2026-11-04-what-shape-a-zero-with-no-stated-extent-has.cn.md) | 类型没写宽度时，那个零的宽度是多少？ |
| [`2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port`](2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port.cn.md) | 抬起的 `with` 值进了值列表：已按甲收窄，切不动的形状按名字拒 |
| [`2026-11-04-where-the-seal-says-the-exit-type-is-wrong`](2026-11-04-where-the-seal-says-the-exit-type-is-wrong.cn.md) | 出口类型对不上：已按 B 落到 `RuntimeDiag::ExitTypeMismatch`，请作者裁这个码与措辞 |
| [`2026-11-04-who-adopts-the-graphs-of-a-nested-cut`](2026-11-04-who-adopts-the-graphs-of-a-nested-cut.cn.md) | 两级嵌套的切：谁来带第二层的图 |
| [`2026-11-04-who-carries-the-cxx-runtime-into-the-install`](2026-11-04-who-carries-the-cxx-runtime-into-the-install.cn.md) | 安装自己带 C++ 运行时，还是继续要调用方补一条导出？ |
| [`2026-11-04-who-states-the-extents-a-product-law-reads`](2026-11-04-who-states-the-extents-a-product-law-reads.cn.md) | 乘积律读的那个形状，该由谁写出来？ |
| [`questions-2026-10-31-does-a-version-edge-order-two-writes`](questions-2026-10-31-does-a-version-edge-order-two-writes.md) | 版本边算不算"两次写同一资源的调用之间的排序" |
| [`questions-2026-10-31-what-executes-a-value-taken-by-a-position`](questions-2026-10-31-what-executes-a-value-taken-by-a-position.md) | 被位置取走的值，怎么拿到执行的理由（不靠 ctrl）？ |
| [`the-switch-that-reaches-no-target`](the-switch-that-reaches-no-target.md) | The switch that reaches no target |
