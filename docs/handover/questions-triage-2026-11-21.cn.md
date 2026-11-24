# 问题页分诊（2026-11-21）

**实数 75 页**（`docs/handover/questions/` 下 `*.md` 去掉 `README.md`；索引原记 75 page(s) / 156 kept row(s)，与实数一致，作者说的 76 含 `README.md` 本身）。

**四类：已答 12 · 取代 2 · 待决-语义原则 52 · 待决-实现 9 = 75。**
已答的 12 页已 `git mv` 进 `reviews/`；取代的 2 页留在原处（不删、不改页面内容）；待决的 61 页留在 `questions/`。

> **为什么分堆表与取代标注在这里、不在 `questions/README.md`**：那张表由
> `node scripts/check-doc-links.mjs --questions-index --write` 生成，并由同一函数**逐字比对**
> （`scripts/check-doc-links.mjs` 的 `renderQuestionsIndex` / `checkQuestionsIndex`），
> 任何手写的节或行都会让 `--questions-index` 报 `the pending table is hand-edited` 并 **exit 1**。
> 规矩是「不手改生成物」，所以两堆与取代标注落在本页，索引保持生成（生成后 exit 0）。

## 一、已答（12 页，已移入 `reviews/`）

| 页 | 依据（一句，带文件/提交/页名） |
| --- | --- |
| [`2026-11-18-specialization-carries-its-ports-one-place-over`](reviews/2026-11-18-specialization-carries-its-ports-one-place-over.cn.md) | §五「答复与本轮的落地（作者 `b68c04b31`）」：错位是缺陷，先修错位再把端口检查问遍每张图；落地在 `GraphDraft::removeNormPort` 与 `validateRuntimeGraphPortTypes`（提交 `339d04ecb`）。 |
| [`2026-11-18-the-exemption-list-accepts-a-reason-that-is-no-longer-why`](reviews/2026-11-18-the-exemption-list-accepts-a-reason-that-is-no-longer-why.cn.md) | §四(b) 的判据；提交 `78d159a0c`「an exemption is checked against the reason it names」把豁免连同它记的原因一起校验，截断与「可删」判据一并改掉。 |
| [`2026-11-19-devirtualize-names-a-differentiated-call-the-type-says-nothing-about`](reviews/2026-11-19-devirtualize-names-a-differentiated-call-the-type-says-nothing-about.cn.md) | §⑦「已落（author 2026-11-19 答复本页后）」：变换给每张图写出子句，`pullback.cpp#makePlanTypes` 写 `pure`（提交 `6226e23d5`）。 |
| [`2026-11-19-the-effect-of-a-call-through-a-function-value`](reviews/2026-11-19-the-effect-of-a-call-through-a-function-value.cn.md) | §① 作者裁定（2026-11-19 14:45）＋§⑦ 裁定与落地：`resources.cpp#Resolver::call` 改读 `with[0]` 的函数类型子句（`8bf3711d8`），`pullback.cpp#gradientSignature` 带上子句（`4012e1099`）。 |
| [`2026-11-19-the-fifty-three-ask-the-language-for-what-it-never-promised`](reviews/2026-11-19-the-fifty-three-ask-the-language-for-what-it-never-promised.cn.md) | §7 更正（作者 `af2cab221` 逐字：「the 53 are defects」）：第 4 段 (甲) 与第 5 段默认作废，要修的是 `with_grad` 把变量交下去（提交 `548a5b887`）。 |
| [`2026-11-20-may-a-law-reduce-only-when-the-extents-are-readable`](reviews/2026-11-20-may-a-law-reduce-only-when-the-extents-are-readable.cn.md) | §「已答（2026-11-20 23:09，`origin/fsh-dev = 1351b3089`，逐字）」：no `(ct, ct)` fallback / `jvp` always hands shapes as values / one transpose law per operator（提交 `7b41284df`）。 |
| [`2026-11-20-the-outer-jvp-needs-split-to-hand-out-static-code`](reviews/2026-11-20-the-outer-jvp-needs-split-to-hand-out-static-code.cn.md) | §五「作者的裁定（已到，2026-11-20 05:55，`36f91f6a4` 第 2 条 / `ea05b6826`）」：问句已答——在 0.2.45 范围里，且不是降级是改表示层。 |
| [`2026-11-20-the-before-edit-graph-beyond-its-type-costs-six-green-cases`](reviews/2026-11-20-the-before-edit-graph-beyond-its-type-costs-six-green-cases.cn.md) | §7.2 归因（一次读数混了两个程序版本，是读数错不是图错）＋§7.3 修法已落地、六条红全消（提交 `ebb384f77`）。 |
| [`2026-11-20-where-is-the-rework-list-r3-through-r7`](reviews/2026-11-20-where-is-the-rework-list-r3-through-r7.cn.md) | §「决策者答（2026-11-20 22:00）」：表在 `origin/fsh-dev` 的 `docs/handover/reviews/questions-2026-10-29.md`，并逐条给出 R3–R7（提交 `3b3e37b6e`）。 |
| [`2026-11-20-whether-jvp-and-vjp-build-two-graphs-of-one-function`](reviews/2026-11-20-whether-jvp-and-vjp-build-two-graphs-of-one-function.cn.md) | §6「不是两张图不一致，是 `jvp` 把一次**调用**写成了**函数值**」，§7 前逐字「本页问的那件事已经有答案了」；造图那一半修在 `991ee512c`。 |
| [`2026-11-21-whether-a-pinned-sentence-follows-the-wall-it-was-pinned-to`](reviews/2026-11-21-whether-a-pinned-sentence-follows-the-wall-it-was-pinned-to.cn.md) | §0 裁定（负责人 2026-11-21 02:20）：A/B/C 都不选，走第四条——目标是算出对的数；15 处标记改成「必须打印的数」（提交 `a2a0acd3a`）。 |
| [`2026-11-21-where-is-a-fill-built-closures-layout-recorded`](reviews/2026-11-21-where-is-a-fill-built-closures-layout-recorded.cn.md) | §四「本页默认已作废（同日撤回）…裁定改为路 B」：闭包布局按 `include/camel/core/type/composite/func.h:14-16` 是**图**的事实，`jvp.cpp:914` 的图级 `setClosureType` 即其落地（提交 `0fd5b4e81`）。 |

