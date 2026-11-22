# 排队工作项的状态列（`pending-work.cn.md` 的 36 个小节）

这份表的每一条状态都能用同行的命令复核；写完这一刻的基线是 `8fab0d3e1`（`origin/develop`）。状态只有五个值：`done` / `open` / `superseded` / `needs-author` / `unverified`。

**口径**：`pending-work.cn.md` 里 `^## ` 的标题共 **39** 个，其中 **36** 个带编号（1..36），另 3 个不编号（「判据（给后来的人）」「工具事实：`-Plan` 与 `-Changed` 同时给时，`-Plan` 被忽略」「新发现、未定责：`debugger.dap_*`）。下表按编号取 36 行，标题逐字抄自 `Select-String -Pattern '^## '`；那 3 个不编号的小节记在表后，不占行。

`done` 的判据是具体 sha 或树内标识符，`open` 的判据是「找过、证据不存在、今天仍该做」。本表的命令一律形如 `git -C D:\Projects\Camel-wt\ledger …`，在 `8fab0d3e1` 上读。

| # | 小节标题（逐字） | 状态 | 证据（sha / 标识符 / 命令） |
|---|---|---|---|
| 1 | `(d1)` 的删除要改走**编辑接口**，不能在 SSA 这一路另写一份 | `done` | `707edbe58`「Take the `var` unit's deletion through the edit interface, which hands the place on」（`git merge-base --is-ancestor 707edbe58 origin/develop` → 0）。今天 `src/compile/gir/builder_expr.cpp` 的 `Builder::dropCarriedVariableUnits` 只剩成员资格与走法，删除经 `draft.eraseUnreadNodes`（`git grep -n eraseUnreadNodes` → `builder_expr.cpp:2459`、`:2499`、`include/camel/runtime/draft.h:776`、`src/runtime/draft.cpp:2069`），未造第三个删除入口。 |
| 2 | **「必须先删」不等于「删了就绿」——必要性不是充分性** | `needs-author` | 实测 1/4 是真的：`8d2fa8b8f`（在 develop 上）。但本节要改的是**决策者的验收口径**，旧口径仍原样在裁定页里：`git grep -n '3/4' -- internal/reviews/q-2026-11-15-should-the-deletion-come-before-the-carrying.cn.md` → 第 51 行「三个度量转绿后报 3/4」。按 `internal/archive/handover-README.cn.md`「带日期的记录不回写」，这一格要决策者把 1/4→3/4→4/4 收进新裁定，不是车道能改的。 |
| 3 | 一处提交标题说的机制，diff 里没有（2026-11-15 记） | `needs-author` | 本节要求作者指认 `f257714fe` 的标题是「期望」还是「判断」，并明说**澄清前不许按标题改代码**。树里没有任何一页回答它（`git grep -rn 'unread unit is dropped through the edit interface'` 的唯一命中是 `pending-work.cn.md:45`，即本节引的那句标题本身）。注意 `707edbe58` 之后代码**确实**走了编辑接口，但那是后来的改动，不回答 `f257714fe` 当时是哪一种。 |
| 4 | 返工：`f883c88ee` 的第 4、5 条要撤（决策者 2026-11-16 凌晨裁定） | `done` | 两条都撤了，且可在树里读：`test/plans/feat/linear/var_read_before_write.plan.toml` 的 `desc` 原话「the 2026-11-16 ruling retires both remaining `var` branches」（第 4 条：期望里去掉 `IgnoredSideEffect`）；`test/plans/feat/linear/order_edges_regions.plan.toml` 第 3 条形状写着「none on the `LOOP`」（第 5 条：从金标去掉写之间的顺序边）。判据也落了：`agents/test.md` 末条「is it testing a rule that was retired?」。 |
| 5 | 生成文档里的示例已经不再示范那条诊断（2026-11-16 记） | `done` | 两件都做了，读在 `23142fb47` 上：`diagnostics/examples.tsv` 第 134 行由 `test/cases/compile/diag_json/warnings_visible.cml` 改钉 `test/cases/effects/callback_write_ignored.cml`——后者的调用把写捕获变量 `counter` 的回调交出去，仍在报此码；`diagnostics/registry.tsv` 的 help 去掉「写排在它之前的读之后」那半句，`src/core/error/diagnostics/messages/semantic.cpp` 与生成页 `docs/diagnostics.md` 由 `node scripts/diagnostics-catalog.mjs --write` 重生成（未手改）。凭据是两条计划读：`feat/compile/effects_syntax` 的 `callback.write_ignored_outside_sync` 断言该用例的 stderr 含该诊断整句（该句只在本诊断的模板里），`feat/linear/captured_writes` 的 `captured_write_graph.warnings` 断言 `data.name === 'SemanticDiag::IgnoredSideEffect'`；两计划 139 pass / 0 fail，`feat/compile/diag_json` 24 pass / 0 fail。 |
| 6 | 诊断示例是按名字相似度自动钉的（2026-11-16 记，第 5 节的成因） | `open` | 生成器的机制没改：`scripts/diagnostics-catalog.mjs` 的 `exampleFor` 仍按 `test.expect_diagnostic_*` 精确命中或**名字模糊命中**（`loose = … new RegExp(\`\\b${entry.name}\\b\`).test(declaredFields(test))`）挑候选，从不运行示例；`--verify-doc` 只比「页面文本 = 生成结果」；`--pinned-coverage` 数的是 **plan 里有断言**，不是「示例真的产生该码」。§5 报的那个具体错钉（`ClosureAssignsCapturedName`）已换到 `test/cases/compile/var_closure/err_closure_assigns_captured.cml`（`diagnostics/examples.tsv:70`），但会产出错钉的机制仍在。 |
| 7 | 两处纪律事实（2026-11-16 记） | `done` | 7.2 的规矩有载体：`agents/commit.md` 的 Citations 一节（「cites a **symbol**, not a line: `modules/graph/module.cpp#neKernel`」）。7.1 是警示没有交付物：`scripts/format.js` 仍在（`git ls-tree origin/develop -- scripts/format.js` 有输出），「先问这是格式化还是改动」这条只写给读者，无代码可改。 |
| 8 | `(d1)` 的编辑接口缺口已补，但它新增的保证没有被任何用例钉住（2026-11-16 记） | `open` | 本节自己点名「两处已知未做」并说那条保证「防御性的、未被验证」——按规矩就是 `open`。第 2 处仍在：`Builder::collapseNonEscapingVariableReads`（`src/compile/gir/builder_expr.cpp:1924`）删「没人用的 read」仍直接 `graphDraft.eraseNode(...)`，没经过 `eraseUnreadNodes`；也没有用例钉「整集一个节点都不动」（`git grep -rn eraseUnreadNodes origin/develop -- test` → 无命中，exit 1；全仓库的命中只有 `src/`、`include/` 与三份账页 `pending-work.cn.md`、`reviews/graph-store-step2-b-ledger.cn.md`、`reviews/questions-2026-10-29.md`）。 |
| 9 | 我写错了一个前提：`ClosureCaptureEscapes` 不是「另一条有效规则」（2026-11-16 裁定） | `done` | 诊断已从编译器、注册表与生成页删掉：`git grep -c ClosureCaptureEscapes -- diagnostics/registry.tsv` → 无命中（exit 1），`git grep -n carryOutOf -- src` → 无命中，提交 `21699b86a`「the withdrawn state rule leaves the catalogue」。5 条 `state_escape` 改报 D1、另加 5 条只读对照：`test/plans/feat/compile/state_escape.plan.toml`（`4e65c9369`、`ab5bb0527`），`test/cases/compile/state_escape/ok_read_escape_*.cml` 五条在树里。第 4 件（第 3 步工作量）在 `internal/archive/red-baseline.cn.md` 的「第 3 步的五件事」。 |
| 10 | `lane-byref` 报的两条缺陷（不归它修，记账） | `done` | A 是 `5dd0c103b`：其正文原句「a nested function that writes a handle it captured was refused with "declares uses capture 0; the graph it runs touches write state::Ref"」，修法是 `resources.h#portPlacement` 作唯一编号读法。B 是 `99560e1c9`「make a write of a handle a root of the region it is written in」，钉在 `test/plans/feat/compile/refwrite.plan.toml` 的 `refwrite.shapes`（`bare set` 形状读 `p=1`）与 `refwrite.dead_arm`（反向判据）。 |
| 11 | 作者把两条 `Ref` 缺陷升到第一档（2026-11-16） | `superseded` | 升档本身在 develop 的标题里：`704ff582a`（`git merge-base --is-ancestor` → 0），原话「…put the two Ref defects into priority one」。但本节要求的**那一步**（`lane-refdiag` 只诊断、给出根因/`file#Symbol`/炸开范围/可证伪判据）从未落页（`git grep -rln 'lane-refdiag'` 只命中本节），它被后来的直接修复顶替：`5dd0c103b`（A）与 `99560e1c9`（B），两笔都在 develop 并有用例（见第 10 行）。 |
| 12 | `lane-refwrite` 撞出的两条（都超出它那条放宽的范围） | `done` | C1（`if` 语句的值没人要时整支不运行）：`d5bb220e7`「Run a branch written as a statement with the region it is written in」，新增 `test/cases/compile/region_roots/branch_statement*.cml` 五条。C2（循环体里读写外层 `Ref` 在基线上崩）：`2b691dec1`「hold a counted value a loop reads until the loop ends」，用例 `test/cases/loops/outer_ref.cml` 里就有 `loop … do sync { state::set(box, state::get(box) + 1) }`，挂在 `test/plans/feat/linear/loops.plan.toml` 的 `loops.outer_ref`。**附带发现**：`refwrite.plan.toml` 的 `desc` 仍写这两条形状「blocked, not passed」，那句已经过期。 |
| 13 | 作者答了 `Ref` 写那页：**同意默认**，并改了原则（2026-11-16） | `done` | 原则文本已落：`origin/fsh-dev` 的 `docs/design-principles.cn.md` 3.1「**对外部资源与句柄的写由所在区域用到**（…句柄自 决策者 2026-11-16 起同此规则…）」、3.3「句柄…与外部资源同样由所在区域用到」；裁定页 `internal/reviews/q-2026-11-16-may-a-ref-write-that-nobody-reads-be-dropped.cn.md`。**本节另一个断言已过期**：`git rev-parse origin/develop:docs/design-principles.cn.md` 与 `origin/fsh-dev` 的同名文件**是同一个 blob**（`788442a5d`），所以「原则在我们这条线上永远是旧的」今天不成立。 |
| 14 | 三条流程事实（都来自今晚某条车道的实测或事故） | `open` | 14.1 有载体：`agents/branching.md`「**Never `git stash`: the stash is one stack for the whole repository, shared by every worktree.**」14.2 同旨（`agents/test.md`「A failed command produces an empty set, and an empty set passes」）。**14.3 的重排没做**：`git grep -rn 'state::' -- test/cases/tools/debugger` 无命中（证实本节「整个目录没有一处 `state::`」），`git log --since=2026-11-16 -- test/plans/feat/tools/debugger` 空——「等 `lane-ifarm` 定了再一起重排」的那一步，在 `lane-ifarm` 落地（`d5bb220e7`）之后仍没人做。 |
| 15 | 闸门名单定死成 12 个目标（2026-11-16） | `superseded` | 被 `agents/test.md` 的「The standing gate is a named set, and a plan outside it is not measured」取代：树里的闸门名单是 **6** 个（`feat/compile`、`feat/opt`、`feat/parse`、`feat/para`、`feat/tools`，2026-11-18 起再加 `feat/linear`），且明写「a merge reading is a claim about six plans」。`git grep -rn '12 目标'` 只命中 `pending-work.cn.md` 自己——那 12 个目标从没进过任何规则文件。 |
| 16 | 基线 174 → 54，且 12 目标里「新红 = 0」（2026-11-16，`0f32ded47`） | `superseded` | 这是一次 dated 读数，后来被换过两次并重记一次：`61(12目标)`（§25，本节自己已标「作废」）、`57(12目标)`（§33，只写作「登记后应为 57、未复测」）、`56(12目标)`（§36 尾部的逐条实测分区）；12 目标名单本身也被第 15 行的六目标名单取代。本节里的「两条不稳定用例」已被 §18 判成负载敏感（不是新红）。`0f32ded47` 仍是这次基线的树名（`git log -1 --format='%h %s' 0f32ded47`）。 |
| 17 | 一条保留的空合并（2026-11-16，`42bbc4ba1`） | `done` | 三条读数逐条复算过：`git rev-parse 42bbc4ba1^{tree}` 与 `git rev-parse 64f281e31^{tree}` 同为 `cf18ed5b28ef7dd945ca99a996c4e358b4b27b72`；`git merge-base --is-ancestor 2de547387 64f281e31` → exit 0（合并了自己的祖先）。保留、不重写，与 `agents/branching.md`「Never rewrite a pushed commit on `develop`」一致。 |
| 18 | 「不稳定用例」是我判错了：判定稳定，越线的是成本（2026-11-16） | `done` | 治因落了：`b3ef76bff`「merge: two test facilities make two and four starts instead of nine and six」（9/6 次嵌套启动各并成 2/4）；「不提高 timeout」这一半的口径与 `test/tools/run-tests.mjs` 的 `timeoutMarginVerdict`（`:579`）一致；根目录残留 14 个未跟踪产物由 `5c8041a70`「test: a run that leaves a path in the checkout fails, and says which test」+ `agents/test.md` 收住。本条「`lsp.navigation` 稳定红 ⇒ 54→55」那半句已被 §28 撤回。 |
| 19 | 我删掉了一棵正在被使用的工作树（2026-11-16，08:04） | `done` | 闸门进了工作规则：`agents/branching.md` 的「A Lane's Checkout Belongs To The Lane」——「Remove a checkout only when the lane has reported and stopped, or when its tree is clean and no process holds it」，并把 `Permission denied` 读成「有人在用」；测量用临时树不碰那一条在「Closing and preserving branches」（「Do not delete probes… or a branch still used by another worktree」）。 |
| 20 | 「两栏相等」可能是「两栏都被同一批未跟踪文件污染」 | `unverified` | 读懂的是：`git stash`（不带 `-u`）留下未跟踪的 `.cml`，「基线」那一栏因此也红，而 `git diff` 看不见未跟踪文件，于是两栏「相等」把错误掩盖住；`lane-ifarm` 更正后的真基线是 **46 红**。缺的是：这条判据没有树内载体——`agents/` 里只有它的**防因**（`agents/branching.md`「Never `git stash`」），没有「两栏必须用同一份文件集才能算证据」这一句；也没有任何文件记着那次 46 红的逐条名单，所以这条只能靠再跑一次才复核得了。 |
| 21 | 三个「基线」数字不是同一种东西，**不许相减** | `done` | 规矩有载体：`agents/test.md`「Two numbers printed side by side must carry their units, or someone will add them.」与「A reading without a pinned base…」。本节举的 `55(12目标)` 这个数已被 §28 撤回（`54` 不变），这正是本节要防的那类混用。 |
| 22 | 一条诊断的判据与它的文本恰好反相关（`UnusedAssignedValue`） | `done` | 判据改了：`3ee093bf2`「Report an unread value only when no read takes what the write stores」（`git merge-base --is-ancestor` → 0）。同一条改写留在 `test/plans/feat/linear/var_read_before_write.plan.toml` 的 `desc`：「the diagnostic asks the question its own text asks (whether any read takes the value this write stores) instead of whether the write's own node has a consumer」。 |
| 23 | 多笔交付不许 squash（2026-11-16，`ifarm` 的代价） | `done` | 规矩有两个载体：`CONTRIBUTING.md`（「a reviewed PR is merged into `develop` with a merge commit (`git merge --no-ff`), not squashed or rebased」）与 `docs/CONTRIBUTING.cn.md`。已 squash 的那一笔按本节要求写全了原笔 sha、顺序与依赖：`git log -1 --format=%B d5bb220e7` 正文列 `20888fd75` / `4268881f8` / `dccce463c` 并写明「② must never come before ③」。 |
| 24 | 新增 `.cml` 必须登记 AST 快照 —— 一条今晚踩了两次的坑 | `done` | 规矩进了工作规则：`agents/test.md` 第 81 行（「Any edit to a `.cml` file under `test/cases/` changes its recorded AST hash」+ 按文件 `check.py snapshot --update --only '<pattern>'`、合并前 `-Check ast-hashes`），工具在 `test/tools/parsecheck/check.py`（`snapshot [--update] [--only GLOB …]`）。 |
| 25 | 基线：`61(12目标)` 在 `9997c789a` | `superseded` | 本节自己写「这一串是陈旧的、加不起来的数字，已作废」，并已被 §36 尾部的 `56(12目标)` 实测分区取代（那一段逐条按 `[PLAN]` 归属、求和 56）。`9997c789a` 仍在（`git log -1 --format='%h %s' 9997c789a`）。 |
| 26 | 「绿在没看」的又一处：通过的用例读不到余量 | `done` | 两半都做了。余量可读：`test/tools/run-tests.mjs` 的 `costRecord`（`:630`）记 `cpu_ms`/`cpu_limit_ms`/`cpu_margin_x`/`cpu_margin_basis`。成本这一半是按本节的方向（降嵌套启动，不动线）做的：`5bf91ad36`「stop paying eleven runner starts to judge one pattern field」，今天 `git grep -c runPlans -- test/cases/tools/cli/stdout_matches.mjs` → 4 次调用点。线本身没有被抬高来盖住读数：`test/plans/feat/tools/cli.plan.toml` 里 `cli.plan_stdout_matches` 的 `timeout_ms = 180000`（线 45 s）仍是原文，改的是成本不是线——与 §18「不提高 `timeout_ms`」同一条纪律。 |
| 27 | 一条失败断言可以把其余检查一起吞掉 | `done` | 「先断言、后取下标」的形状已从 LSP 用例里拿掉：`git grep -n severity -- test/cases/tools/lsp/navigation.test.mjs` → `:45` 用 `warned?.severity`，`:208`/`:215` 改用 `.some(...)`，不再有 `diagnostics[0]` 取下标。 |
| 28 | 基线：`54` 没有变过，`lsp.navigation` 不是红 | `done` | 撤回的依据在树里：那条 `IgnoredSideEffect` 的实参/赋值分支随 `27b0c53a0`（合并 `8b541bdba`「feat: a write to a `var` is not an effect」）删掉，两者都在 develop（`git merge-base --is-ancestor` → 0）；`docs/cli.md` 同步改了，`test/cases/tools/lsp/navigation.test.mjs:45` 现在钉的是 `UnusedAssignedValue` 的码。 |
| 29 | 「推前自己先看一遍区间」这条规矩，第一次用就抓到一处 | `done` | 抓到的那一处是 `b3ef76bff`：它的标题不再是 `Merge remote-tracking branch …`，而是「two test facilities make two and four starts instead of nine and six」。规矩本身有载体：`agents/test.md`「Before committing on `develop`, print `git rev-list --count origin/develop..develop` and the composition of that range」+ `agents/branching.md`「A merge commit describes the integrated content, not merely the act of merging」。 |
| 30 | 两笔改同一个列表，但回答不同的问题 —— 都留 | `done` | 两边的判据都在 develop 且各有用例：`test/plans/feat/compile/refwrite.plan.toml`（`refwrite.shapes` / `refwrite.dead_arm`）、`test/plans/feat/compile/branch_exit.plan.toml` 的 `used_arm_exit`、`test/plans/feat/linear/loops.plan.toml` 的 `loops.outer_ref`；分支那个种子在 `d5bb220e7` 的正文（`branchJoinsNoValue` / `branchJoinOf`）。**本节写的标识符 `statementBranchRoots` 在树里查不到**（`git grep -n statementBranchRoots` exit 1）——落地后换了名字。 |
| 31 | 「未复测」是一个合法的答案 | `open` | 登记本身落了：`8866b029f`「fix(parse): record the two sources the handle-write batch added」（在 develop）。但本节认的那一格**仍是未复测**：下一份基线（§33）自己写「`57` 这个数记作「登记后应为 57、未复测」」，`git log --oneline --all --grep='57(12目标)'` 无命中——合并位欠的那次 12 目标重跑，账页里始终没有读数。 |
| 32 | 快照那条红不是粗心，是「车道只跑自己的计划，而快照读全仓库」 | `done` | 两处都落了：派工侧的规矩在 `agents/test.md` 第 81–82 行（新增/改动 `.cml` 按文件重录、合并前 `-Check ast-hashes`、整表重录会删掉缺席文件的行），合并侧的读法是 `internal/tools/camel-remote.ps1` 的 `-Check ast-hashes`。工具 `test/tools/parsecheck/check.py snapshot`。 |
| 33 | 基线：`57(12目标)`，但要写成「登记后应为 57、未复测」 | `superseded` | 那个「应该」后来被一次实测顶掉了：§36 尾部的分区是 **`56(12目标)`**（实测、逐条按 `[PLAN]` 归属、求和 56），不是 57。本节的两个事实仍可核：`lsp.navigation` 绿了（`test/cases/tools/lsp/navigation.test.mjs`）、`frontend.ast_snapshot` 的登记是 `56966b07b`（`git log -1 --format='%h %s' 56966b07b`）。 |
| 34 | 余量终于可读了：五个字段，一条判定没动 | `done` | 字段在代码里：`test/tools/run-tests.mjs` 的 `costRecord`（`:630`）产出 `cpu_ms` / `cpu_limit_ms` / `cpu_margin_x` / `cpu_margin_basis`，`:1907`–`:1909` 与 `:2697`–`:2699` 两处写入；判定仍是 `timeoutMarginVerdict`（`:579`）。「一条判定没动」这一点本表不复核（要跑计划）。 |
| 35 | 二手数字进了账页，就会被下一个人当读数用 | `done` | 那个数字被数过了，而且能对上：`5bf91ad36`「stop paying **eleven** runner starts to judge one pattern field」——11 次是本节的更正数，不是 15。更正已写进 §26 正文（「账页原写 15，是转述别的车道的话、我没数」）。 |
| 36 | 一条与任何改动无关的既有红：`check-doc-links --citations` | `done` | **这条已经不红了，与直觉相反但可当场复核**：`50bfb0191`「docs: cite Resolver::node for the effects plan's function-type row」（在 develop，`git merge-base --is-ancestor` → 0）把那行引证从 `resources.cpp` 的 `FunctionType` 改成 `resources.cpp` 的 `Resolver::node`，而 `Resolver::node` 在 `src/runtime/resources.cpp:415` 有定义。实测：`node scripts/check-doc-links.mjs --citations` → `no unverified citation: 1053 of 1053 symbol citation(s) verified, 2650 path citation(s) across 6765 file(s)`，exit 0。旧的串只留在 `internal/archive/artifacts/2026-11-15-stale-citation/fallback-census.txt:10` 与本页的引文里，不再在 `docs/` 正文中被引用。 |

