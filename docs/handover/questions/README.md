# 待裁索引

> 一页一问。作者把读过的页整体挪进 `reviews/`；这里只放**还在等回答**的页。
>
> 这一页由 `node scripts/check-doc-links.mjs --questions-index --write` 从目录生成，不要手改：
> 规矩在 `docs/handover/reviews/` 的裁定里，落没落地看代码和提交，不看这张表。

## 现在在等作者：47 页

| 页 | 问什么 |
| --- | --- |
| [`2026-11-02-does-reversing-a-backward-need-a-new-operator`](2026-11-02-does-reversing-a-backward-need-a-new-operator.cn.md) | 让反向半程按轮倒序，需要新增一个数组反转算子 `:struct/reverse_arr` 吗？ |
| [`2026-11-02-which-missing-operator-carries-a-reversed-backward`](2026-11-02-which-missing-operator-carries-a-reversed-backward.cn.md) | 倒序的 backward 该由哪一条来承载：新增 `:struct/reverse_arr`，还是让 draft 能造动态下标读？ |
| [`2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong`](2026-11-03-a-captured-scalar-inside-a-loop-body-is-silently-wrong.cn.md) | 一个被循环体捕获的标量，抬升后静默算错 |
| [`2026-11-03-a-mean-tangent-written-as-one-scale-moves-the-wall-to-split`](2026-11-03-a-mean-tangent-written-as-one-scale-moves-the-wall-to-split.cn.md) | 把 `mean_axis` 的切线写成一次缩放：墙换了一处，没有消失 |
| [`2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today`](2026-11-03-a-norm-port-count-over-the-call-arguments-is-not-a-wall-today.cn.md) | 转置成员的 norm 端口数多于调用点实参数：今天不是墙，是被更早一道拒绝挡住的潜在墙 |
| [`2026-11-03-a-returned-literal-is-not-built-into-the-declared-shape`](2026-11-03-a-returned-literal-is-not-built-into-the-declared-shape.cn.md) | 返回位置不肯把字面量建成声明说出的形状 |
| [`2026-11-03-a-second-rule-under-one-key-is-judged-by-its-when-text`](2026-11-03-a-second-rule-under-one-key-is-judged-by-its-when-text.cn.md) | 同一个键下挂两条规则，今天按 `when` 文本判重，与裁定「按操作数类型」不一致 |
| [`2026-11-03-a-sized-rank-one-cast-refuses-a-literal-the-declaration-accepts`](2026-11-03-a-sized-rank-one-cast-refuses-a-literal-the-declaration-accepts.cn.md) | 一个定长的秩一 `as`，拒了声明和传参都接受的同一个字面量 |
| [`2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant`](2026-11-03-a-timeout-should-be-derived-from-a-measurement-not-a-constant.cn.md) | 一个超时应该由读数推出来，而不是由人写死 |
| [`2026-11-03-can-gradient-of-a-function-be-a-library-type-function`](2026-11-03-can-gradient-of-a-function-be-a-library-type-function.cn.md) | `grad(f)` 的类型能不能写成库层类型函数？（删引擎 D2） |
| [`2026-11-03-does-a-comment-in-a-source-file-count-as-a-direct-connection`](2026-11-03-does-a-comment-in-a-source-file-count-as-a-direct-connection.cn.md) | 源文件里的一句注释，算不算一次"直连"？ |
| [`2026-11-03-does-a-generic-call-accept-a-rank-zero-argument`](2026-11-03-does-a-generic-call-accept-a-rank-zero-argument.cn.md) | 泛型函数调用的实参匹配，要不要照 5.4 收秩 0？ |
| [`2026-11-03-does-a-loop-statement-with-an-unused-value-still-run`](2026-11-03-does-a-loop-statement-with-an-unused-value-still-run.cn.md) | 一个值没人用的 `loop` 语句，还算不算一条要执行的语句？ |
| [`2026-11-03-does-a-one-member-closure-prove-the-slot-identity`](2026-11-03-does-a-one-member-closure-prove-the-slot-identity.cn.md) | 闭包只有一个成员时，「槽的类型等于参数类型」能证明它是这个参数的值吗？ |
| [`2026-11-03-does-an-open-rank-index-law-cover-rank-one`](2026-11-03-does-an-open-rank-index-law-cover-rank-one.cn.md) | 下标的律：秩一与开秩要的答案类型不同，一条写在 `T[..]` 上的律覆盖不了两者 |
| [`2026-11-03-does-note-zero-cover-a-closure-capture-port`](2026-11-03-does-note-zero-cover-a-closure-capture-port.cn.md) | `noteZero` 要不要覆盖闭包捕获端口？ |
| [`2026-11-03-how-does-a-transform-hand-a-with-value-through-a-generic-body`](2026-11-03-how-does-a-transform-hand-a-with-value-through-a-generic-body.cn.md) | 变换在泛型库体里怎么把 `with` 值传下去：lambda 采纳了位置的 `with` 列表，体里却没有这些值的名字 |
| [`2026-11-03-is-a-name-at-a-void-tail-a-value-use`](2026-11-03-is-a-name-at-a-void-tail-a-value-use.cn.md) | 一个 `void` 函数体尾部的名字，是"被丢掉的值"还是"被当成值使用"？ |
| [`2026-11-03-is-a-var-read-outside-the-pure-value-rule`](2026-11-03-is-a-var-read-outside-the-pure-value-rule.cn.md) | 链位到底是谁在承担语义：`var` 读，还是纯算术调用？ |
| [`2026-11-03-is-the-loop-halfs-missing-value-at-the-call-site`](2026-11-03-is-the-loop-halfs-missing-value-at-the-call-site.cn.md) | loop 那一半的转置要的值，在调用点存在吗？在——但那是一个循环的值 |
| [`2026-11-03-is-the-unused-sync-block-warning-reachable-at-all`](2026-11-03-is-the-unused-sync-block-warning-reachable-at-all.cn.md) | 「这个 sync 块没人用」这句警告，今天还响得了吗？ |
| [`2026-11-03-may-a-region-owner-read-its-own-exit`](2026-11-03-may-a-region-owner-read-its-own-exit.cn.md) | 一个区域的拥有者能不能读自己区域的端口？ |
| [`2026-11-03-must-a-pullback-of-a-with-function-take-the-with-list`](2026-11-03-must-a-pullback-of-a-with-function-take-the-with-list.cn.md) | 带 `with` 参数的函数的 pullback，要不要把 `with` 列表写在类型上？ |
| [`2026-11-03-must-the-same-shape-refusal-carry-the-same-name-in-jvp-and-transpose`](2026-11-03-must-the-same-shape-refusal-carry-the-same-name-in-jvp-and-transpose.cn.md) | 同一个形状拒绝，在 `jvp` 和 `transpose` 里该不该有同一个名字？ |
| [`2026-11-03-one-rule-key-cannot-hold-an-array-and-a-number`](2026-11-03-one-rule-key-cannot-hold-an-array-and-a-number.cn.md) | 一个规则键只能挂一条规则，于是同一个算子的两种形状无法共存 |
| [`2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float`](2026-11-03-reversing-a-backward-can-only-be-pinned-when-a-loop-state-is-not-a-float.cn.md) | 反传倒着走要能钉住，先要循环状态不是浮点标量 |
| [`2026-11-03-should-a-value-read-twice-be-one-residual`](2026-11-03-should-a-value-read-twice-be-one-residual.cn.md) | 同一个值被读两次，是一个残余还是两个？ |
| [`2026-11-03-should-one-unused-parameter-warn-once-per-rule`](2026-11-03-should-one-unused-parameter-warn-once-per-rule.cn.md) | 一个未使用的类型参数，该在律上报一次还是每侧面一次？ |
| [`2026-11-03-should-vjp-carry-the-with-list-as-jvp-does`](2026-11-03-should-vjp-carry-the-with-list-as-jvp-does.cn.md) | `Vjp<F>` 该不该和 `Jvp<F>` 同形地带 `WithOf<F>`？ |
| [`2026-11-03-the-capture-does-not-reach-the-body-is-not-the-whole-cause`](2026-11-03-the-capture-does-not-reach-the-body-is-not-the-whole-cause.cn.md) | 捕获"没进循环输入"不是全部原因：两条通道都实现了，错数一个也没动 |
| [`2026-11-03-the-criterion-case-cannot-tell-a-right-law-from-a-wrong-one`](2026-11-03-the-criterion-case-cannot-tell-a-right-law-from-a-wrong-one.cn.md) | 一条分辨不了对错的判据，能不能算判据？——`mean_axis_transpose` 的 6.0 既对也错 |
| [`2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow`](2026-11-03-the-lifted-list-is-grouped-and-the-ports-must-follow.cn.md) | 抬升列表已定为 grouped、空切向占位；剩下的是建图里的端口次序 |
| [`2026-11-03-the-remote-helper-loses-a-plans-stderr`](2026-11-03-the-remote-helper-loses-a-plans-stderr.cn.md) | 远程 helper 把 plan 的 stderr 整段丢掉了：这条修只活在本机，够不够？ |
| [`2026-11-03-the-self-test-is-red-on-develop-and-has-no-name`](2026-11-03-the-self-test-is-red-on-develop-and-has-no-name.cn.md) | 自检在 develop 上是红的：`runner.backend_matrix` 里 7 个用例失败，这一红该由谁摘？ |
| [`2026-11-03-the-silent-wrong-number-does-not-reproduce`](2026-11-03-the-silent-wrong-number-does-not-reproduce.cn.md) | 循环体里读外部标量的「静默错数」今天不可复现：它还算主线第一件吗？ |
| [`2026-11-03-two-exception-classes-moved-and-nobody-said-why`](2026-11-03-two-exception-classes-moved-and-nobody-said-why.cn.md) | `EXCEPTIONS` 的两个数字早就错了：补记账，还是二分到引入提交？ |
| [`2026-11-03-what-does-region-of-zero-answer-for-a-graph-without-regions`](2026-11-03-what-does-region-of-zero-answer-for-a-graph-without-regions.cn.md) | 一张没有区表的图，`region_of(0)` 该答真记录还是报错？ |
| [`2026-11-03-what-does-taking-pure-nodes-off-the-chain-cost`](2026-11-03-what-does-taking-pure-nodes-off-the-chain-cost.cn.md) | 纯节点离开链位要付 59 条红和一条封存拒绝：P0-2 第二步还做吗？ |
| [`2026-11-03-what-type-does-an-error-binding-have-across-a-yield`](2026-11-03-what-type-does-an-error-binding-have-across-a-yield.cn.md) | `case error e` 绑定的名字跨 `yield` 进状态时，那个 cell 的类型从哪来 |
| [`2026-11-03-when-a-lambda-ends-with-a-with-call-what-does-split-cut`](2026-11-03-when-a-lambda-ends-with-a-with-call-what-does-split-cut.cn.md) | 被微分的 lambda 以「调用带 `with` 的函数」收尾时，`split` 该剪什么？ |
| [`2026-11-03-where-does-the-chain-tail-service-live`](2026-11-03-where-does-the-chain-tail-service-live.cn.md) | 出口门要等的"链上最后一站"，这个服务归谁？ |
| [`2026-11-03-where-does-the-loop-results-residual-land`](2026-11-03-where-does-the-loop-results-residual-land.cn.md) | "循环结果作为残差跨过切口"这条裁定，落在哪一格？ |
| [`2026-11-03-which-layer-carries-the-libstdcxx-path`](2026-11-03-which-layer-carries-the-libstdcxx-path.cn.md) | 闸门的 libstdc++ 路径带在哪一层——"只在 `-Check` 加"不成立，那么要不要收敛成一条？ |
| [`2026-11-03-which-p0-2-does-the-order-mean`](2026-11-03-which-p0-2-does-the-order-mean.cn.md) | 排序里那条 `P0-2` 指 A 还是 B？A 的两处是不是同一个条件（逐分支量：不是） |
| [`2026-11-03-who-carries-the-forward-halfs-intermediate-values`](2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md) | 转置图需要前向那一半的中间值，谁携带它？ |
| [`2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth`](2026-11-03-why-the-self-test-grew-expensive-without-any-test-count-growth.cn.md) | 自检什么时候变贵了：成本已逐项归因，没有余额，也没有可比的前后读数 |
| [`array-scalar-product-transpose`](array-scalar-product-transpose.cn.md) | 数组×标量那条路：律已就位、形状已按类型拒绝，剩下的是"律的答案类型"这一问 |
