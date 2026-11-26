# 报告页引用：27 条指错目录的改回来了，24 条指向已撤的页，278 页没人引用

**状态：已落。** 一笔提交在 `lane/misc` 上，基点是 `origin/integration` 的 `aa1b23b9a7`。
本页记四件事：口径与读数、改了什么（28 行）、没改的逐类说明、以及两份逐条附录。

## 一、口径

```
读的东西：internal/backlog.md + internal/reports/*.md（324 个文件）
找的东西：一个路径形状（`internal/…`、`docs/…`、`src/…`、`test/…` 等，带扩展名，可选 `:行号`）
         和一个页面名形状（`<日期>-<名字>.md`，可选 `:行号`）
判四类：目标在树里不存在 / 目标存在但换了目录 / 行号超出文件长度 / 旧布局的源码路径
```

**先记一条最容易读错的事：`internal/` 不在 `--citations` 的读法里。**
`scripts/check-doc-links.mjs` 的 `RECORD_DIRS = new Set(['internal', 'docs/reviews'])`，扫文件时整目录跳过。
所以这份债**没有任何闸门会看见**，`--citations` 绿不等于这些引用指得着 —— 这正是这一格要人工对一遍的理由。

## 二、读数（同一条判据，改前 → 改后）

| 类 | 含义 | 改前 | 改后 | 处置 |
|---|---|---:|---:|---|
| 引用 token 总数 | 路径 + 页面名两种形状 | 5887 | 5887 | — |
| 指得着 | 目标在树里、行号在范围内 | 4501 | 4501 | — |
| B 换了目录 | 页面在，但不在写的那个目录 | **27** | **0** | **已改（本节三）** |
| A 目标不存在 | `internal` 页面 24 + 其余（源码/工具/运行产物）176 | 201 | 200 | 只报（本节四、附录 A） |
| C 行号越界 | 行号超过文件长度 | 1 | 1 | 只报（backlog 侧） |
| D 旧布局 | 目标换了位置，只剩旧名字 | 1187 | 1186 | 只报 |
| 没人引用的报告页 | 页面与 backlog 都不提它 | 278 | 278 | 只报（附录 B） |

## 三、改了什么：28 行，23 个文件（全在 `internal/reports/` 页内）

| 改法 | 处 | 例 |
|---|---:|---|
| `internal/questions/<页>` → `internal/reviews/<页>` | 26 | `2026-11-03-what-deleting-the-engine-pullback-takes.cn.md:71` |
| `internal/reports/2026-11-18-nested-transforms.cn.md` → `internal/plans/` | 1 | `2026-11-18-canonical-unreachable-and-a-draft-s-id.cn.md:51` |
| 名字写岔（少 `questions-` 前缀、少 `.cn` 之外的分节） → 现名 | 1 | `vmap-coverage-of-tensor-primitives-2026-11-01.md:254` |

判据（四条都要成立，逐条量过）：

```
· 每条新目标都存在：脚本对 28 条逐条 `existsSync(新目标)`，无一例外
· 4 条带行号：行号落在新目标的行数之内（49 / 45 / 15 / 32）
· 一次只改一行：脚本断言"这一行里旧串只出现 1 次"再替换；`git diff --numstat` = 23 文件 28 增 28 删
· check-doc-links --citations → 1182/1182 symbol、2640 path、7115 文件、exit 0（改前改后同）
```

## 四、没改的：逐类为什么

**(a) 24 条指向已撤的 `internal` 页面**（附录 A）。19 条是页面真有历史、后来被删（多数在 2026-10-23～25 与 11-01 的两批整档），删的是"那一页的推理已经进了设计原则"的那种；把它们改成今天某个页，是替历史换指路牌。5 条任何提交都没带过那个名字（其中 1 条已经在三节按现名修掉，余 4 条是当时的笔误，目标从没写出来过）。

