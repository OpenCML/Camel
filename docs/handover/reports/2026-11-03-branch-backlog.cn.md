# 分支积压：43 条未合支逐条判档，以及"为什么会有这么多"

**读数树**：`D:\Projects\Camel`（主检出，`develop`）。**`origin/develop` = `f130ac445`**（14:27 那笔 push 之后）。
**量的时间**：14:29 首量 → 14:32–14:38 判档并删 → 14:39 收尾。**本轮没有任何构建**（本机不构建，也没有需要构建的改动）。
**结论**：主干外分支 **101 → 60**（删 44 条、期间新开 3 条）；三档 **a=5 ／ b=0 ／ c=38**；判档依据全部是 sha，没有一行凭分支名。

## 一、两个数（删除前后）

| 时刻 | `git branch` 总数 | 主干外 | 其中 `origin/develop..<b>`=0 | 其中未合 |
|---|---|---|---|---|
| 14:29 首量 | 104 | 101 | 59 | 42 |
| 14:38 删前 | 107 | 104 | 60 | 44 |
| 14:39 删后 | **63** | **60** | **20** | **40** |

- 删掉 **44** 条：40 条 `count=0`（内容全在 `origin/develop` 的历史里）+ 4 条判为 (a)。
- 期间新开 **3** 条（`release` 由 0 笔变 2 笔、`lane-rel42`、`p02-baseline`），所以 101 − 44 + 3 = 60。
- 剩下的 20 条 `count=0` 删不掉，唯一原因是 `D:\Projects\Camel-wt` 里还有工作树占着那个分支名（清单见 §四）。

## 二、43 行：档位、依据、一句话

`+` = `git cherry origin/develop <b>` 的未上游补丁数。**(a)** 内容已在上游（记落点 sha，删）；**(b)** 被裁定作废（记裁定 sha，删）；**(c)** 仍有价值（留，写一句它只做一件事）。

