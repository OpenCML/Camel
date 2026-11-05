# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：13 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-is-a-chain-start-the-same-question-as-an-order-carrier`](2026-11-02-is-a-chain-start-the-same-question-as-an-order-carrier.cn.md) | `nodeTakesAnOrder` 与 `isChainStatement`：一个问题还是两个？ |
| [`2026-11-02-the-gc-soak-step-is-not-the-eq-rewrite`](2026-11-02-the-gc-soak-step-is-not-the-eq-rewrite.cn.md) | `gc_soak` 的那一跳不是 `!=` 改 `!(==)` 造成的 —— 作者 12:10 的机制假设被读数证伪，预算怎么办 |
| [`2026-11-02-who-carries-the-chain-place-after-a-branch`](2026-11-02-who-carries-the-chain-place-after-a-branch.cn.md) | 分支之后，链位由谁承接？ |
| [`2026-11-03-a-structural-nodes-chain-position-belongs-to-its-join`](2026-11-03-a-structural-nodes-chain-position-belongs-to-its-join.cn.md) | 结构节点的链位在它的 JOIN 之后，不在它自己身上 |
| [`2026-11-03-does-a-mark-encode-shape-or-history`](2026-11-03-does-a-mark-encode-shape-or-history.cn.md) | 一处标记编码的是"形状"还是"已发生过的事件"？ |
| [`2026-11-03-the-mean-axis-transpose-waits-for-two-upstreams`](2026-11-03-the-mean-axis-transpose-waits-for-two-upstreams.cn.md) | `mean_axis` 的转置：一条律改不动这道墙，它等的是两个上游 |
| [`2026-11-04-a-refusal-removed-is-a-defect-hidden`](2026-11-04-a-refusal-removed-is-a-defect-hidden.cn.md) | 一个拒绝被拿掉之后，它挡住的缺陷就只剩读数了 |
| [`2026-11-04-does-the-seal-check-judge-the-marker-or-the-statement`](2026-11-04-does-the-seal-check-judge-the-marker-or-the-statement.cn.md) | 同步边的封口判据：问标记，还是问"这个节点是不是语句" |
| [`2026-11-04-how-does-a-type-pack-take-apart`](2026-11-04-how-does-a-type-pack-take-apart.cn.md) | 类型层的包怎么拆：`match type` 的元组模式，还是声明处的拆包？ |
| [`2026-11-04-what-is-a-value-of-extent-of`](2026-11-04-what-is-a-value-of-extent-of.cn.md) | `extent_of::<A>` 当一个值用：答，还是拒绝这个值？ |
| [`2026-11-05-a-type-position-pattern-is-not-expressible-only-inside-match-type`](2026-11-05-a-type-position-pattern-is-not-expressible-only-inside-match-type.cn.md) | 包模式只在一个位置有读法：类型位置的 `match type` 的 case |
| [`2026-11-05-does-the-transpose-of-a-tuple-construction-hand-each-member-its-cotangent`](2026-11-05-does-the-transpose-of-a-tuple-construction-hand-each-member-its-cotangent.cn.md) | 构造元组（`FILL`）的转置，有没有把余切逐成员交给两个操作数 |
| [`2026-11-05-who-carries-the-value-that-mul-tensor-t-reads`](2026-11-05-who-carries-the-value-that-mul-tensor-t-reads.cn.md) | `mul_tensor_t` 一族律要读的操作数值，由谁交过来 |

## 生成器不会删的行：54 行（页已不在 `questions/` 里）

> 这些行是从上一版的表里原样带过来的：**生成器只增不删**，页走了行也留着。
> 页搬进了 `reviews/` 的行指到那一页；树里已经没有这一页的行只留名字，不编一个指不到的目标。
> 要删哪一行是作者的编辑，删掉之后生成器不会再把它写回来。