**(b) backlog 侧 29 条指不着的目标 + 1 条行号越界**（只报，不改）。行号那条是
`internal/backlog.md:267` 引 `internal/collab/rules.md:45`，而那份文件只有 41 行。目标不存在的 29 条里，
一类是**已撤的工具**（`scripts/merge-readings.mjs`、`scripts/merge-record.mjs`、`internal/tools/merge-lane.ps1`、
`merge-batch.ps1`、`merge-lane-config.ps1`、`scripts/plan-set.mjs`、`scripts/process-traces-baseline.json`），
一类是**已撤的页**（`internal/lanes.md`、`docs/handover/lanes.cn.md`、`docs/design/architecture.cn.md`），
一类是**运行产物**（`test/selection/readings/<hash>.json`），还有 3 条是**从没写出来的报告页**
（`2026-11-24-the-merge-message-said-from-disk.cn.md`、`2026-11-27-resnet-block-space.cn.md`、
`2026-11-27-where-a-graph-lives-and-why-its-frame-record-does-not-move.cn.md`）。
另有 `internal/backlog.md:1139` 引 `internal/reports/2026-11-27-process-traces-reading.cn.md` —— 那一页在合并队列的
`lane/ast-rerecord` 上，还没进 `integration`，所以在这棵树上是"不存在"；合进去即指得着。

**(c) 1186 条旧布局的源码路径。** 那些页记的是"当天在那棵树上量到什么"，路径是那条读数的坐标
（`src/passes/sched/nodevm/nodevm.cpp` 后来搬成 `src/stdlib/std/nvm/nodevm.cpp`）。改路径等于改坐标，
读数与它指向的行就对不上了；这和 `--citations` 只验 `.md` 路径、不验源码路径是同一个分寸。

**(d) 278 页没人引用（附录 B）。** `internal/reports/` 没有索引页，也没有生成器：`ls` 是唯一入口。
这些页大多是一次性读数（普查、清单、某天的红表），它们的读者是当时的对话，不是后来的引用。
要收这个口，做法是**生成一份索引**（按日期与主题两栏，从目录生成、不手写），不是逐页补引用 —— 那是一件新活。

## 五、附录 A：24 条指向已撤 `internal` 页面的引用