| # | 分支 | ahead | `+` | 档 | 依据 / 一句话 |
|---|---|---|---|---|---|
| 1 | `hold/fsh-dev` | 2 | 1 | c | 把 `origin/fsh-dev` 的尖端并进来，并重生成 `export_matrix` 的两份产物供作者那条线对照 |
| 2 | `lane-b2` | 60 | 57 | c | 把"转置成员需要的值"改成调用点填端口（with 端口／多轮探针／签名声明），并带 20+ 条转置用例 |
| 3 | `lane-bisect` | 12 | 7 | c | 给每条有副作用或可能故障的语句写一条 sync 链边，并量"链式重录"的判据 |
| 4 | `lane-bisect-script` | 4 | 2 | c | 让帧池的哨兵是一个真的 `Frame`，`top_` 处永远有活对象 |
| 5 | `lane-c3-merged` | 1 | 1 | c | 记 C3 的六层清单：类型对得上、后半一 build 就 fault |
| 6 | `lane-casemig` | 3 | 2 | c | 把 `transpose(f)(value…, ct)` 的用例改写成今天的形状，并记旧写法还教在哪里 |
| 7 | `lane-ci-macos2` | 4 | 4 | c | 把 macOS arm64 腿并进平台矩阵，并把"没有代码生成器的 `std::jit`"降成一条警告 |
| 8 | `lane-dcemayfault` | 17 | 10 | c | graph-store 的第十六切与两个测试读数（纯文档），逐次把 develop 并进来 |
| 9 | `lane-defun4` | 4 | 2 | c | 让 dispatch 的 join 只按它需要的东西排序（纯区域先行） |
| 10 | `lane-edges` | 21 | 19 | c | 给 GIR 加"写后带版本"的边来替控制边的替身（printer／C++ emitter／ONNX 一起学） |
| 11 | `lane-edges-green` | 21 | 19 | c | 在 `lane-edges` 上再退掉"写变量保留版本"那一笔，试一条绿的组合 |
| 12 | `lane-eff-rule` | 15 | 11 | c | 为每条有副作用的语句写 sync 链边，并记这条支为什么不能单独交付 |
| 13 | `lane-fold2` | 17 | 12 | c | 把 P0-2 的控制边按"它替哪三件事"分族量清，撤掉不能再用的判据 |
| 14 | `lane-gcfreed` | 20 | 11 | c | 让 poison 报告说得出 stale 指针来自哪个槽（program 的所有权） |
| 15 | `lane-gpu` | 8 | 7 | c | 让 tiles 的窄浮点元素类型在编译期被命名，并按宽度拒绝 |
| 16 | `lane-gs6` | 17 | 11 | c | 把 `EffectLivenessFacts` 的七个点与 closure-layout 检查收成一处定义 |
| 17 | `lane-ledger2` | 3 | 2 | c | 两条探针证明"根表从来不是那个洞" |
| 18 | `lane-lit2` | 20 | 14 | c | 把三个 helper 移到 `rule_expand`，让 vmap 取到规则实例的类型 |
| 19 | `lane-m4b` | 5 | 4 | c | 一次 splice 完一条链的位置（GATE 规则在内），并给 sync 边加"必须满足的谓词"检查 |
| 20 | `lane-m4b2` | 47 | 33 | c | 让链位置跟着"语句"走而不是跟着标记走（builder／opt／inline 三处） |
| 21 | `lane-m4census` | 1 | 1 | c | 把 M4 的 autodiff 用例按三列清点，指出 `buildGradientGraph` 哪个调用点没人接 |
| 22 | `lane-m5` | 17 | 11 | c | 记下"调用怎么写"：nn／tiles 的 with 参数与 axis 归约的两种拼写 |
| 23 | `lane-marks` | 12 | 8 | c | 让机器建不出的每条形状在自己的话里被拒，并给图的出口值加类型检查 |
| 24 | `lane-namenature` | 6 | 6 | c | 改正"typeof 是类型表达式"的判据，清掉生成器拒绝清单 |
| 25 | `lane-nepeephole` | 11 | 11 | c | 先把构造树的磁盘格式定死再写读写器，并把比较取反融成一条指令 |
| 26 | `lane-p02` | 4 | 4 | c | 把链位置做成位置量，并让诊断走它（P0-2 的 A 对） |
| 27 | `lane-p02-gate-from-value` | 1 | 1 | c | 记链位置的几种形状与"锚点不能缩"的理由 |
| 28 | `lane-reinterp` | 1 | 1 | c | 记 tag 移动真正撞到的缺陷：import 会重查 prelude |
| 29 | `lane-rel42` | 2 | 1 | c | 把 v0.2.42 装出来（neq 切分／打印机往返／里程碑层账）并写它的 changes 记录 |
| 30 | `lane-remat` | 40 | 27 | c | 让 `#remat` 成为标准标签，并让 split／jvp 尊重"要求它出现" |
| 31 | `lane-resid` | 4 | 4 | c | 把 timeout 的界裁定落成常量（余量规则），并记读数取在什么负载下 |
| 32 | `lane-splitproj` | 2 | 1 | c | 让循环把后半读到的每个参数值都带上 |
| 33 | `lane-typelaw` | 1 | 1 | c | 让 `Transpose<F>` 只声明余切（类型法），树里暂不移动 |
| 34 | `lane-verify` | 14 | 12 | c | 逐条排除"gate 走 replaceNode"这条路（负结果已收），把场景交出去 |
| 35 | `lane-writers` | 27 | 22 | c | 让变量的读带版本、写序靠版本而不是靠控制边（含 ONNX 导出器） |
| 36 | `m-train-merge` | 1 | 0 | **a** | 只含一次合并：`13426126f`（`lane-docexamples`），develop 已在 `f130ac445` 做过同一合并；`git merge-tree` 重算的树与它逐字节相同 ⇒ **未删**，`Camel-wt/m-train` 占着名字 |
| 37 | `lane-census2` | 2 | 0 | **a** | 两笔逐条等价于上游 `add32e1c8`（`tensor:zeros`）与 `6d535959c`（元素类型过桥），patch-id 相同 ⇒ **已删**（tip `f80a4cd7e`） |
| 38 | `lane-norm-arity` | 1 | 0 | **a** | `97fdb234f` 与 `810f58818` 的 patch-id 相同，后者经 `3ff14cbf8` 合入 develop ⇒ **已删** |
| 39 | `lane-vmapcap` | 2 | 0 | **a** | `30c090493` 与上游 `2d90b96f7` patch-id 相同 ⇒ **已删** |
| 40 | `probe-runner-matrix` | 2 | 0 | **a** | 两笔都是合并（父 `lane-s1` `8750a780e`／`lane-reinterp2` `a94a7f5ce` 全在上游），`git merge-tree` 重算的树相同 ⇒ **已删**（tip `6c42aaf68`） |
| 41 | `probe/bisect-forward` | 3 | 1 | c | 给 runner 矩阵造一条"不改变计划所读内容"的对照提交 |
| 42 | `probe/revert-dry` | 3 | 3 | c | 把 `lane-lit2` 的三笔 lift 改动整体 revert 一遍，量 revert 干不干净 |
| 43 | `probe/shuffle` | 1 | 1 | c | 与 41 同一笔对照提交的另一处落点 |