## 附：3 个不编号的 `## ` 小节（不占上表的行）

| 小节标题（逐字） | 状态 | 证据 |
|---|---|---|
| 判据（给后来的人） | `unverified` | 读懂的是：写顺序结论时要把「不可能早于」与「一定晚不了」分开写（必要性 vs 充分性）。缺的是：这只是给读者的写作规则，树内没有载体、也没有任何命令能判定它被遵守了；它和第 2 行是同一个问题的两面。 |
| 工具事实：`-Plan` 与 `-Changed` 同时给时，`-Plan` 被忽略 | `unverified` | 读懂的是：两者同给时 `-Plan` 不生效、`--changed` 自己选的集合生效（两次误读读数）。缺的是：要复核它必须**跑一次带 `-Changed` 的 gate**并对照 `[PLAN]` 名单，本车道不跑计划；`internal/tools/camel-remote.ps1` 里也没有一条能读出「`-Plan` 被忽略」的断言。 |
| 新发现、未定责：`debugger.dap_*` | `open` | 本节自己写「先记不修，下一轮轮转再看」，且**没有主人**：`git grep -rln 'dap_stepping' -- docs` 只命中账页与 `docs/technical/35_debugger.md`（后者是调试器文档，不是定责页），`git log --since=2026-11-16 -- test/plans/feat/tools/debugger` 为空——下一次轮转也还没发生。 |