```
internal/backlog.md:108  internal/lanes.md
      页面已删：1643941ade 2026-11-23 lanes: the table leaves the repository, and the plugin stops reading a file nobody writes
internal/backlog.md:245  internal/reports/2026-11-24-the-merge-message-said-from-disk.cn.md
      页面已删：（历史里没有删除提交，名字改过）
internal/backlog.md:1128  internal/reports/2026-11-27-resnet-block-space.cn.md
      页面已删：（历史里没有删除提交，名字改过）
internal/backlog.md:1130  internal/reports/2026-11-27-where-a-graph-lives-and-why-its-frame-record-does-not-move.cn.md
      页面已删：（历史里没有删除提交，名字改过）
internal/backlog.md:1139  internal/reports/2026-11-27-process-traces-reading.cn.md
      页面已删：（历史里没有删除提交，名字改过）
internal/reports/2026-11-04-b3-reverted-and-the-ten-it-does-not-explain.cn.md:112  internal/questions/2026-11-04-a-sum-to-that-takes-the-shape-itself.cn.md
      任何提交都没带过这个名字
internal/reports/2026-11-04-four-classes-of-written-types-and-names.cn.md:21  internal/questions/2026-11-04-should-a-law-retried-at-float-be-a-refusal.cn.md
      任何提交都没带过这个名字
internal/reports/2026-11-04-what-the-unmerged-lanes-still-hold.cn.md:45  internal/questions/2026-11-02-can-any-spelling-reach-set-index.cn.md
      任何提交都没带过这个名字
internal/reports/2026-11-05-what-the-eighty-seven-unmerged-lanes-still-hold.md:114  internal/questions/2026-11-02-can-any-spelling-reach-set-index.cn.md
      任何提交都没带过这个名字
internal/reports/2026-11-23-the-96-statements-a-check-can-read.cn.md:185  internal/lanes.md
      页面已删：1643941ade 2026-11-23 lanes: the table leaves the repository, and the plugin stops reading a file nobody writes
internal/reports/ci-failures.md:42  internal/questions/reduced-precision-text.md
      页面已删：6eb2ad449f 2026-10-23 docs: reduced-precision text is shortest in its own type, and log_softmax is generic over float element types
internal/reports/H232-ac-canonical-form.md:86  internal/questions/generic-rule-instantiation.md
      页面已删：9d55d74b00 2026-10-23 docs: a generic rule is instantiated where it matches, from the matched nodes' types, and cached per type
internal/reports/H232-generic-rule-bridge.md:3  internal/questions/generic-rule-instantiation-set-and-predicate.md
      页面已删：71b9a44526 2026-10-23 docs: rule conditions resolve where declared, instantiation sets per use, and same-named declarations split by disjoint conditions
internal/reports/H232-generic-rule-bridge.md:311  internal/questions/accuracy-check-sampling.md
      页面已删：d0908ef548 2026-10-24 docs: verification propositions per permission, a report beside the library, seeds by rule identity, and what refine_accuracy promises
internal/reports/tensor-flip-integration.md:141  internal/questions/bare-tensor-parameter-instantiation-measured.md
      页面已删：f5ac8fd763 2026-10-24 docs: BLOCK stays off develop, match guards get a lane, inlining measures the callee, and the bare-Tensor instantiation lane is withdrawn
internal/reports/tensor-flip-integration.md:156  internal/questions/unstated-element-type-scalar-and-print.md
      页面已删：f2ce4e88ac 2026-10-24 docs: splice the sync body verbatim, compound assignment follows the operator, the transition type gets no new semantics, and reassociation is checked in higher precision
internal/reports/tensor-flip-integration.md:415  internal/questions/bare-tensor-parameter-instantiation-measured.md
      页面已删：f5ac8fd763 2026-10-24 docs: BLOCK stays off develop, match guards get a lane, inlining measures the callee, and the bare-Tensor instantiation lane is withdrawn
internal/reports/tensor-flip-integration.md:675  internal/questions/bare-tensor-parameter-instantiation.md
      页面已删：6c4b371e69 2026-10-24 docs: a bare Tensor parameter is instantiated at its call site, as every generic part is
internal/reports/tensor-flip-integration.md:785  internal/questions/bare-tensor-parameter-instantiation.md
      页面已删：6c4b371e69 2026-10-24 docs: a bare Tensor parameter is instantiated at its call site, as every generic part is
internal/reports/two-layer-reds-of-the-flip-line.md:208  internal/questions/var-cell-widening-and-the-bare-tensor-return.md
      页面已删：3d3463f20d 2026-10-25 Review: bare Tensor signatures and grad's transitional result give way to concrete types; the training-peak regression gates the flip
internal/reports/two-layer-reds-of-the-flip-line.md:241  internal/questions/declaration-evaluator-facts-vs-kernels.md
      页面已删：96b2b4dfa0 2026-10-25 Review: named dimensions make no instances (earlier expectation retracted) and a growing type argument is refused at once; in place is planned once and counted on the array; one subscript rule in the core; reductions round once in their definitions
internal/reports/two-layer-reds-of-the-flip-line.md:287  internal/questions/declaration-evaluator-facts-vs-kernels.md
      页面已删：96b2b4dfa0 2026-10-25 Review: named dimensions make no instances (earlier expectation retracted) and a growing type argument is refused at once; in place is planned once and counted on the array; one subscript rule in the core; reductions round once in their definitions
internal/reports/two-layer-reds-of-the-flip-line.md:293  internal/questions/canonical-node-key-keeps-one-spelling.md
      任何提交都没带过这个名字
internal/reports/when-at-the-call-site.md:65  internal/questions/two-when-conditions-hold.md
      页面已删：281a228b27 2026-10-24 docs: two holding conditions are an ambiguity, and the slow layer's reds get owners in order
```

## 六、附录 B：278 个没人引用的报告页

生成口径：`internal/backlog.md` 与全部 323 个报告页里都不出现这个名字（自己提自己不算）。

