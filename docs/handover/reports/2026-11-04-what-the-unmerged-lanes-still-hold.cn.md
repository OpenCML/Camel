# 未合进 develop 的 lane 还剩什么：57 条分支逐条判定

日期 2026-11-04 · 车道 `lane-backlog` · **只分类，不合并、不删除、不改源码**

范围：`origin/develop` = `34b659107`。78 条 `lane-*` 的 tip 不是它的祖先；其中 21 条是今晚在跑的车道，排除后**逐条判 57 条**。（用户点名排除的 25 条里，`lane-axistated` / `lane-withport` / `lane-withtan` / `lane-statetypes` 其实**已经合进** develop——它们的 tip 就是 `34b659107`；剩下 21 条按"在跑/待合"处理，不下可删结论。）

## 判据（怎么判的）

1. `git log --oneline origin/develop..<b>` —— 它自己的提交。**不用本地 `develop`**（落后远端 24 个提交）。
2. `git diff --shortstat origin/develop...<b>` —— 三点净差异（从 merge-base 起）。
3. `git cherry origin/develop <b>` —— patch-id 逐笔比对：`-` 表示**内容相同的一笔已在 develop 上**。标题相同不算证据。
4. 对每条自己的 `-U0` hunk，把**连续的改动块**拿到 develop 同名文件的同一位置找连续匹配：整块在 = 已进。单行匹配会因文件里重复的代码行（如 `for (size_t i = 0; ...)`）误判，所以按块不按行。
5. 分支新增的文件：同名文件在 develop 上是否存在、是否逐字节相同；**merge-base 时有、develop 上没有 = develop 删掉或挪走了**。
6. 新增用例：`test/cases/` 下 merge-base 没有、分支有的文件。
7. 分类：**已被取代** = 内容已在 develop（同名文件相同、或同一能力以另一种写法在）；**部分已被取代** = 有一笔/一个文件确凿进了，其余没进；**已过期** = 它要改的东西今天不在 develop 上了（被挪走、被重构掉）；**仍然独立** = 以上都不是，且对象仍在 develop 上。

引用写 `file#Symbol`，不写行号。

## 表（57 条）