| 页 | 问什么 | 页在哪 |
| --- | --- | --- |
| [`2026-11-02-what-carries-the-residuals`](../reviews/2026-11-02-what-carries-the-residuals.cn.md) | 残差用什么承载？ | 在 `reviews/` |
| [`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float`](../reviews/2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md) | 反传倒着走要能钉住，先要循环状态不是浮点标量 | 在 `reviews/` |
| [`2026-11-04-a-case-with-no-plan-entry-and-a-main`](../reviews/2026-11-04-a-case-with-no-plan-entry-and-a-main.cn.md) | 一个用例带着 `func main` 却没有任何计划条目：删掉，还是给它写一条？ | 在 `reviews/` |
| [`2026-11-04-a-draft-node-asked-of-the-source-graph`](../reviews/2026-11-04-a-draft-node-asked-of-the-source-graph.cn.md) | 余切类型问的是哪张图：造图节点被当成源图节点来问，回退值该不该继续静默说 `float`？ | 在 `reviews/` |
| [`2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep`](../reviews/2026-11-04-a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep.cn.md) | `^` 的反向半读不了切向的值：那句守卫留不留，`(-3.0) ^ 2.0` 答 `-6.0` 还是 `NaN`？ | 在 `reviews/` |
| [`2026-11-04-a-member-read-is-not-a-parameter-read`](../reviews/2026-11-04-a-member-read-is-not-a-parameter-read.cn.md) | 读一个参数的成员，不是读这个参数：`transpose` 该跟到端口吗 | 在 `reviews/` |
| [`2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction`](../reviews/2026-11-04-a-parameter-holds-no-cotangent-on-a-two-operand-reduction.cn.md) | 为什么 `sqrt`/`log`/`divide` 的两腿用例停在“一个参数拿不到余切，因为没有东西用它” | 在 `reviews/` |
| [`2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer`](../reviews/2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md) | 秩 0 的余切被当成指针读：`mean_axis_stated_t` 的端口写着 `float[..]` | 在 `reviews/` |
| [`2026-11-04-a-struct-field-read-refused-or-copied-by-key`](../reviews/2026-11-04-a-struct-field-read-refused-or-copied-by-key.cn.md) | 一个 struct 字段读（`qq.a`）：切里按名字拒，还是把 key 抄过去 | 在 `reviews/` |
| [`2026-11-04-axis-reductions-state-their-extents`](../reviews/2026-11-04-axis-reductions-state-their-extents.cn.md) | 轴归约的结果类型：把声明退掉交给推导，还是留着声明另开一条路？ | 在 `reviews/` |
| [`2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out`](../reviews/2026-11-04-can-a-pass-build-the-graph-a-transform-hands-out.cn.md) | `autodiff` 能不能在改写时自己把 `split` 造出来的那个值造出来？ | 在 `reviews/` |
| [`2026-11-04-does-a-build-gated-case-count-as-a-name-pin`](../reviews/2026-11-04-does-a-build-gated-case-count-as-a-name-pin.cn.md) | 只在 no-JIT / arm64 构建里才跑得到的用例，算不算把这条码按名字钉住了？ | 在 `reviews/` |
| [`2026-11-04-does-a-call-stand-on-its-callees-lin-or-on-its-body`](../reviews/2026-11-04-does-a-call-stand-on-its-callees-lin-or-on-its-body.cn.md) | 一条**静态具名**的调用，转置时站哪儿：被调函数的 body，还是它的 `lin`？ | 在 `reviews/` |
| [`2026-11-04-does-a-closure-members-cotangent-have-a-slot`](../reviews/2026-11-04-does-a-closure-members-cotangent-have-a-slot.cn.md) | 闭包成员的余切应不应该有一个槽？ | 在 `reviews/` |
| [`2026-11-04-does-a-product-transpose-still-need-a-zero-carrier`](../reviews/2026-11-04-does-a-product-transpose-still-need-a-zero-carrier.cn.md) | 乘积的转置还要不要一个零载体？ | 在 `reviews/` |
| [`2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it`](../reviews/2026-11-04-does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it.cn.md) | 一条转置律没被一阶走查读到，算不算"律已经补上"？ | 在 `reviews/` |
| [`2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value`](../reviews/2026-11-04-does-a-transposed-member-get-a-port-for-a-parameter-value.cn.md) | 转置成员要不要给"一个参数自己的值"留一个端口？ | 在 `reviews/` |
| [`2026-11-04-does-a-with-value-get-a-tangent`](../reviews/2026-11-04-does-a-with-value-get-a-tangent.cn.md) | `with` 值该不该有自己的切向？ | 在 `reviews/` |
| [`2026-11-04-does-adopt-carry-what-it-adopted-or-does-a-cut-carry-its-own-chain`](../reviews/2026-11-04-does-adopt-carry-what-it-adopted-or-does-a-cut-carry-its-own-chain.cn.md) | 嵌套两级的 cut 丢掉自己的孙程序：把 `adopt` 改成传递，还是让每个 cut 自带一条链？ | 在 `reviews/` |
| [`2026-11-04-does-pick-at-need-a-forward-rule`](../reviews/2026-11-04-does-pick-at-need-a-forward-rule.cn.md) | 问题：`pick_at` 该补一条**前向**律吗？ | 在 `reviews/` |
| [`2026-11-04-does-the-engine-need-a-pick-at-rule`](../reviews/2026-11-04-does-the-engine-need-a-pick-at-rule.cn.md) | 问题：引擎的反向表要不要给 `pick_at` 补一条规则？ | 在 `reviews/` |
| [`2026-11-04-does-the-library-need-forward-rules-for-the-stated-spellings`](../reviews/2026-11-04-does-the-library-need-forward-rules-for-the-stated-spellings.cn.md) | 问题：`rulesets.jvp` 要不要给四个"stated"拼写补前向律？ | 在 `reviews/` |
| [`2026-11-04-does-the-transpose-walk-still-refuse-by-arity`](../reviews/2026-11-04-does-the-transpose-walk-still-refuse-by-arity.cn.md) | 问题：转置走图还按操作数个数拒一个算子吗？ | 在 `reviews/` |
| [`2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape`](../reviews/2026-11-04-how-does-a-tie-mask-travel-with-an-extremums-shape.cn.md) | `max_axis` / `min_axis` 的孪生算子：并列极值的选择怎么随形状走？ | 在 `reviews/` |
| [`2026-11-04-how-is-a-two-port-transposed-value-called`](../reviews/2026-11-04-how-is-a-two-port-transposed-value-called.cn.md) | 两端口（值 + 余切）的转置值，怎么被调用？ | 在 `reviews/` |
| [`2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal`](../reviews/2026-11-04-is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal.cn.md) | 开秩数组的零，是空数组还是明确拒绝 | 在 `reviews/` |
| [`2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape`](../reviews/2026-11-04-should-a-multiplys-adjoint-need-its-operands-shape.cn.md) | 乘法的伴随要不要读操作数自己的形状？ | 在 `reviews/` |
| [`2026-11-04-should-an-owner-name-be-checked-against-a-roster`](../reviews/2026-11-04-should-an-owner-name-be-checked-against-a-roster.cn.md) | 负责人这个名字，要不要对一张名册？ | 在 `reviews/` |
| [`2026-11-04-should-the-compiler-version-invalidate-an-install`](../reviews/2026-11-04-should-the-compiler-version-invalidate-an-install.cn.md) | 编译器的**版本**要不要进构建指纹？ | 在 `reviews/` |
| [`2026-11-04-should-the-library-grad-be-declared-per-arity`](../reviews/2026-11-04-should-the-library-grad-be-declared-per-arity.cn.md) | 库层 `grad` 要不要按 arity 铺开一族声明，还是等一个"pack 的第一个成员"类型函数？ | 在 `reviews/` |
| [`2026-11-04-should-the-library-grow-a-has-aux-form`](../reviews/2026-11-04-should-the-library-grow-a-has-aux-form.cn.md) | 库层要不要也有 `has_aux`，还是"配对在哪写就在哪拆"就够了？ | 在 `reviews/` |
| [`2026-11-04-should-the-library-with-grad-be-an-export-that-refuses`](../reviews/2026-11-04-should-the-library-with-grad-be-an-export-that-refuses.cn.md) | 库层 `with_grad` 今天就该挂一个一律拒绝的可调用导出，还是等 `jvp` 的 `with` 切向落地再挂？ | 在 `reviews/` |
| [`2026-11-04-the-two-tuple-cases-have-no-closure-member`](../reviews/2026-11-04-the-two-tuple-cases-have-no-closure-member.cn.md) | 那两条元组用例里没有闭包成员：19:55 那条裁决的前提，今天量不出来 | 在 `reviews/` |
| [`2026-11-04-two-f32-combination-cases-meet-their-own-call-text`](../reviews/2026-11-04-two-f32-combination-cases-meet-their-own-call-text.cn.md) | 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字） | 在 `reviews/` |
| [`2026-11-04-what-answers-a-length-from-a-type-alone`](../reviews/2026-11-04-what-answers-a-length-from-a-type-alone.cn.md) | `extent_of` 到底答「形状」还是答「某一维的长度」？ | 在 `reviews/` |
| [`2026-11-04-what-does-a-call-of-a-value-that-is-not-a-function-say`](../reviews/2026-11-04-what-does-a-call-of-a-value-that-is-not-a-function-say.cn.md) | 调用一个类型不是函数的值，诊断该怎么写？ | 在 `reviews/` |
| [`2026-11-04-what-does-the-second-derivative-need`](../reviews/2026-11-04-what-does-the-second-derivative-need.cn.md) | 二阶自动微分卡在哪一层：一个"图叫得出名字"的函数值？ | 在 `reviews/` |
| [`2026-11-04-what-shape-a-zero-with-no-stated-extent-has`](../reviews/2026-11-04-what-shape-a-zero-with-no-stated-extent-has.cn.md) | 类型没写宽度时，那个零的宽度是多少？ | 在 `reviews/` |
| [`2026-11-04-when-a-lookup-that-finds-nothing-should-refuse`](../reviews/2026-11-04-when-a-lookup-that-finds-nothing-should-refuse.cn.md) | 查不到的时候，什么时候该拒绝、什么时候该答默认？ | 在 `reviews/` |
| [`2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port`](../reviews/2026-11-04-when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port.cn.md) | 抬起的 `with` 值进了值列表：已按甲收窄，切不动的形状按名字拒 | 在 `reviews/` |
| [`2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed`](../reviews/2026-11-04-where-a-laws-port-disagrees-with-the-value-it-is-handed.cn.md) | 律的端口和它收到的那个值不符时，该在哪一层拒绝？ | 在 `reviews/` |
| [`2026-11-04-where-the-seal-says-the-exit-type-is-wrong`](../reviews/2026-11-04-where-the-seal-says-the-exit-type-is-wrong.cn.md) | 出口类型对不上：已按 B 落到 `RuntimeDiag::ExitTypeMismatch`，请作者裁这个码与措辞 | 在 `reviews/` |
| [`2026-11-04-which-shape-reads-can-become-extent-of`](../reviews/2026-11-04-which-shape-reads-can-become-extent-of.cn.md) | 律里那几处取形状的地方，哪些能换成 `extent_of`？ | 在 `reviews/` |
| [`2026-11-04-who-adopts-the-graphs-of-a-nested-cut`](../reviews/2026-11-04-who-adopts-the-graphs-of-a-nested-cut.cn.md) | 两级嵌套的切：谁来带第二层的图 | 在 `reviews/` |
| [`2026-11-04-who-carries-the-cxx-runtime-into-the-install`](../reviews/2026-11-04-who-carries-the-cxx-runtime-into-the-install.cn.md) | 安装自己带 C++ 运行时，还是继续要调用方补一条导出？ | 在 `reviews/` |
| [`2026-11-04-who-gives-a-shapeless-cotangent-its-rank`](../reviews/2026-11-04-who-gives-a-shapeless-cotangent-its-rank.cn.md) | 余切没有形状时，谁给它那个秩：`numel(ct)` 与 `mean_axis_stated_t` 的端口 | 在 `reviews/` |
| [`2026-11-04-who-reads-the-first-operand-as-a-callee`](../reviews/2026-11-04-who-reads-the-first-operand-as-a-callee.cn.md) | 被调值站在“第一个操作数”位置：该删掉那条读法，还是教调用转置把它写对？ | 在 `reviews/` |
| [`2026-11-04-who-states-the-extents-a-product-law-reads`](../reviews/2026-11-04-who-states-the-extents-a-product-law-reads.cn.md) | 乘积律读的那个形状，该由谁写出来？ | 在 `reviews/` |
| [`2026-11-04-who-states-the-extents-of-a-stated-reduction`](../reviews/2026-11-04-who-states-the-extents-of-a-stated-reduction.cn.md) | 一个 stated 归约的 extents，由谁在编译期说出来 | 在 `reviews/` |
| [`2026-11-04-who-states-the-length-when-the-shape-is-open`](../reviews/2026-11-04-who-states-the-length-when-the-shape-is-open.cn.md) | 形状开放时，长度由谁陈述？ | 在 `reviews/` |
| [`2026-11-04-widening-the-float-guard-turns-ten-hardcoded-types-into-defects`](../reviews/2026-11-04-widening-the-float-guard-turns-ten-hardcoded-types-into-defects.cn.md) | 放宽 `Float64` 这道闸门时，十余处写死的类型会在同一刻变成缺陷 | 在 `reviews/` |
| [`questions-2026-10-31-does-a-version-edge-order-two-writes`](../reviews/questions-2026-10-31-does-a-version-edge-order-two-writes.md) | 版本边算不算"两次写同一资源的调用之间的排序" | 在 `reviews/` |
| [`questions-2026-10-31-what-executes-a-value-taken-by-a-position`](../reviews/questions-2026-10-31-what-executes-a-value-taken-by-a-position.md) | 被位置取走的值，怎么拿到执行的理由（不靠 ctrl）？ | 在 `reviews/` |
| [`the-switch-that-reaches-no-target`](../reviews/the-switch-that-reaches-no-target.md) | The switch that reaches no target | 在 `reviews/` |