## 统计

| 状态 | 条数 |
|---|---|
| `done` | 23 |
| `open` | 5 |
| `superseded` | 5 |
| `needs-author` | 2 |
| `unverified` | 1 |
| **合计** | **36** |

`open` 的五条是第 5、6、8、14、31 节；`superseded` 的五条是第 11、15、16、25、33 节；`needs-author` 的两条是第 2、3 节；`unverified` 的是第 20 节。

`done` 的每一条都附了证据，`unverified` 的每一条都写了缺什么：编号小节里 `unverified` 只有第 20 节一条（缺的是判据的树内载体与那次 46 红的可复算记录），表后三个不编号小节里另有 2 条，各自的缺处写在那一行里。

这张表不改 `pending-work.cn.md`：那是决策者的账，本表只是它的状态列。`internal/` 下没有列这些文件的 README 索引（`README.cn.md` 是接手说明，不是文件清单），所以没有索引可更新；`questions/` 的索引是另一个生成器的范围。

## 附：一次有记录的例外（合并主题只写了动作，没写内容）

**`743186814` 的合并主题只写了动作（`merge origin/develop into lane-nn-restore`），正文空 —— 合进来的是 `ba4852022`（`ledger: three residents recorded and one spelling taken out, so never_rises is green again`）+ 它的合并 `d69da02be`；`lane-nn-restore` 当时漏写，按 `AGENTS.md`「合并提交必须概述被合进来的内容」应写那句。已推的 `develop` 不重写，所以这一笔算「有记录的例外」，不算静默违规。**