| 分支 | 自己的提交 | 净差异行 | 判定 | 依据 |
|---|---|---|---|---|
| lane-aspath | 1 | 98 | 仍然独立 | 加 `stdlib/rulesets/jvp.cml#jvp_sum_axis`（沿轴的 sum 的前向律）与它到不了的转置律；`jvp_sum_axis` 在 develop 上 0 处。**有测试**（`test/cases/modules/autodiff/rules_angle_args_sum_axis_jvp.cml`） |
| lane-b2 | 59 | 1707 | 部分已被取代 | 59 笔里 2 笔 patch-id 已进（`agents/refactor.md` 默认实参规则、capture-gap 页），57 笔未进；39 个文件里 10 个已在 develop 上逐字节相同。**有测试**（2 个新用例） |
| lane-b2triage | 1 | 75 | 仍然独立 | 一页账：`docs/handover/reports/2026-11-04-what-of-lane-b2-is-still-missing-from-our-chain.cn.md` 不在 develop 上，内容是 b2 那 9 笔还差什么 |
| lane-bisect | 10 | 725 | 部分已被取代 | 3 笔已进（三页 questions），7 笔代码/用例未进；`modules/tiles/include/tiles/internal/regions.h`、`modules/tiles/src/graph_regions.cpp` 在 develop 上另有写法。**有测试**（1 个新用例） |
| lane-bisect-script | 3 | 24 | 部分已被取代 | 中间那笔（sentinel 不分配）已进；`include/camel/core/context/frame.h` 的 sentinel 改造与 `agents/refactor.md` 那条规则未进 |
| lane-c3-merged | 3 | 264 | 部分已被取代 | 它的目标页 `docs/handover/questions/...lifted-list-is-grouped...` 已被挪进 `docs/handover/reviews/`；`modules/transforms/split.cpp` 的 `(values…, tangents…)` 读法未进 |
| lane-casemig | 2 | 126 | 仍然独立 | 两张迁移表（被收回的 `transpose(f)(x, ȳ)` 今天还教在哪儿）；旧写法在 develop 的 `docs/handover/` 下仍有 3 处 |
| lane-dcemayfault | 10 | 116 | 仍然独立 | `agents/test.md` 两条规矩（引用只写符号、探针要证两次）与 graph-store 的读法；两条都不在 develop 上 |
| lane-dedup | 3 | 81 | 仍然独立 | 一个值只带一条残余（`modules/transforms/split.cpp`）+ 无用类型参数只报一次；develop 上两条都没有 |
| lane-dedupe | 2 | 1610 | 部分已被取代 | 四处 `size()`→`numel()` 走查里，`src/runtime/graph.cpp#collectStaticGraphRefsRecursive` 与 `src/passes/trans/json/gir_canonical.cpp` 今天仍是 `array->size()`；`docs/handover/pitfalls.cn.md` 那一行没进 |
| lane-defun4 | 2 | 54 | 已过期 | 它要删的 `catch (const std::exception` 在 `src/passes/opt/defun/defunctionalize.cpp` 上**已经不在**，同一处改成了 `raisePassWarning` |
| lane-docsync | 1 | 3 | 已被取代 | `docs/reference/camel-for-ai-stdlib-signatures.md#may_be_rearranged` 那行今天就在 develop 上（该文件是生成物，合的时候重录了） |
| lane-edges | 20 | 985 | 部分已被取代 | 1 笔已进（gir 的 version edge 不算语句）；版本边的图、svg 与 `modules/onnx/exporter.cpp` 的改法未进 |
| lane-edges-green | 20 | 844 | 部分已被取代 | 同上一条的"绿"版：同一笔已进，其余未进；两棵树是同一件事的两次尝试，**不要一起合** |
| lane-eff-rule | 12 | 1061 | 部分已被取代 | 1 笔已进（这车道为什么不能单独交付那页）；tiles 区域资格（`modules/tiles/src/graph_regions.cpp`）未进 |
| lane-exittype | 3 | 589 | 仍然独立 | 新增 `include/camel/runtime/graph_exit_type.h` + `ExitTypeMismatch` 诊断：图交出的值按可赋值性判。develop 上 `diagnostics/registry.tsv` 无此码。**有测试**（4 个新用例 + `feat/runtime/exit_type` 计划） |
| lane-extentfold | 7 | 628 | 仍然独立 | `extent_of::<A>()` 在类型实参在手时折掉 + `ExtentOfNeedsStatedShape` 诊断。develop 上没有。**有测试**（3 个新用例） |
| lane-fold2 | 13 | 857 | 部分已被取代 | 1 笔已进（GATE 抹除处的审计注释）；`src/runtime/graph_edges.cpp` 那条 sync 边的检查未进，8 个报告的页不在 develop 上 |
| lane-garbage | 3 | 153 | 仍然独立 | 新增 `test::edges_fill` pass 校 FILL 的成员与声明形状；`EdgesFillTestPass` 在 develop 上 0 处，且明说"这不是那两个程序的修复" |
| lane-gaterepo | 1 | 1582 | 部分已被取代 | 闸门入口**已进**：`internal/tools/camel-remote.ps1` + `internal/tools/README.md` 在 develop 上（`d96cbfc68` 落、`46a0db2ee` 改写）；只差 `internal/tools/camel-remote.local.ps1.example` |
| lane-gcfreed | 11 | 684 | 仍然独立 | 预留程序的根表落地代价：`src/core/mm/poison.cpp`、`src/runtime/program.cpp` 的收窄 + 一页问题；develop 上未落 |
| lane-gpu | 7 | 172 | 仍然独立 | tiles 的标量类型在计算前先命名窄浮点；`modules/tiles` 侧未进 |
| lane-gs6 | 11 | 744 | 仍然独立 | `EffectLivenessFacts` 的 7 个装配点先贴原文再画矩阵 + `:op/set_index` 没有任何写法能到达（那页 `docs/handover/questions/2026-11-02-can-any-spelling-reach-set-index.cn.md` 不在 develop 上） |
| lane-jvpwith | 3 | 663 | 仍然独立 | jvp 按名字拒绝 `with` 的几种形状（callee 的 lift 带不了）。**有测试**（1 个新用例） |
| lane-knowlen | 15 | 361 | 仍然独立 | 转置律怎么拿到"它不可读的长度"：`stdlib/tensor.cml` 与 `modules/tensor/ops/layout_ops.cpp` 的读法 + 一页问题 |
| lane-lamwith | 2 | 53 | 部分已被取代 | 它改的那页（lambda 以 `with` 调用收尾，split 剪什么）已被挪进 `docs/handover/reviews/`；新的那张"操作数前向值从哪儿来"整页未进 |
| lane-ledger2 | 2 | 78 | 部分已被取代 | 页本体已在 `docs/handover/reviews/2026-11-02-who-updates-the-root-table-when-the-root-is-replaced.cn.md`（差一行）；`agents/refactor.md` 那 13 行未进 |
| lane-libgrad | 3 | 251 | 仍然独立 | 库侧的梯度判据（`mean_axis` 负轴由库拒绝等）。**有测试**（2 个新用例） |
| lane-lit2 | 14 | 635 | 仍然独立 | 自动微分迁移的文献与读数（哪三条路关掉了）。**有测试**（1 个新用例） |
| lane-loopfix | 4 | 815 | 仍然独立 | "把自己的状态带出自身"的循环转置怎么切 + 一道墙的一页问题；`modules/transforms/*` 未进 |
| lane-m4b | 4 | 277 | 仍然独立 | sync 边按它必须满足的谓词校验（`include/camel/runtime/draft.h` 105 行）+ 两页问题 |
| lane-m4b2 | 33 | 1143 | 仍然独立 | 状态是数组的循环被 lift，其余聚合状态不被（jvp/split）。**有测试**（1 个新用例） |
| lane-m4census | 1 | 114 | 仍然独立 | 一页普查：M4 的自动微分用例按库路/引擎路/旧 ABI 三栏分 |
| lane-m5 | 11 | 359 | 仍然独立 | 入口调用的深度计数收成一条谓词、printer 的两个字面量两个名字、轴归约在角括号里写轴 |
| lane-marks | 12 | 425 | 部分已被取代 | 4 笔已进（宏改写问链、inline 拼接、runner 自检的界、catch-all 故障文案）；chain mark 的其余读写未进。**有测试**（1 个新用例） |
| lane-meanax2 | 2 | 41 | 仍然独立 | 一页：`mean_axis` 的转置等的是两个上游，不是一条律能改的 |
| lane-mult | 0（只有 merge 提交） | 282 | 已被取代 | `acbabaf85` 的两个父（`461114c82`、`6c5fa05e`）**都在 develop 里**；它带的两条用例 `test/cases/compile/transforms/transpose_array_product.cml` 在 develop 上逐字节相同（`fd964b750`） |
| lane-namenature | 6 | 111 | 仍然独立 | 生成器拒绝清单从 2 条到 0 条 + `docs/language/07_statements.md` 的读法；8 个 hunk 里 1 个已一致，其余未进 |
| lane-nepeephole | 11 | 772 | 仍然独立 | 窥孔设计：字节序、键、调用方 pass 先写再落代码。**有测试**（2 个新用例） |
| lane-p02-gate-from-value | 1 | 60 | 仍然独立 | 一页：P0-2 链位的两种形状，块值那条在文法上不存在 |
| lane-p02a | 9 | 269 | 仍然独立 | gir 里 sync 边只有一个写点（八个调用方各留原样）+ 一页验收条件 |
| lane-p02v | 1 | 105 | 仍然独立 | 一页：P0-2 的第二步站着的三个前提，逐条核过 |
| lane-rank1 | 1 | 93 | 仍然独立 | 一页：`T[]` 的规则要不要覆盖 `T[..]` 的实参（秩一/open-rank 的键问题） |
| lane-reinterp | 1 | 40 | 仍然独立 | 一页缺陷：从 `prelude` 引入名字会让 `prelude` 自己的 `#op` 在无实现模块的上下文里求值 |
| lane-remat | 27 | 767 | 仍然独立 | remat 一族：`hasWaitEdge` 不是它旁边那些事实之一、split 存什么。**有测试**（5 个新用例） |
| lane-reverse | 1 | 191 | 已被取代 | `modules/transforms/transpose.cpp` 上"rounds 从最后一轮到第一轮撤"那段**今天就在 develop 上**（只有调试打印与 known_fail 注未跟） |
| lane-rpath | 2 | 88 | 仍然独立 | GLIBCXX 坑与新安装的 rpath/`libs/` 记录（`docs/handover/pitfalls.cn.md`、`scripts/`） |
| lane-rpathdoc | 1 | 2 | 仍然独立 | 只改 pitfalls 里那一行，把根修（`libs/libstdc++.so.6` + `$ORIGIN`）写进去；develop 那行仍是旧版（两处标记 0 命中） |
| lane-sigmoid2 | 3 | 240 | 部分已被取代 | 文档那笔已进；`tanh_gradient_through_the_library.cml` 与 develop 逐字节相同；`stdlib/rulesets/jvp.cml` 的写法未进——develop 由 `b93074169` 用另一种写法（由输出取斜率）覆盖同一能力 |
| lane-sigtable | 4 | 118 | 已过期 | 它续写的 `docs/handover/questions/2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md` 已被作者挪进 `docs/handover/reviews/`，questions/ 里那份不在了 |
| lane-slopes-wip | 3 | 176 | 仍然独立 | 三个斜率钉在整数组上，并按中心差分与引擎自己的前向对过。**有测试**（2 个新用例） |
| lane-splitproj | 2 | 69 | 部分已被取代 | 文档那笔已进；`modules/transforms/split.cpp`"循环带上它后半段读到的每个参数的值"未进 |
| lane-stdlibref | 1 | 18 | 已被取代 | `docs/stdlib-reference.md#may_be_rearranged` 整段（含 `graph::may_be_rearranged` 的实现指向）今天逐字在 develop 上 |
| lane-typelaw | 1 | 60 | 已被取代 | develop 的 `docs/stdlib-reference.md#transpose` 与 `stdlib/signatures/transforms.cml#transpose` 已是 `transpose(f)(1.0)` 那一版；旧写法在 develop 上 0 处 |
| lane-verify | 12 | 636 | 仍然独立 | 一个否定结果的车道：pruneBranch 的交接扩展并不修那件事 + 两个 reduced 用例。**有测试**（2 个新用例） |
| lane-writers | 22 | 1416 | 仍然独立 | 写入者集合：哪些节点会写、代价与收窄；45 个文件里 9 个已在 develop 上。**有测试**（4 个新用例） |
| lane-zeroplan | 4 | 211 | 已被取代 | `scripts/plan-dir-markers.mjs`、`test/plans/feat/*/README.md`、`scripts/release-gate.mjs` 与 develop **逐字节相同**（`7374711ad` 合进来的）；`test/tools/run-tests.mjs` 那一半 develop 已另写 |