```
2026-10-25-flip-landing-checklist.cn.md
2026-11-02-a-green-run-proves-the-guards-it-walked-past.cn.md
2026-11-02-a-lambdas-rest-parameter.cn.md
2026-11-02-an-authors-commit-on-a-lane-branch.cn.md
2026-11-02-deleting-the-upperscan-unmark-costs-38-reds.cn.md
2026-11-02-how-the-front-end-would-stop-building-ne-on-a-built-in-eq.cn.md
2026-11-02-p0-2-a-item-and-the-cells-without-readings.cn.md
2026-11-02-p0-2-which-control-edges-still-count-as-chain-positions.cn.md
2026-11-02-p0-2-who-the-control-edge-speaks-for.cn.md
2026-11-02-p0-3-order-edges-are-not-a-credential.cn.md
2026-11-02-p1-13-does-merging-from-bytes-need-a-new-mechanism.cn.md
2026-11-02-p1-13-the-op-wildcard-works-in-one-place-only.cn.md
2026-11-02-p1-13-there-are-five-names-for-one-operator.cn.md
2026-11-02-p1-13-three-columns-call-shape.cn.md
2026-11-02-p1-13-which-kind-of-family-is-the-twelve-pow-rows.cn.md
2026-11-02-p1-13-which-operator-uris-have-no-compiled-declaration.cn.md
2026-11-02-rework-1-and-2-status.cn.md
2026-11-02-slice4-checklist-marked-and-the-gates-own-wait.cn.md
2026-11-02-the-age-field-saturates-at-fifteen.cn.md
2026-11-02-the-array-case-kept-out-of-the-plan.cn.md
2026-11-02-the-known-fail-mark-that-did-nothing.cn.md
2026-11-02-the-m4-autodiff-census-three-columns.cn.md
2026-11-02-the-milestone-layer-nobody-ran.cn.md
2026-11-02-the-name-counter-retired.cn.md
2026-11-02-the-poison-reading-and-the-dangling-node.cn.md
2026-11-02-the-port-type-came-from-a-literal-float.cn.md
2026-11-02-the-residual-chain-is-dead-and-four-spots-to-remove.cn.md
2026-11-02-the-transpose-spec-is-withdrawn-here-is-where-it-is-taught.cn.md
2026-11-02-the-two-narrowings-of-the-credential.cn.md
2026-11-02-the-value-chain-after-the-port-fix.cn.md
2026-11-03-a-graph-after-an-opt-pass-is-not-the-graph-that-was-built.cn.md
2026-11-03-a-half-of-a-two-state-loop-is-a-projection-of-that-loop.cn.md
2026-11-03-a-transformer-learns-a-position-where-it-could-only-abstain.cn.md
2026-11-03-a-transposed-loop-graph-crashes-when-it-runs.cn.md
2026-11-03-a-whole-plan-excluded-for-one-entry-in-it.cn.md
2026-11-03-both-escapes-of-the-1210-ruling-are-unwritable.cn.md
2026-11-03-branch-backlog.cn.md
2026-11-03-does-a-partial-transposition-need-the-operand-values.cn.md
2026-11-03-ledger-rework-checklist.cn.md
2026-11-03-library-vjp-walls.cn.md
2026-11-03-night-handover-what-landed-what-is-blocked-and-three-decisions.cn.md
2026-11-03-no-var-write-sits-before-a-read.cn.md
2026-11-03-p0-2-chain-position-two-shapes-and-the-anchor-with-two-meanings.cn.md
2026-11-03-p0-2-invariant-side-two-predicates-that-cannot-replace-the-mark.cn.md
2026-11-03-p0-2-the-three-numbers-recounted.cn.md
2026-11-03-seven-marks-none-of-them-pins-a-reason.cn.md
2026-11-03-sync-and-ctrl-are-not-the-same-set-but-the-difference-is-inert.cn.md
2026-11-03-the-forward-rule-of-a-conversion-from-an-integer.cn.md
2026-11-03-the-milestone-layer-is-where-the-six-reds-live.cn.md
2026-11-03-the-three-premises-p02-stands-on.cn.md
2026-11-03-the-transpose-closure-can-be-inherited-but-not-extended.cn.md
2026-11-03-what-deleting-the-engine-pullback-takes.cn.md
2026-11-03-what-is-unreachable-and-uncalled.cn.md
2026-11-03-what-the-closure-identity-table-left.cn.md
2026-11-03-what-the-ten-reds-need-when-transpose-stops-reading-by-position.cn.md
2026-11-03-which-gradient-criteria-cannot-tell-right-from-wrong.cn.md
2026-11-03-which-operators-the-rule-key-scheme-cannot-spell.cn.md
2026-11-03-who-holds-the-pair-is-a-fact-not-a-type.cn.md
2026-11-04-a-zero-tangent-term-that-was-never-built.cn.md
2026-11-04-extent-of-reads-the-type-and-hands-it-over.cn.md
2026-11-04-four-classes-of-written-types-and-names.cn.md
2026-11-04-is-there-any-read-of-a-parameter-value-through-a-port.cn.md
2026-11-04-no-jgir-golden-was-ever-red.cn.md
2026-11-04-pending-questions-audit.cn.md
2026-11-04-readings-moved-out-of-plan-notes.cn.md
2026-11-04-ten-numeric-assertions-and-the-declared-ulp-bound.cn.md
2026-11-04-the-host-ccache-lost-its-exec-target.cn.md
2026-11-04-the-ledger-counts-a-word-not-a-connection.cn.md
2026-11-04-the-product-law-cannot-read-the-other-operand.cn.md
2026-11-04-the-two-vector-laws-ask-for-a-shape-the-declaration-does-not-write.cn.md
2026-11-04-the-written-form-in-a-comment.cn.md
2026-11-04-what-the-author-answered.cn.md
2026-11-04-what-the-ten-lane-batch-owes.cn.md
2026-11-04-what-the-unmerged-lanes-still-hold.cn.md
2026-11-04-where-the-library-and-the-engine-still-differ.cn.md
2026-11-04-which-fused-gradients-write-every-element.cn.md
2026-11-04-who-still-reads-the-by-position-take.cn.md
2026-11-04-zero-carrier-deleted-and-what-it-was-load-bearing-for.cn.md
2026-11-05-four-places-the-merger-edited-another-lanes-code.cn.md
2026-11-05-the-fifteen-item-list-read-against-the-tree.cn.md
2026-11-05-the-place-fork-guard-walks-order-edges-and-no-reading-needs-it.cn.md
2026-11-05-the-preserve-list-lost-two-branches-and-what-the-reflog-found.cn.md
2026-11-05-the-reverse-has-aux-stops-on-transpose-refusing-a-pair.md
2026-11-05-the-six-rulings-of-eleven-thirty-and-where-each-lands.cn.md
2026-11-05-the-triage-of-the-eighty-seven-unmerged-branches.cn.md
2026-11-05-what-the-eighty-seven-unmerged-lanes-still-hold.md
2026-11-05-where-the-zero-gradient-comes-from.md
2026-11-05-why-split-needs-no-lifting-rule-and-what-the-rule-would-have-returned.cn.md
2026-11-13-architecture-audit.cn.md
2026-11-15-alias-read-then-write-red-readings.cn.md
2026-11-15-counted-boxes-under-load.cn.md
2026-11-15-is-the-stream-family-load-dependent.cn.md
2026-11-15-the-anchor-counts-boxes-not-values.cn.md
2026-11-15-the-anchor-handoff.cn.md
2026-11-15-the-count-has-two-primitives.cn.md
2026-11-15-the-last-use-column-and-the-block-that-owns-itself.cn.md
2026-11-15-the-var-migration-surface.cn.md
2026-11-15-two-probe-patches-replayed-from-words.cn.md
2026-11-15-var-annotation-is-an-upper-bound.cn.md
2026-11-15-var-ssa-join-shape-right-value-wrong.cn.md
2026-11-15-var-ssa-relaxation-cannot-land-alone.cn.md
2026-11-15-var-ssa-source-is-the-cell-not-the-join.cn.md
2026-11-15-var-ssa-statement-join-does-not-select.cn.md
2026-11-15-var-ssa-step2a-rewrite-plan.cn.md
2026-11-15-var-ssa-value-branch-machine-exists.cn.md
2026-11-15-var-to-ssa-escapes-on-assignment.cn.md
2026-11-15-var-to-ssa-start.cn.md
2026-11-15-var-to-ssa-step2-blocked.cn.md
2026-11-15-var-to-ssa-step2a-landing-zone.cn.md
2026-11-15-var-to-ssa-step2a-path-pinned.cn.md
2026-11-15-var-to-ssa-step2a1-three-attempts.cn.md
2026-11-16-architecture-readings.cn.md
2026-11-16-the-generator-state-travels-by-value.cn.md
2026-11-17-e9-full-measurement.cn.md
2026-11-17-e9-gate-said-nothing-now-speaks.cn.md
2026-11-17-e9-regression-bisect.cn.md
2026-11-17-s3b-the-training-steps-update-cannot-read-in-place.cn.md
2026-11-17-the-claim-order-is-not-the-root-the-count-is.cn.md
2026-11-17-the-feat-subtree-as-one-target.cn.md
2026-11-17-the-three-queued-reworks-state.cn.md
2026-11-18-a-condition-written-on-one-side-only.cn.md
2026-11-18-assignment-kind-1-3.cn.md
2026-11-18-canonical-unreachable-and-a-draft-s-id.cn.md
2026-11-18-e9-readings-at-9d589d2f3.cn.md
2026-11-18-e9-six-bisected-by-seed.cn.md
2026-11-18-eco-measurements.cn.md
2026-11-18-rc23-bisect-and-where-the-repair-stands.cn.md
2026-11-18-status-on-e9-and-generator-state.cn.md
2026-11-18-the-28-reds-were-one-operator-declaring-nothing.cn.md
2026-11-18-the-cell-a-handle-needs-no.cn.md
2026-11-18-the-generator-machine-builds-quad-and-publication-refuses-three-order-edges.cn.md
2026-11-18-the-generator-machine-s-next-segment-hits-a-nested-graph.cn.md
2026-11-18-the-generator-state-holds-only-pc.cn.md
2026-11-18-three-reds-one-shape-the-front-end-made-a-graph-it-would-refuse.cn.md
2026-11-18-two-implementations-of-one-pass.md
2026-11-18-what-to-do-next-from-the-graph-editing-draft.cn.md
2026-11-19-ecoop-pin-2d7c5c3a1.md
2026-11-19-the-default-pipeline-was-measured-across-the-alias-withdrawal.en.md
2026-11-19-the-generator-cell-is-never-patched.cn.md
2026-11-20-where-the-opt-twice-cpu-goes.cn.md
2026-11-22-four-legs-red-on-3564f5d37.cn.md
2026-11-22-lane-table.cn.md
2026-11-22-panic-has-no-writer.cn.md
2026-11-22-round-failures.cn.md
2026-11-22-second-move-reader-inventory.cn.md
2026-11-22-ten-gates.cn.md
2026-11-22-the-four-red-legs-are-four-things.cn.md
2026-11-22-the-opt-twice-slowdown-is-the-timing-harness.cn.md
2026-11-23-the-168-operators-47-written-121-not.cn.md
2026-11-23-the-96-statements-a-check-can-read.cn.md
2026-11-23-the-rewrite-readers-triaged.cn.md
2026-11-24-a-while-loop-has-no-element-port.cn.md
2026-11-24-c1124-93-range-comprehension-plan.cn.md
2026-11-24-coverage-table.cn.md
2026-11-24-defect-a-known-fail-mark-with-no-owner-and-an-expired-sentence.cn.md
2026-11-24-does-the-move-change-any-red.cn.md
2026-11-24-eight-runs-the-panel-would-call-dead.cn.md
2026-11-24-promotion-2.cn.md
2026-11-24-promotion-3.cn.md
2026-11-24-the-37-cases-that-exit-1-without-saying-why.cn.md
2026-11-24-the-eleven-always-red-cases-each-on-a-line.cn.md
2026-11-24-the-generator-sweep-read-every-emitter.cn.md
2026-11-24-the-prelude-layer-reaches-a-module-not-only-the-entry.cn.md
2026-11-24-the-view-reads-tags-and-waits-and-never-payload-bytes.cn.md
2026-11-24-to_array-a-declared-prelude-function-is-not-in-scope-unasked.cn.md
2026-11-25-cc-needs-status.cn.md
2026-11-25-develop-promotion-bypassed-protection.cn.md
2026-11-25-edit-block-gap-table.cn.md
2026-11-25-process-traces-scope.cn.md
2026-11-25-time-sensitive-readers.cn.md
2026-11-25-traces-to-zero.cn.md
2026-11-26-block-identity-hash.cn.md
2026-11-26-ci-develop-two-reds.cn.md
2026-11-26-conv2d-shaped-grad-extent.cn.md
2026-11-26-edit-block-body-program.cn.md
2026-11-26-edit-block-frontend-handover.cn.md
2026-11-26-internal-page-references-and-a-stale-coverage-table.cn.md
2026-11-26-kinds-to-oper-stop.cn.md
2026-11-26-loading-has-its-own-shape.cn.md
2026-11-26-promotion-7.cn.md
2026-11-26-promotion-8.cn.md
2026-11-26-push-gate.cn.md
2026-11-26-rebase-across-the-ast-golden.cn.md
2026-11-26-ref-boxes-finalized-is-a-collector-statistic.cn.md
2026-11-26-replace-of-an-anchor-reads-its-hand-out.cn.md
2026-11-26-stream-zip-overload.md
2026-11-26-the-date-is-a-record.cn.md
2026-11-26-the-debugger-repl-three-lines.cn.md
2026-11-26-the-four-slow-capacity-cases-were-reading-a-parse-error.cn.md
2026-11-26-the-placement-family-leaves-graph-h.cn.md
2026-11-26-the-record-answers-what-the-kind-answered.cn.md
2026-11-26-the-three-transpose-laws-the-walls-behind-the-walls.cn.md
2026-11-26-two-parse-entries-one-record.cn.md
2026-11-26-two-small-defects-commit-annotation-and-example-fragment.cn.md
2026-11-26-what-a-blocks-name-reads.cn.md
2026-11-26-what-the-98-lines-are.cn.md
2026-11-27-a-blocks-root-needs-a-module-the-program-never-imported.cn.md
H232-ac-canonical-form.md
H232-generic-rule-bridge.md
H276-param-defaults.md
H366-flip-attribution.md
H367-effect-check-audit.md
a-b-before-you-know-which-copy-you-edited.md
a-known-fail-that-prints-success-and-writes-nothing.md
accuracy-softmax-trade.md
assert-is-not-a-release-guard.md
build-host-staging-refs.md
camel-for-ai-draft.notes.md
camel-writing-primer.md
code-review-2026-11-01.md
code-review-autodiff.md
code-review-backends.md
code-review-frontend.md
code-review-generics.md
code-review-index.md
code-review-indexing.md
code-review-macro-modules-gc.md
code-review-onnx-tiles.md
code-review-passes.md
code-review-python.md
code-review-rewrite.md
code-review-streams.md
code-review-tools.md
code-review-types.md
code-review-when.md
control-edge-inventory.md
control-edge-readers.md
defun-dispatch-notes.md
defun-wrapper-variant.md
entries-2026-10-30-arm-chain-place.md
executable-judgements.md
faults-table-audit.md
files-whose-name-is-not-their-nature.md
format-spec-rule-and-the-cpp-writer.md
four-items-2026-10-31.md
generator-refuse-shapes.md
gpu-g4-g5-argmax-axis.md
gpu-refusal-census.md
grad-runtime-graph-ownership.md
graph-values-audit.md
guard-effectiveness-audit.md
invariant-owner-census.md
known-failures-two.md
opt-ctrl-inventory.md
opt-cycle-composition-not-found-on-develop.md
order-edge-written-and-judged.md
p0-6-boundary-check-promises-vs-checks.cn.md
pass-boundary-effect-order-invariant-in-place.md
pending-sum-axis-rank-1.cn.md
prelude-cache-what-it-can-take.md
prelude-cost-of-autodiff-rules.md
principles-audit-2026-10-26.md
prl-two-reds-and-segments.md
probes-that-measure-the-probe.md
s2-with-grad-time-split.md
signature-types-compared-113-differences-and-why-none-is-a-defect.md
stale-numbers-in-tonight-reports.md
stdlib-blockers.md
tensor-buckets-2026-10-31.md
tensor-flip-integration.md
the-instrument-discipline.md
the-shape-a-comment-promises.md
the-value-a-pass-ends-with.md
tiles-transfer-case-ownership.md
today-state-one-number-a-row.md
transpose-two-wrong-cotangents.md
two-layer-reds-of-the-flip-line.md
unpack-count-migration.md
vmap-coverage-of-tensor-primitives-2026-11-01.md
what-the-derived-chain-place-still-drops.md
when-at-the-call-site.md
where-a-bad-pass-parameter-is-reported.md
where-the-forward-replacement-must-live.md
where-transpose-stops.md
where-two-pages-give-two-answers.md
who-checks-a-graph-cross-references.md
why-two-test-files-moved-in-the-port-order-station.md
writer-set-what-it-costs-and-what-narrows-it.md
```