## 二、被取代（2 页，留在 `questions/` 不动，此处标注后关闭）

| 页 | 被哪一条取代（具体） |
| --- | --- |
| [`2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve`](questions/2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve.cn.md) | 被页 `2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve-or-five.cn.md` 取代：后者首段逐字「这一页取代同目录 `…-says-twelve.cn.md` 的第 ① 节」，并重新给了 12 / 5 两组读数（提交 `f0b1e06b3`）。 |
| [`2026-11-19-the-front-end-in-place-rewriting-is-unreachable-not-removed`](questions/2026-11-19-the-front-end-in-place-rewriting-is-unreachable-not-removed.cn.md) | 被页 `2026-11-19-the-front-end-in-place-path-is-reachable-for-var-parameters.cn.md` 取代：后者实测「它们对 `var` 形参仍在跑、原地形照样出现在图里」，前者的「已不可达」前提被推翻（提交 `bf389cfe0`）。 |

## 三、待决-语义原则（52 页，给负责人）

| 页 |
| --- |
| [`2026-11-18-a-call-to-a-pure-function-has-no-place-on-the-chain`](questions/2026-11-18-a-call-to-a-pure-function-has-no-place-on-the-chain.cn.md) |
| [`2026-11-18-the-citation-check-demands-a-page-that-lives-on-the-authors-branch`](questions/2026-11-18-the-citation-check-demands-a-page-that-lives-on-the-authors-branch.cn.md) |
| [`2026-11-19-a-rules-angle-argument-is-a-port`](questions/2026-11-19-a-rules-angle-argument-is-a-port.cn.md) |
| [`2026-11-19-a-sync-body-takes-the-call-s-tuple-as-the-statement-s-value`](questions/2026-11-19-a-sync-body-takes-the-call-s-tuple-as-the-statement-s-value.cn.md) |
| [`2026-11-19-a-tie-break-that-keeps-creation-order-when-fragments-are-equal`](questions/2026-11-19-a-tie-break-that-keeps-creation-order-when-fragments-are-equal.cn.md) |
| [`2026-11-19-a-tuple-flows-into-an-operator-s-arguments`](questions/2026-11-19-a-tuple-flows-into-an-operator-s-arguments.cn.md) |
| [`2026-11-19-a-var-parameter-cannot-hand-its-value-back`](questions/2026-11-19-a-var-parameter-cannot-hand-its-value-back.cn.md) |
| [`2026-11-19-a-var-parameter-recursion-loses-its-tail-call`](questions/2026-11-19-a-var-parameter-recursion-loses-its-tail-call.cn.md) |
| [`2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made`](questions/2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md) |
| [`2026-11-19-an-order-edge-across-an-inline-region-is-ignored-by-the-effect-check`](questions/2026-11-19-an-order-edge-across-an-inline-region-is-ignored-by-the-effect-check.cn.md) |
| [`2026-11-19-in-place-the-two-orders-cannot-both-hold`](questions/2026-11-19-in-place-the-two-orders-cannot-both-hold.cn.md) |
| [`2026-11-19-order-cannot-reach-a-node-that-inline-just-made`](questions/2026-11-19-order-cannot-reach-a-node-that-inline-just-made.cn.md) |
| [`2026-11-19-removing-the-packing-bit-and-computing-from-the-declaration-presuppose-each-other`](questions/2026-11-19-removing-the-packing-bit-and-computing-from-the-declaration-presuppose-each-other.cn.md) |
| [`2026-11-19-route-b-s-113-failures-split-into-two-classes`](questions/2026-11-19-route-b-s-113-failures-split-into-two-classes.cn.md) |
| [`2026-11-19-rule-overlap-is-judged-by-value-or-by-writing`](questions/2026-11-19-rule-overlap-is-judged-by-value-or-by-writing.cn.md) |
| [`2026-11-19-s1-1-names-a-program-that-is-not-legal`](questions/2026-11-19-s1-1-names-a-program-that-is-not-legal.cn.md) |
| [`2026-11-19-the-call-construction-sites-and-the-three-sources-of-an-exit-type`](questions/2026-11-19-the-call-construction-sites-and-the-three-sources-of-an-exit-type.cn.md) |
| [`2026-11-19-the-declaration-the-first-order-needs-is-the-field-the-second-order-deletes`](questions/2026-11-19-the-declaration-the-first-order-needs-is-the-field-the-second-order-deletes.cn.md) |
| [`2026-11-19-the-declared-type-slot-is-its-own-only-witness`](questions/2026-11-19-the-declared-type-slot-is-its-own-only-witness.cn.md) |
| [`2026-11-19-the-entry-packs-nothing-and-the-five-reds-are-a-call-s-tuple`](questions/2026-11-19-the-entry-packs-nothing-and-the-five-reds-are-a-call-s-tuple.cn.md) |
| [`2026-11-19-the-front-end-in-place-path-is-reachable-for-var-parameters`](questions/2026-11-19-the-front-end-in-place-path-is-reachable-for-var-parameters.cn.md) |
| [`2026-11-19-the-gate-value-has-five-readings`](questions/2026-11-19-the-gate-value-has-five-readings.cn.md) |
| [`2026-11-19-the-open-loop-give-back-is-12-call-sites`](questions/2026-11-19-the-open-loop-give-back-is-12-call-sites.cn.md) |
| [`2026-11-19-the-packing-overwrites-what-the-declaration-said`](questions/2026-11-19-the-packing-overwrites-what-the-declaration-said.cn.md) |
| [`2026-11-19-the-packing-slot-came-back-under-another-name`](questions/2026-11-19-the-packing-slot-came-back-under-another-name.cn.md) |
| [`2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve-or-five`](questions/2026-11-19-the-thirteen-expect-ten-and-the-mechanism-says-twelve-or-five.cn.md) |
| [`2026-11-19-three-attempts-at-projecting-a-calls-value-and-what-each-refuted`](questions/2026-11-19-three-attempts-at-projecting-a-calls-value-and-what-each-refuted.cn.md) |
| [`2026-11-19-what-a-rollback-puts-back-the-pointer-or-the-content`](questions/2026-11-19-what-a-rollback-puts-back-the-pointer-or-the-content.cn.md) |
| [`2026-11-19-what-the-user-sees-of-the-packing-the-entry-graph-makes`](questions/2026-11-19-what-the-user-sees-of-the-packing-the-entry-graph-makes.cn.md) |
| [`2026-11-20-a-law-that-states-its-angle-answers-only-that-angle`](questions/2026-11-20-a-law-that-states-its-angle-answers-only-that-angle.cn.md) |
| [`2026-11-20-a-ledger-that-may-only-fall-meets-a-legal-merge`](questions/2026-11-20-a-ledger-that-may-only-fall-meets-a-legal-merge.cn.md) |
| [`2026-11-20-a-marker-that-asks-for-a-var-the-compiler-folded-away`](questions/2026-11-20-a-marker-that-asks-for-a-var-the-compiler-folded-away.cn.md) |
| [`2026-11-20-parked-means-paused-or-deleted`](questions/2026-11-20-parked-means-paused-or-deleted.cn.md) |
| [`2026-11-20-the-ecoop-item-1-row-is-not-a-pure-call-misread`](questions/2026-11-20-the-ecoop-item-1-row-is-not-a-pure-call-misread.cn.md) |
| [`2026-11-20-the-higher-order-footprint-has-a-place-and-a-reader`](questions/2026-11-20-the-higher-order-footprint-has-a-place-and-a-reader.cn.md) |
| [`2026-11-20-the-log-softmax-tangent-the-comment-sums-p-dx-the-code-scales-by-p`](questions/2026-11-20-the-log-softmax-tangent-the-comment-sums-p-dx-the-code-scales-by-p.cn.md) |
| [`2026-11-20-the-optimizer-calls-a-spent-growth-budget-a-fixpoint`](questions/2026-11-20-the-optimizer-calls-a-spent-growth-budget-a-fixpoint.cn.md) |
| [`2026-11-20-the-refusal-of-an-unnamed-access-waits-for-the-declarations`](questions/2026-11-20-the-refusal-of-an-unnamed-access-waits-for-the-declarations.cn.md) |
| [`2026-11-20-the-tangent-argument-record-is-not-written-by-the-transform`](questions/2026-11-20-the-tangent-argument-record-is-not-written-by-the-transform.cn.md) |
| [`2026-11-20-the-two-derivative-graphs-are-not-in-the-canonical-document`](questions/2026-11-20-the-two-derivative-graphs-are-not-in-the-canonical-document.cn.md) |
| [`2026-11-20-the-v1-jgir-document-is-a-function-of-the-binary-not-of-the-program`](questions/2026-11-20-the-v1-jgir-document-is-a-function-of-the-binary-not-of-the-program.cn.md) |
| [`2026-11-20-what-transpose-reads-a-lifted-kernel-call-by`](questions/2026-11-20-what-transpose-reads-a-lifted-kernel-call-by.cn.md) |
| [`2026-11-20-when-a-laws-doc-formula-and-its-right-side-disagree`](questions/2026-11-20-when-a-laws-doc-formula-and-its-right-side-disagree.cn.md) |
| [`2026-11-20-whether-jvp-of-a-closure-answering-function-is-a-pair-or-a-lifted-closure`](questions/2026-11-20-whether-jvp-of-a-closure-answering-function-is-a-pair-or-a-lifted-closure.cn.md) |
| [`2026-11-20-whether-the-second-with-parameter-is-the-operand-the-cut-drops`](questions/2026-11-20-whether-the-second-with-parameter-is-the-operand-the-cut-drops.cn.md) |
| [`2026-11-21-how-does-a-lift-know-it-is-a-lift-at-build-time`](questions/2026-11-21-how-does-a-lift-know-it-is-a-lift-at-build-time.cn.md) |
| [`2026-11-21-the-record-and-the-type-do-not-say-the-same-thing-about-a-kernels-arguments`](questions/2026-11-21-the-record-and-the-type-do-not-say-the-same-thing-about-a-kernels-arguments.cn.md) |
| [`2026-11-21-where-does-the-capture-tangent-get-its-value`](questions/2026-11-21-where-does-the-capture-tangent-get-its-value.cn.md) |
| [`2026-11-21-whether-transpose-can-say-its-signature-is-lifted`](questions/2026-11-21-whether-transpose-can-say-its-signature-is-lifted.cn.md) |
| [`2026-11-21-which-reader-may-fill-a-lifts-value-half`](questions/2026-11-21-which-reader-may-fill-a-lifts-value-half.cn.md) |
| [`2026-11-21-who-lifts-the-members-of-a-closure-fill`](questions/2026-11-21-who-lifts-the-members-of-a-closure-fill.cn.md) |
| [`2026-11-21-who-pairs-the-captures-of-a-body-built-closure`](questions/2026-11-21-who-pairs-the-captures-of-a-body-built-closure.cn.md) |