## 汇总

| 判定 | 条数 |
|---|---|
| 已被取代 | 6 |
| 部分已被取代 | 15 |
| 仍然独立 | 34 |
| 已过期 | 2 |
| **合计** | **57** |

## 建议动作（只建议，不执行）

**A. 可以删（已被取代 + 已过期，8 条）**：`lane-docsync`、`lane-stdlibref`、`lane-typelaw`、`lane-zeroplan`、`lane-mult`、`lane-reverse`、`lane-defun4`、`lane-sigtable`。
理由：前四条的内容在 develop 上逐字/等价存在；`lane-mult` 两个父都在 develop 里；`lane-reverse` 的走法已在 `modules/transforms/transpose.cpp`；`lane-defun4` 与 `lane-sigtable` 要改的东西已不在。**删前仍按 lane-sweep 的四条件复查一次**（本次只判，未删）。

**B. 该合，且有测试（12 条）**：`lane-aspath`、`lane-exittype`、`lane-extentfold`、`lane-jvpwith`、`lane-libgrad`、`lane-lit2`、`lane-m4b2`、`lane-nepeephole`、`lane-remat`、`lane-slopes-wip`、`lane-verify`、`lane-writers`。
理由：各带新用例（1–5 个），且内容不在 develop 上——这是"看不见的债"里最值得先搬的一半。

