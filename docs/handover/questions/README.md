# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：53 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-04-a-case-with-no-plan-entry-and-a-main`](2026-11-04-a-case-with-no-plan-entry-and-a-main.cn.md) | 一个用例带着 `func main` 却没有任何计划条目：删掉，还是给它写一条？ |
| [`2026-11-04-a-draft-node-asked-of-the-source-graph`](2026-11-04-a-draft-node-asked-of-the-source-graph.cn.md) | 余切类型问的是哪张图：造图节点被当成源图节点来问，回退值该不该继续静默说 `float`？ |
| [`2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep`](2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep.cn.md) | `^` 的反向半读不了切向的值：那句守卫留不留，`(-3.0) ^ 2.0` 答 `-6.0` 还是 `NaN`？ |
| [`2026-11-04-a-member-read-is-not-a-parameter-read`](2026-11-04-a-member-read-is-not-a-parameter-read.cn.md) | 读一个参数的成员，不是读这个参数：`transpose` 该跟到端口吗 |
| [`2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction`](2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction.cn.md) | 为什么 `sqrt`/`log`/`divide` 的两腿用例停在“一个参数拿不到余切，因为没有东西用它” |
| [`2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer`](2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md) | 秩 0 的余切被当成指针读：`mean_axis_stated_t` 的端口写着 `float[..]` |
| [`2026-11-04-a-struct-field-read-refused-or-copied-by-key`](2026-11-04-a-struct-field-read-refused-or-copied-by-key.cn.md) | 一个 struct 字段读（`qq.a`）：切里按名字拒，还是把 key 抄过去 |
| [`2026-11-04-a-sum-to-that-takes-the-length-itself`](2026-11-04-a-sum-to-that-takes-the-length-itself.cn.md) | 由长度直接收长度的 `sum_to`：`sum_to_at(t, at)` |
| [`2026-11-04-axis-reductions-state-their-extents`](2026-11-04-axis-reductions-state-their-extents.cn.md) | 轴归约的结果类型：把声明退掉交给推导，还是留着声明另开一条路？ |
| [`2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out`](2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out.cn.md) | `autodiff` 能不能在改写时自己把 `split` 造出来的那个值造出来？ |
| [`2026-11-04-does-a-build-gated-case-count-as-a-name-pin`](2026-11-04-does-a-build-gated-case-count-as-a-name-pin.cn.md) | 只在 no-JIT / arm64 构建里才跑得到的用例，算不算把这条码按名字钉住了？ |
| [`2026-11-04-does-a-call-stand-on-its-callees-lin-or-on-its-body`](2026-11-04-does-a-call-stand-on-its-callees-lin-or-on-its-body.cn.md) | 一条**静态具名**的调用，转置时站哪儿：被调函数的 body，还是它的 `lin`？ |
| [`2026-11-04-does-a-closure-members-cotangent-have-a-slot`](2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md) | 闭包成员的余切应不应该有一个槽？ |
| [`2026-11-04-does-a-product-transpose-still-need-a-zero-carrier`](2026-11-04-does-a-product-transpose-still-need-a-zero-carrier.cn.md) | 乘积的转置还要不要一个零载体？ |
| [`2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it`](2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it.cn.md) | 一条转置律没被一阶走查读到，算不算"律已经补上"？ |
| [`2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value`](2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md) | 转置成员要不要给"一个参数自己的值"留一个端口？ |
| [`2026-11-04-does-a-with-value-get-a-tangent`](2026-11-04-does-a-with-value-get-a-tangent.cn.md) | `with` 值该不该有自己的切向？ |
| [`2026-11-04-does-adopt-carry-what-it-adopted-or-does-a-cut-carry-its-own-chain`](2026-11-04-does-adopt-carry-what-it-adopted-or-does-a-cut-carry-its-own-chain.cn.md) | 嵌套两级的 cut 丢掉自己的孙程序：把 `adopt` 改成传递，还是让每个 cut 自带一条链？ |
| [`2026-11-04-does-pick-at-need-a-forward-rule`](2026-11-04-does-pick-at-need-a-forward-rule.cn.md) | 问题：`pick_at` 该补一条**前向**律吗？ |
| [`2026-11-04-does-the-engine-need-a-pick-at-rule`](2026-11-04-does-the-engine-need-a-pick-at-rule.cn.md) | 问题：引擎的反向表要不要给 `pick_at` 补一条规则？ |
| [`2026-11-04-does-the-library-need-forward-rules-for-the-stated-spellings`](2026-11-04-does-the-library-need-forward-rules-for-the-stated-spellings.cn.md) | 问题：`rulesets.jvp` 要不要给四个"stated"拼写补前向律？ |
| [`2026-11-04-does-the-transpose-walk-still-refuse-by-arity`](2026-11-04-does-the-transpose-walk-still-refuse-by-arity.cn.md) | 问题：转置走图还按操作数个数拒一个算子吗？ |
| [`2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape`](2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape.cn.md) | `max_axis` / `min_axis` 的孪生算子：并列极值的选择怎么随形状走？ |
| [`2026-11-04-how-is-a-two-port-transposed-value-called`](2026-11-04-how-is-a-two-port-transposed-value-called.cn.md) | 两端口（值 + 余切）的转置值，怎么被调用？ |
| [`2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal`](2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal.cn.md) | 开秩数组的零，是空数组还是明确拒绝 |
| [`2026-11-04-narrow-the-declaration-or-teach-the-consumer`](2026-11-04-narrow-the-declaration-or-teach-the-consumer.cn.md) | 声明的类型与运行时的装箱不一致时，改窄声明还是教会消费者：`mean_axis_stated_t` 与六个轴归约的两个实例 |
| [`2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape`](2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape.cn.md) | 乘法的伴随要不要读操作数自己的形状？ |
| [`2026-11-04-should-an-owner-name-be-checked-against-a-roster`](2026-11-04-should-an-owner-name-be-checked-against-a-roster.cn.md) | 负责人这个名字，要不要对一张名册？ |
| [`2026-11-04-should-the-compiler-version-invalidate-an-install`](2026-11-04-should-the-compiler-version-invalidate-an-install.cn.md) | 编译器的**版本**要不要进构建指纹？ |
| [`2026-11-04-should-the-library-grad-be-declared-per-arity`](2026-11-04-should-the-library-grad-be-declared-per-arity.cn.md) | 库层 `grad` 要不要按 arity 铺开一族声明，还是等一个"pack 的第一个成员"类型函数？ |
| [`2026-11-04-should-the-library-grow-a-has-aux-form`](2026-11-04-should-the-library-grow-a-has-aux-form.cn.md) | 库层要不要也有 `has_aux`，还是"配对在哪写就在哪拆"就够了？ |
| [`2026-11-04-should-the-library-with-grad-be-an-export-that-refuses`](2026-11-04-should-the-library-with-grad-be-an-export-that-refuses.cn.md) | 库层 `with_grad` 今天就该挂一个一律拒绝的可调用导出，还是等 `jvp` 的 `with` 切向落地再挂？ |
| [`2026-11-04-the-two-tuple-cases-have-no-closure-member`](2026-11-04-the-two-tuple-cases-have-no-closure-member.cn.md) | 那两条元组用例里没有闭包成员：19:55 那条裁决的前提，今天量不出来 |
| [`2026-11-04-two-f32-combination-cases-meet-their-own-call-text`](2026-11-04-two-f32-combination-cases-meet-their-own-call-text.cn.md) | 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字） |
| [`2026-11-04-what-answers-a-length-from-a-type-alone`](2026-11-04-what-answers-a-length-from-a-type-alone.cn.md) | `extent_of` 到底答「形状」还是答「某一维的长度」？ |
| [`2026-11-04-what-does-a-call-of-a-value-that-is-not-a-function-say`](2026-11-04-what-does-a-call-of-a-value-that-is-not-a-function-say.cn.md) | 调用一个类型不是函数的值，诊断该怎么写？ |
| [`2026-11-04-what-does-the-second-derivative-need`](2026-11-04-what-does-the-second-derivative-need.cn.md) | 二阶自动微分卡在哪一层：一个"图叫得出名字"的函数值？ |
| [`2026-11-04-what-shape-a-zero-with-no-stated-extent-has`](2026-11-04-what-shape-a-zero-with-no-stated-extent-has.cn.md) | 类型没写宽度时，那个零的宽度是多少？ |
| [`2026-11-04-when-a-lookup-that-finds-nothing-should-refuse`](2026-11-04-when-a-lookup-that-finds-nothing-should-refuse.cn.md) | 查不到的时候，什么时候该拒绝、什么时候该答默认？ |
| [`2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port`](2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port.cn.md) | 抬起的 `with` 值进了值列表：已按甲收窄，切不动的形状按名字拒 |
| [`2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed`](2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed.cn.md) | 律的端口和它收到的那个值不符时，该在哪一层拒绝？ |
| [`2026-11-04-where-the-seal-says-the-exit-type-is-wrong`](2026-11-04-where-the-seal-says-the-exit-type-is-wrong.cn.md) | 出口类型对不上：已按 B 落到 `RuntimeDiag::ExitTypeMismatch`，请作者裁这个码与措辞 |
| [`2026-11-04-which-shape-reads-can-become-extent-of`](2026-11-04-which-shape-reads-can-become-extent-of.cn.md) | 律里那几处取形状的地方，哪些能换成 `extent_of`？ |
| [`2026-11-04-who-adopts-the-graphs-of-a-nested-cut`](2026-11-04-who-adopts-the-graphs-of-a-nested-cut.cn.md) | 两级嵌套的切：谁来带第二层的图 |
| [`2026-11-04-who-carries-the-cxx-runtime-into-the-install`](2026-11-04-who-carries-the-cxx-runtime-into-the-install.cn.md) | 安装自己带 C++ 运行时，还是继续要调用方补一条导出？ |
| [`2026-11-04-who-carries-the-shape-as-an-operand`](2026-11-04-who-carries-the-shape-as-an-operand.cn.md) | 形状要当操作数交下来：`mul_stated` 与 `matmul_stated` |
| [`2026-11-04-who-gives-a-shapeless-cotangent-its-rank`](2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md) | 余切没有形状时，谁给它那个秩：`numel(ct)` 与 `mean_axis_stated_t` 的端口 |
| [`2026-11-04-who-reads-the-first-operand-as-a-callee`](2026-11-04-who-reads-the-first-operand-as-a-callee.cn.md) | 被调值站在“第一个操作数”位置：该删掉那条读法，还是教调用转置把它写对？ |
| [`2026-11-04-who-records-which-closure-member-is-which-parameter`](2026-11-04-who-records-which-closure-member-is-which-parameter.cn.md) | 闭包成员与参数的对应，由谁记录、经哪条通道 |
| [`2026-11-04-who-states-the-extents-a-product-law-reads`](2026-11-04-who-states-the-extents-a-product-law-reads.cn.md) | 乘积律读的那个形状，该由谁写出来？ |
| [`2026-11-04-who-states-the-extents-of-a-stated-reduction`](2026-11-04-who-states-the-extents-of-a-stated-reduction.cn.md) | 一个 stated 归约的 extents，由谁在编译期说出来 |
| [`2026-11-04-who-states-the-length-when-the-shape-is-open`](2026-11-04-who-states-the-length-when-the-shape-is-open.cn.md) | 形状开放时，长度由谁陈述？ |
| [`2026-11-04-widening-the-float-guard-turns-ten-hardcoded-types-into-defects`](2026-11-04-widening-the-float-guard-turns-ten-hardcoded-types-into-defects.cn.md) | 放宽 `Float64` 这道闸门时，十余处写死的类型会在同一刻变成缺陷 |