**(b) 本轮 0 条，这是查过的结论，不是没查**：`origin/fsh-dev` 上 6 条 `review:` 提交（`d4f105fda`、`c933c04a1`、`815a30dd0`、`3ab482ab2`、`9103fbf64`、`871efa59c`）与 `docs/handover/reviews/` 的裁定页逐条读过，没有一条点名"作废某条 lane"并留下可引用的 sha；按"判不准一律归 c"，它们都留在 c 档。
**开工后新增的一条**：`p02-baseline`（ahead=3，tip `4032f16b0`，是 `lane-p02` 前两笔的基线快照）不在这 43 行里，未参与判档，按规则默认归 c 保留。

## 三、为什么会有这么多（三条原因，都是今晚的实例）

1. **返工不回收**：换了做法以后旧支不删，于是同一件事长出两支。`lane-edges` 与 `lane-edges-green` 是同一套（后者多一笔 `Revert`）；`lane-writers` 已经把 `lane-edges` 并进去，`lane-edges` 自己还独立活着；`lane-reinterp2` 的 `count=0`（`e9ab3bdd9` 已经随它进 develop），同题的 `lane-reinterp` 还带着 1 笔未合。
2. **工作树就是保姆**：任务工作树绑死一个分支名，工作树不清，分支就不能删——本轮 20 条 `count=0` 全卡在这里，`lane-s2` 的工作树甚至停在 `f130ac445`（=`develop` 尖端本身）。`m-train-merge` 的内容已在上游，也只因 `Camel-wt/m-train` 存在而删不掉。
3. **分支当记事本**：读数、清单、快照都写在分支上，而不是提交信息或 `reports/` 页里。删掉的 44 条里，`m-fsh1–4` 与 `m-train117–129` 共 **17 条**全是"一两笔记录"；`lane-m4census`、`lane-c3-merged`、`lane-showcase` 各只差一笔纯文档。

## 四、没删的 20 条（`count=0`，但工作树占着分支名）

`lane-closurewk`(dirty 1)、`lane-docexamples`、`lane-dref`(dirty 1)、`lane-knownfail-audit`、`lane-loop-shapes`、`lane-m4b2-v2`、`lane-memberzero`(dirty 4)、`lane-narrow2`(dirty 2)、`lane-questions-answer`、`lane-questions-defaults`、`lane-questions-relay`、`lane-questions-withdraw2`、`lane-reverse`(dirty 1)、`lane-reverse-walk-pin`、`lane-rule-probe`、`lane-s2`、`lane-split-exit`、`lane-split-share`、`lane-tloop`、`lane-trans-array`（加 `m-train-merge`，判 a 但同样占着）。
收尾命令（**干净的那 14 条**可以直接走；dirty 的先看改动）：`git worktree remove --force D:\Projects\Camel-wt\<wt>` 之后 `git branch -d <b>`。**这一轮没有替任何人删工作树**：里面有 9 个未提交改动。

## 五、指针改写：文档里被改写的每一处（原名 → 新 sha）

规则：`` `lane-X @ <sha>` `` 保留原 sha、把分支名换成它；只有名字没有 sha 的，换成该支的 tip sha，并注明"原 `lane-X`，该分支已删"。**未删的支（`lane-tloop`、`lane-knownfail-audit`、`lane-m4b2-v2`、`m-train-merge`）与 7 条判 c 的未合支，名字一律不动**；`agents/test.md#the-instruments-range-is-narrower-than-the-fact` 的 `origin/lane-a3` 说的是远端 ref、不是本地分支，也不动。