**C. 该合，但要人先看一眼（22 条，仍然独立但无新用例）**：`lane-b2triage`、`lane-casemig`、`lane-dcemayfault`、`lane-dedup`、`lane-garbage`、`lane-gcfreed`、`lane-gpu`、`lane-gs6`、`lane-knowlen`、`lane-loopfix`、`lane-m4b`、`lane-m4census`、`lane-m5`、`lane-meanax2`、`lane-namenature`、`lane-p02-gate-from-value`、`lane-p02a`、`lane-p02v`、`lane-rank1`、`lane-reinterp`、`lane-rpath`、`lane-rpathdoc`。
理由：没有新用例的"仍然独立"最容易烂——其中 10 条是纯文档/问题页（`b2triage`、`casemig`、`dcemayfault`、`m4census`、`meanax2`、`namenature`、`p02-gate-from-value`、`p02v`、`rank1`、`reinterp`、`rpathdoc`），搬的时候只搬结论、不搬叙述（§1 报告长度的教训）；另 10 条是代码（`dedup`、`garbage`、`gcfreed`、`gpu`、`gs6`、`knowlen`、`loopfix`、`m4b`、`m5`、`p02a`、`rpath`），合之前先补一条能红的用例。

**D. 该丢（部分已被取代里已经过时的那一半，2 条）**：`lane-edges` 与 `lane-edges-green` 是同一件事的两次尝试，两者各只有 1 笔进了 develop；建议**两条都丢、只留那笔已进的部分**，不再试图把整条搬过来。其余 13 条"部分"不要整条丢（`lane-b2`、`lane-bisect`、`lane-bisect-script`、`lane-c3-merged`、`lane-dedupe`、`lane-eff-rule`、`lane-fold2`、`lane-gaterepo`、`lane-lamwith`、`lane-ledger2`、`lane-marks`、`lane-sigmoid2`、`lane-splitproj`），按表里"未进"的那几笔拆开处理。

## 没做的

- 没 merge、没 rebase、没 push、没删分支、没删工作树、没 `git stash`（4 条来历不明的 stash 只报不动）。
- 没构建、没跑计划：这一轮没有代码改动。
- 21 条今晚在跑的车道（`lane-fwdclosure`、`lane-exitfix`、`lane-splitnext`、`lane-splitports`、`lane-tranche`、`lane-tranche2`、`lane-missinglaws`、`lane-hasaux`、`lane-vjparity`、`lane-b2port`、`lane-withgradexport`、`lane-rpathc`、`lane-skipdirs`、`lane-meanneg`、`lane-extentof`、`lane-f32trans`、`lane-secondorder`、`lane-multensor`、`lane-tuplecrash`、`lane-exitfix-probe`、`lane-closuredefect`）只排除、不判。
- `lane-b2` 的 57 笔未进没有逐笔列；它已经有一页 `docs/handover/reports/2026-11-04-what-of-lane-b2-is-still-missing-from-our-chain.cn.md`（在 `lane-b2triage` 上），拆它的时候从那里起。