## 附：两条被取代的分支，以及凭什么说它们被取代

两条分支都量过、判定「内容被取代」，于是从本地删掉。删的顺序是**先打 `archive/*` 标签并推成功，再删分支**——`git branch -d` 会因 `ahead>0` 被拒，所以只能用 `-D`，而 `-D` 唯一的前提是标签已经在远端。理由照抄合并车道量出来的那两句，没重查。**一条删成了、一条被拒，结果写在下面，删不掉的那条没有绕过。**

**① `lane-win-s1-6`（ahead 2，behind 99，tip `bfd086d6b`）**

净效果只有删 `test/plans/feat/compile/_s16_probe.plan.toml`；`e7e006d1e` 加的 `_s16_determinism` 又被 `bfd086d6b` 自己删掉
⇒ **develop 上该文件已由 `1a0b8005c` 删掉**；`git merge-tree` 的结果树对 develop 的 diff **为空**；`git grep -rn '_s16_probe\|s16_probe' -- test docs scripts` **exit 1**
⇒ **⇒ 所以没有内容可取。**

**② `lane-win-s1-6-diag`（ahead 1，behind 87，tip `ea4b422e3`）**

`ea4b422e3` 的内容**已在 `48156fcaa`**；`git diff origin/develop lane-win-s1-6-diag -- src/compile/gir/captures.cpp` 只剩 develop 多出的 `enclosesOrigin()`，`builder.h` 差异全是 develop 侧更新
⇒ **`git cherry` 报 `+` 只因补丁带旧上下文，不是内容缺失**
⇒ **⇒ 所以内容已在。**