| 文件:行 | 原名 | 新 sha |
|---|---|---|
| docs/design/effects/liveness-fills.cn.md:133 | lane-a3 | ea5d5de70 |
| docs/handover/m4-a-class-migration-order.cn.md:9 | lane-guard | 6af543d27 |
| docs/handover/m4-autodiff-migration-table.cn.md:169–172,178 | lane-reinterp2 | 67509bd1a（正文 `e9ab3bdd9` 保留，并改正为"现已进 develop `f130ac445`"） |
| docs/handover/m4-vjp-assembly.cn.md:3 | lane-m4-assembly | c2181c95f |
| docs/handover/questions/2026-11-03-a-norm-port-count-…:81 | lane-norm-arity | 97fdb234f（补"同一补丁以 810f58818 合入"） |
| docs/handover/questions/2026-11-03-reversing-a-backward-…:4 | lane-splitprimal | a7c41f15（tip e3cd90321） |
| …/reports/2026-11-02-the-known-fail-mark-that-did-nothing.cn.md:85 | lane-a3 | ea5d5de70 |
| …/reports/2026-11-03-a-half-of-a-two-state-loop-…cn.md:3 | lane-loopreverse | 737a7e896 |
| …/reports/2026-11-03-a-member-port-slot-nobody-fills.cn.md:3 | lane-splitprimal | 25880f2a8 |
| …/reports/2026-11-03-a-transposed-loop-graph-…cn.md:3 / :153 / :214 | lane-portgate / lane-splitprimal / lane-splitprimal | a1f648fb4 / cd46c06de / cd46c06de |
| …/reports/2026-11-03-does-a-partial-transposition-…cn.md:3 / :186 / :237 / :301 / :371 / :372 | lane-portgate / lane-norm-arity ×3 / lane-sortchain ×2 | a1f648fb4 / 97fdb234f ×3 / 810f58818 ×2 |
| …/reports/2026-11-03-p0-2-invariant-side-…cn.md:60 | lane-p02-invariant | 37bbd03d4 |
| …/reports/2026-11-03-the-forward-rule-…cn.md:93 | lane-loopreverse | 737a7e896 |
| …/reports/2026-11-03-who-holds-the-pair-…cn.md:3 | lane-loopreverse | 737a7e896 |
| …/reports/executable-judgements.md:30 | lane-narrow | 2ce226040 |
| …/reports/pending-sum-axis-rank-1.cn.md:65 | lane-guard | 6af543d27 |
| …/reports/tensor-buckets-2026-10-31.md:1 | lane-census2 | f80a4cd7e（并注明两笔落在 add32e1c8、6d535959c） |
| …/reports/transpose-two-wrong-cotangents.md:12 | lane-m4-assembly | c2181c95f |
| …/reviews/2026-11-02-is-with-grad-a-stub.cn.md:3 | lane-a3 | ea5d5de70 |
| …/reviews/2026-11-02-whether-the-release-gate-…md:30 | lane-a3 | ea5d5de70 |
| …/reviews/questions-2026-10-29.md:579 / :1609 / :1819 | lane-narrow / lane-guard / lane-a3 | 2ce226040 / 6af543d27 / ea5d5de70 |
| …/reviews/questions-2026-11-01-…array-parameter-…md:3 | lane-m4-assembly | c2181c95f |
| …/reviews/questions-2026-11-01-…computed-pack.md:3 | lane-m4-assembly | c2181c95f |
| …/reviews/questions-2026-11-01-…pullback-…md:3 | lane-m4-assembly | c2181c95f |
| …/reviews/questions-2026-11-02-…-every-name-or-the-first.md:3 | lane-m4-assembly | c2181c95f |

合计 **12 个分支名、25 个文件、33 处**（`git diff --numstat`：25 files, +38/−37，全部在 `docs/`）。

## 六、给下一个人：什么时候允许开新 `lane-*`，什么时候必须删旧的

**允许开**（两条都要满足）：① 这一支能被**一句话**说清"它只做一件事"，且预期 ≥1 笔**代码**提交；② 它要在主机上跑闸门，需要自己的工作树与 `build/`、`out/`。**内部先查重**：`git log --all --grep=<关键词>` 与 `git branch --contains <sha>` 找同题的活支；找到就并进那一支，或在第一笔提交里写"取代 `lane-X`（tip `<sha>`）"。
**必须删**（够一条就删，不许"再过一遍就删"）：① 已合进 `develop`（`origin/develop..<b>`=0）且工作树已 `remove` ⇒ 当场 `git branch -d`；② 作者裁定换了做法 ⇒ 在**裁定提交**里写下被作废支的 tip sha，同日删；③ 一支只剩读数／清单／报告 ⇒ 内容写进提交信息或 `reports/` 页，然后删（分支不是记事本）；④ 删了工作树却留着分支名 ⇒ 算清场未完成。
**一条元规则**：分支名不是证据。判一条支的生死，必须给出 sha——本轮 44 条删除各有自己的 sha，一条不例外。

## 七、本轮最该复活的一条（派工建议）

**`lane-rel42`**：唯一一条内容是"一个版本"的支（`e89dc5909` 装 v0.2.42 + `c168db836` 写 changes），只有 2 笔、没有工作树所以一直是搁置状态，且它**不在** `release` 里（`git merge-base --is-ancestor lane-rel42 release` 为假）；`release` 自己已经领先 develop 2 笔。其余 39 条都在 M4 这条主线上有工作树、活着。
风险单列一笔：`lane-b2` 领先 **60** 笔、`+` 57，早已越过"十笔规则"，按 `agents/branching.md` 只能**整支合并**，不能再按 diff 摘。