## 四、待决-实现（9 页，给决策者）

| 页 | 留给决策者的是什么 |
| --- | --- |
| [`2026-11-19-a-write-then-read-through-a-call-has-no-printable-subject`](questions/2026-11-19-a-write-then-read-through-a-call-has-no-printable-subject.cn.md) | 那个读法已无题材：摘掉 `effects/callback_write_ignored` 并改期望行，还是另造题材。 |
| [`2026-11-19-packing-the-entry-graph-four-steps-and-two-failed-attempts`](questions/2026-11-19-packing-the-entry-graph-four-steps-and-two-failed-attempts.cn.md) | 方案已定死，等一次干净落笔（接手页，不是问题页）。 |
| [`2026-11-19-the-call-site-refresh-in-the-tiles-pass`](questions/2026-11-19-the-call-site-refresh-in-the-tiles-pass.cn.md) | 交下一手：`region_outline.cpp` 已划给 order 族车道。 |
| [`2026-11-19-the-card-quiets-signalling-nans`](questions/2026-11-19-the-card-quiets-signalling-nans.cn.md) | 四条判据要不要容忍设备 quiet 掉的 sNaN 负载位（用例断言侧）。 |
| [`2026-11-19-the-three-steps-target-code-exists-only-on-the-parked-branch`](questions/2026-11-19-the-three-steps-target-code-exists-only-on-the-parked-branch.cn.md) | 先修 `lane-ssa` 的 104 条红再执行三步，还是 develop 上只做第三步。 |
| [`2026-11-19-the-writes-a-clause-refresh-adds-have-no-order`](questions/2026-11-19-the-writes-a-clause-refresh-adds-have-no-order.cn.md) | 刷新声明出的写由谁排序，落点在 effect order（`region_outline.cpp`）。 |
| [`2026-11-20-what-a-lifted-graph-s-second-segment-means-in-the-next-cut`](questions/2026-11-20-what-a-lifted-graph-s-second-segment-means-in-the-next-cut.cn.md) | 页内逐字「这一句是给决策者的」：提升图第 2 段在下一刀 `split` 里算切向还是值。 |
| [`2026-11-20-when-a-ruling-read-a-success-line-as-the-failure`](questions/2026-11-20-when-a-ruling-read-a-success-line-as-the-failure.cn.md) | 等决策者一次更正（不是新裁定），不是负责人。 |
| [`2026-11-20-who-reads-the-two-graphs-of-a-refused-capture-on-windows`](questions/2026-11-20-who-reads-the-two-graphs-of-a-refused-capture-on-windows.cn.md) | 要一台能构建 Windows 的机器读数，本机拿不到。 |