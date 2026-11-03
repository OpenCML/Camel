# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：21 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-04-a-draft-node-asked-of-the-source-graph`](2026-11-04-a-draft-node-asked-of-the-source-graph.cn.md) | 余切类型问的是哪张图：造图节点被当成源图节点来问，回退值该不该继续静默说 `float`？ |
| [`2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep`](2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep.cn.md) | `^` 的反向半读不了切向的值：那句守卫留不留，`(-3.0) ^ 2.0` 答 `-6.0` 还是 `NaN`？ |
| [`2026-11-04-a-member-read-is-not-a-parameter-read`](2026-11-04-a-member-read-is-not-a-parameter-read.cn.md) | 读一个参数的成员，不是读这个参数：`transpose` 该跟到端口吗 |
| [`2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction`](2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction.cn.md) | 为什么 `sqrt`/`log`/`divide` 的两腿用例停在“一个参数拿不到余切，因为没有东西用它” |
| [`2026-11-04-a-struct-field-read-refused-or-copied-by-key`](2026-11-04-a-struct-field-read-refused-or-copied-by-key.cn.md) | 一个 struct 字段读（`qq.a`）：切里按名字拒，还是把 key 抄过去 |
| [`2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out`](2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out.cn.md) | `autodiff` 能不能在改写时自己把 `split` 造出来的那个值造出来？ |
| [`2026-11-04-does-a-closure-members-cotangent-have-a-slot`](2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md) | 闭包成员的余切应不应该有一个槽？ |
| [`2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it`](2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it.cn.md) | 一条转置律没被一阶走查读到，算不算"律已经补上"？ |
| [`2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value`](2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md) | 转置成员要不要给"一个参数自己的值"留一个端口？ |
| [`2026-11-04-does-a-with-value-get-a-tangent`](2026-11-04-does-a-with-value-get-a-tangent.cn.md) | `with` 值该不该有自己的切向？ |
| [`2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape`](2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape.cn.md) | 乘法的伴随要不要读操作数自己的形状？ |
| [`2026-11-04-should-the-library-grad-be-declared-per-arity`](2026-11-04-should-the-library-grad-be-declared-per-arity.cn.md) | 库层 `grad` 要不要按 arity 铺开一族声明，还是等一个"pack 的第一个成员"类型函数？ |
| [`2026-11-04-should-the-library-grow-a-has-aux-form`](2026-11-04-should-the-library-grow-a-has-aux-form.cn.md) | 库层要不要也有 `has_aux`，还是"配对在哪写就在哪拆"就够了？ |
| [`2026-11-04-should-the-library-with-grad-be-an-export-that-refuses`](2026-11-04-should-the-library-with-grad-be-an-export-that-refuses.cn.md) | 库层 `with_grad` 今天就该挂一个一律拒绝的可调用导出，还是等 `jvp` 的 `with` 切向落地再挂？ |
| [`2026-11-04-the-two-tuple-cases-have-no-closure-member`](2026-11-04-the-two-tuple-cases-have-no-closure-member.cn.md) | 那两条元组用例里没有闭包成员：19:55 那条裁决的前提，今天量不出来 |
| [`2026-11-04-two-f32-combination-cases-meet-their-own-call-text`](2026-11-04-two-f32-combination-cases-meet-their-own-call-text.cn.md) | 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字） |
| [`2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port`](2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port.cn.md) | 抬起的 `with` 值进了值列表：已按甲收窄，切不动的形状按名字拒 |
| [`2026-11-04-where-the-seal-says-the-exit-type-is-wrong`](2026-11-04-where-the-seal-says-the-exit-type-is-wrong.cn.md) | 出口类型对不上：已按 B 落到 `RuntimeDiag::ExitTypeMismatch`，请作者裁这个码与措辞 |
| [`2026-11-04-who-adopts-the-graphs-of-a-nested-cut`](2026-11-04-who-adopts-the-graphs-of-a-nested-cut.cn.md) | 两级嵌套的切：谁来带第二层的图 |
| [`2026-11-04-who-carries-the-cxx-runtime-into-the-install`](2026-11-04-who-carries-the-cxx-runtime-into-the-install.cn.md) | 安装自己带 C++ 运行时，还是继续要调用方补一条导出？ |
| [`2026-11-04-who-states-the-extents-a-product-law-reads`](2026-11-04-who-states-the-extents-a-product-law-reads.cn.md) | 乘积律读的那个形状，该由谁写出来？ |