四条 sha 的关系是可以读出来的，不是判断：`1a0b8005c`（删 `_s16_probe.plan.toml`）与 `48156fcaa`（已带着 `ea4b422e3` 的内容）都在 `origin/develop` 上（`git merge-base --is-ancestor` → 0）；`e7e006d1e`、`bfd086d6b`、`ea4b422e3` 都不在（→ 1），只存在于这两条分支里——所以下面两个标签是它们唯一的落点。

删之前先打标签归档（**标签推成功之后才许 `-D`**）：

```powershell
git -C D:\Projects\Camel tag -a archive/lane-win-s1-6 -m "superseded by 1a0b8005c: the probe plan it removes is already gone" lane-win-s1-6
git -C D:\Projects\Camel tag -a archive/lane-win-s1-6-diag -m "content already in 48156fcaa" lane-win-s1-6-diag
git -C D:\Projects\Camel push origin archive/lane-win-s1-6 archive/lane-win-s1-6-diag
git -C D:\Projects\Camel branch -D lane-win-s1-6 lane-win-s1-6-diag
```

**这一轮的结果（`origin/develop` = `2cb6ab8`）**：两个标签都推上去了，远端读数是 `refs/tags/archive/lane-win-s1-6` = `7854d3599`（peeled `bfd086d6b`）、`refs/tags/archive/lane-win-s1-6-diag` = `76da6c35f`（peeled `ea4b422e3`）。于是：

- `lane-win-s1-6-diag` 删掉了，`git branch -D` 回 `Deleted branch lane-win-s1-6-diag (was ea4b422e3).`
- `lane-win-s1-6` **被拒**，原样回 `error: cannot delete branch 'lane-win-s1-6' used by worktree at 'D:/Projects/Camel-wt/wins1'`。它没被绕过：工作树 `D:\Projects\Camel-wt\wins1` 还停在 `bfd086d6b`，而按 SKILL §5.7，删树要先过三步——`ahead == 0`、`status --porcelain` 为空、**该车道当前不在 `list_agents` 里 `running`**——第三步子代理看不见（只看得见自己的子代理），所以「先删树再删分支」这一半留给协调者点名。标签已把 `bfd086d6b` 钉在远端，删树时不会有「只此一份」的风险。

**同一刻的台账**：删之前 `U=9 maxBehind=99`，删之后 `U=8 maxBehind=99`。所以「删掉这两条就能把 `maxBehind` 从 99 压到 23」这个判据**不成立**，两处都有读数：`lane-win-s1-6`（behind 99）因为工作树占用还在账上；即使它删掉，红线里的 `lane-unknown-resource`（ahead 7）本身就是 behind 97。要压到 23 只能处理 `lane-unknown-resource`，而它这一轮明确不许删。