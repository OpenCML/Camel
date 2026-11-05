# 八十七条未合车道还剩什么：逐条判成 A/B/C/D

日期 2026-11-05 · 车道 `lane-backtriage` · 基线 `origin/develop` = `d14575b55`（本地 `develop` 同一点）
**只读一趟**：没合并、没 rebase、没删分支、没删工作树、没 push、没构建、没跑计划。这一页是唯一产出。

**为什么要这一页**：`ahead >= 1` 的本地分支 87 条（33 小时前 57 条），合并位一次只合一条、每条一次全量闸门约 11.3 分钟，
87 × 11.3 ≈ **16.4 小时纯闸门时间**。只要还按"逐条合"排，合并位就会把时间花在本来就该关掉的分支上。所以先判内容，再排合。

## 1. 判据：每条四个读数，标题与分支名都不算

| 读数 | 命令 | 读的是什么 |
|---|---|---|
| R1 | `git cherry origin/develop <br>` | `-` = 同一笔内容已在 develop（patch-id 比对）；`+` = 未进。**标题相同不算证据** |
| R2 | `git diff --stat origin/develop...<br>` | 它自己改了多少（三点 = 从 merge-base 起算），本文简写 `f=文件数/+增/−删` |
| R3 | `git rev-parse <br>:<f>` vs `origin/develop:<f>` | 逐个文件：同一份 / 不同 / develop 上根本没有 |
| R4 | `git log -1 --format=%s` | 它自己说它是什么 |

下文简写：`c+未进-已进` 是 R1，`f/a/b` 是 R2，`src` 是 R2 里落在 `src|modules|include|stdlib|tools|bindings` 的行数
（`scripts/`、`internal/`、`test/`、`docs/` 不计入 `src`，涉及时会单独点名）。

**最容易骗过自己的三处，本页因此才逐条列读数：**

1. **三点 diff 非零 ≠ 内容没进。** `lane-reverse` 读 `f3/+150−41`、`lane-typelaw` 读 `f3/+34−26`，两条的 R2 都"有改动"；
   而 develop 上 `modules/transforms/transpose.cpp#buildTransposeLoop` 那句"the rounds are undone from the last to the first"
   （`10e097354`，同一句 subject）与 `stdlib/signatures/transforms.cml#transpose` 的 `transpose(f)(1.0)`（`c6bcbdb70`）
   就是它们要加的东西。三点 diff 是从 merge-base 起的净差异，**develop 自己走远也会让它看起来像"有改动"**。
2. **分支名像"已完成"不等于 A。** `lane-exitfix-probe`、`lane-arrpat`（与 `parked-arrpat` 同源）、`lane-c3-merged` 三条
   名字都像"已经合过/已经是绿的"，读数分别落在 C、C、A。
3. **`git cherry` 只比对非合并提交。** 只有 merge 提交的分支（`lane-mult`、`lane-powzero`、`parked-shapeext`、`m-train-merge`）
   R1 是空的，判它们要用"两个父是不是都在 develop 里"。

## 2. 四类的条数

| 类 | 含义 | 条数 |
|---|---|---|
| **A 可交付** | 有实质提交，且只改文档/测试/小范围源码，没有已知会撞的路径 | **44** |
| **B 需返工** | 有实质提交，但面太大、或已知有红、或依赖还没落地的东西 | **17** |
| **C 该关掉** | 内容已在 develop、或已被后来的提交取代、或它要改的对象已不在树上 | **15** |
| **D 探针/保留** | 名字与提交写明 probe / test merge not for push / parked / hold | **11** |
| | 合计 | **87** |

D 里的 `hold/fsh-dev` 与 `release` **只列不判处置**（作者的钱 / 发布线，不是 lane）。

## 3. A 类：合并位的工作队列（44 条，按"最便宜先合"排）

排序规则：`src` 行数优先、其次文件数；两条改同一路径的已移出 A（见 B 类 `lane-rc00`、`lane-edges*`）。

| # | 分支 | 改了什么（一句） | 待办条目 | 读数 |
|---|---|---|---|---|
| 1 | `lane-rpathdoc` | `docs/handover/pitfalls.cn.md#GLIBCXX` 那一行改成 rpath 根修（`libs/libstdc++.so.6` + `$ORIGIN`）的读法 | 环境条目（GLIBCXX 坑） | `c+1` `f1/+1−1` `src0` |
| 2 | `lane-meanax2` | 一页问句：`mean_axis` 的转置等的是两个上游 | 待裁索引 | `c+2` `f1/+41` `src0` |
| 3 | `lane-arminv` | 一页问句：删掉一个拒绝就是把缺陷藏起来（今晚两个实例） | 待裁索引 | `c+1` `f2/+51−1` `src0` |
| 4 | `lane-gcsafe` | 一页问句：不在根里的图版本还保不保它的静态值 | 待裁索引 | `c+1` `f1/+53` `src0` |
| 5 | `lane-lamwith` | 两页：`with` 收尾的 lambda 被 `split` 切什么 + 转置成员的值从闭包来 | M2 / 待裁索引 | `c+2` `f2/+52−1` `src0` |
| 6 | `lane-p02-gate-from-value` | 一页：P0-2 链位的两种形状，块值那条在文法上不存在 | P0-2 账页 | `c+1` `f1/+60` `src0` |
| 7 | `lane-docdrift` | `AGENTS.md`、`agents/test.md`、`docs/npm-scripts.md` 三处读数对齐 | 文档漂移 | `c+2` `f3/+47−18` `src0` |
| 8 | `lane-fsh1-pre` | 一页：作者四条裁定落在哪，还差哪三件 | 账页 | `c+2` `f2/+40−3` `src0` |
| 9 | `lane-b2triage` | 一页：`lane-b2` 那九笔还差什么（拆 b2 的入口） | 账页 | `c+1` `f1/+75` `src0` |
| 10 | `lane-casemig` | 两页迁移表：被收回的 `transpose(f)(x, ȳ)` 今天还在哪教 | M3 | `c+2` `f2/+126` `src0` |
| 11 | `lane-namenature` | 生成器拒绝清单 2→0，并改 `docs/language/07_statements.md#generator` 的读法 | 语言文档 | `c+6` `f4/+104−7` `src0` |
| 12 | `lane-rank1` | 一页：`T[]` 的律要不要覆盖 `T[..]` 的实参 | 待裁索引 | `c+1` `f1/+93` `src0` |
| 13 | `lane-reinterp` | 一页缺陷：从 `prelude` 引名字会让它自己的 `#op` 在无实现模块的上下文里求值 | M1 / 待裁索引 | `c+1` `f1/+40` `src0` |
| 14 | `lane-rescue` | 一页：2026-10-25 flip 落地清单搬进树（逐字） | 账页 | `c+1` `f1/+63` `src0` |
| 15 | `lane-m4census` | 一页：M4 自动微分用例的库路/引擎路/旧 ABI 三栏普查 | M4 账页 | `c+1` `f1/+114` `src0` |
| 16 | `lane-p02v` | 一页：P0-2 第二步站着的三个前提，逐条核过 | P0-2 账页 | `c+1` `f1/+105` `src0` |
| 17 | `lane-backlog` | 一页：57 条未合车道的逐条判定（本页的前一版） | 账页 | `c+1` `f1/+109` `src0` |
| 18 | `lane-tiergap` | 两页问句 + `scripts/milestone-for-changes.mjs`：里程碑层谁跑、什么时候跑 | 闸门（里程碑层） | `c+2` `f4/+428−1` `src0`（脚本 428 行在 `scripts/`） |
| 19 | `lane-b10assert` | `feat/modules/resources.plan.toml` 与 `feat/linear/jit_framed.plan.toml` 的读法收紧（`resources` 逐行读、`jit_framed` 再读循环理由） | 测试读法 | `c+1` `f2/+100−38` `src0` |
| 20 | `lane-rc04` | 把 `adb67e081` 的五个 reduced 程序作为欠账放进闸门 | 闸门（opt 欠账） | `c+1` `f6/+195` `src0` |
| 21 | `lane-bisect-script` | `agents/refactor.md` 一条规则 + `include/camel/core/context/frame.h#sentinel` 不分配 | 工程规则 | `c+2−1` `f2/+21−3` `src15` |
| 22 | `lane-defun4` | defun 的 join 只在不可自由调度时才加那条 order 边（`nodeIsFreelySchedulable`） | S1 / P0-2 | `c+2` `f2/+52−2` `src10` |
| 23 | `lane-kconv` | 转换表与它重复的那份声明对齐，并加 `scripts/check-conversions.mjs` 防再分叉 | 闸门条目 | `c+1` `f5/+396−5` `src29` |
| 24 | `lane-nextext` | `extent_of` 对数组套数组答什么（两个新用例钉住） | M3/M4 extent_of | `c+1` `f7/+161−2` `src12` |
| 25 | `lane-errtype` | 模块错误类型：`errfmt`/`error_type` 两个用例 + `module.h` 的报法 | 诊断 | `c+2` `f7/+82−2` `src36` |
| 26 | `lane-sigmoid2` | 四条 sigmoid/tanh 梯度（引擎与库两条腿）+ pitfalls 一条 | M4 | `c+2−1` `f8/+238−2` `src43` |
| 27 | `lane-p02a` | gir 里 sync 边只有一个写点（八个调用方各留原样）+ 一页验收条件 | P0-2 | `c+9` `f6/+252−17` `src43` |
| 28 | `lane-dedup` | 一个值只带一条残余；无用类型参数只报一次 | M2 | `c+3` `f5/+46−35` `src57` |
| 29 | `lane-b5ext` | 删掉 `extent_of` 那个没有任何写法能到达的 kernel | M3/M4 extent_of | `c+1` `f5/+38−33` `src61` |
| 30 | `lane-splitproj` | `split` 的循环带上它后半段读到的每个参数的值 | M2 | `c+2−1` `f3/+70−3` `src67` |
| 31 | `lane-iffix` | 给 `let` 赋值报成赋值，而不是报成 `var` 实参 | 诊断 | `c+2` `f26/+147−47` `src54` |
| 32 | `lane-b3redo2` | 乘积律按 stated 列表回答开秩操作数 | M3 | `c+1` `f4/+144−65` `src75` |
| 33 | `lane-garbage` | `test::edges_fill` 校 FILL 的成员与声明形状（明说不是那两个程序的修复） | S1 / edges | `c+3` `f5/+150−3` `src120` |
| 34 | `lane-tuplem` | 结构体成员读的余切走同一条路径 + 成员自己那份零 | M2/M3 | `c+1` `f6/+259−10` `src127` |
| 35 | `lane-knowlen` | 转置律怎么拿到"它不可读的那个长度" | M3 | `c+15` `f4/+361` `src132` |
| 36 | `lane-fsh1` | 三页问句 + `split`/`program.h` 的采纳链（adopt_chain 用例已在 develop） | M2/M4 | `c+1` `f17/+890−78` `src141` |
| 37 | `lane-gpu` | tiles 的标量类型在算之前先命名窄浮点 | G4/G5 | `c+7` `f12/+148−24` `src150` |
| 38 | `lane-m5` | 入口调用的深度收成一条谓词、printer 两个字面量两个名字、轴归约在角括号里写轴 | M5 | `c+11` `f18/+314−45` `src187` |
| 39 | `lane-marks` | chain mark 的读写（12 笔里 4 笔已在 develop） | P0-2 | `c+8−4` `f20/+359−66` `src211` |
| 40 | `lane-c3-merged` | `split_two_params` 按提升后的顺序重写 | M2 | `c+3` `f6/+179−85` `src220` |
| 41 | `lane-withgrad` | `with_grad` 的库导出 + 除法律答不答分母（`src` 248 行，贴着 250 的线） | M4 | `c+8` `f16/+447−389` `src248` |
| 42 | `lane-milestone` | 里程碑层的入口与 `-Changed` 交接（改 `internal/tools/camel-remote.ps1`、`test/runner.mjs`） | 闸门（里程碑层） | `c+2` `f9/+572−10` `src0` |
| 43 | `lane-dcemayfault` | `agents/test.md` 两条规矩 + graph-store 的读法；代码一行没落 | 工程规则 | `c+10` `f6/+107−9` `src0` |
| 44 | `lane-fold2` | 11 个 P0-2 账页（8 新 / 3 改）+ `src/runtime/graph_edges.cpp#syncEdge` 那条检查 | P0-2 账页 | `c+12−1` `f18/+846−11` `src68` |

**A 不是"已证绿"。** 这 44 条里面**没有一条自己跑过闸门**（这一趟不构建）。A 的意思是"内容上够格进队列"，
闸门仍然要按老规矩每条一次；`lane-withgrad`(248)、`lane-c3-merged`(220)、`lane-marks`(211) 三条贴着线，
排在队尾，合之前先看它自己的读数。

## 4. B 类：卡点（17 条）

| 分支 | 读数 | 卡在哪一条 |
|---|---|---|
| `parked-arrpat` | `c+3` `f20/+580−146` `src560` | **已知有红：闸门第 46 次那七条回归**（合并位的读数，本趟不构建、没复跑）。它把 `match type` 拿数组类型的维度拆开、`ShapeOf` 陈述秩，而那七条红的另一半是开秩 `float[..]` 拿不到形状 |
| `lane-chainplace` | `c+1` `f62/+2120−187` `src1388` | 把十条车道的批与 seal 一次性搬回：**依赖 `closeChainOver`（`src/runtime/draft_inline.cpp`）那侧的修还没落地**；不先修，它一落地就在 `benchmarks/mlp/model.cml` 上报"分支臂读到它不该读的 `time:now`"，`std::opt` 在真实程序上退回去（`docs/handover/reports/2026-11-04-what-lands-and-what-waits.cn.md#closeChainOver`） |
| `lane-b2` | `c+57−2` `f39/+1388−319` `src1011` | 59 笔实提、8 页新问句 + 7 个 transforms 源文件：**面太大**，一条闸门装不下；拆它的入口是 `lane-b2triage` 那一页 |
| `lane-dedupe` | `c+2` `f32/+1377−233` `src1180` | 面太大（23 个源文件），且 `src/runtime/graph.cpp#collectStaticGraphRefsRecursive` 与 `src/passes/trans/json/gir_canonical.cpp#canonicalNodes` 今天仍是 `array->size()` |
| `lane-eff-rule` | `c+11−1` `f30/+937−124` `src557` | 面太大；tiles 的区域资格（`modules/tiles/src/graph_regions.cpp`）与 15 个源文件一起到 |
| `lane-lifcall` | `c+1` `f8/+476−65` `src362` | `modules/transforms/jvp.cpp` 的 362 行改动 + 3 个新用例，**但同一批 `jvp.cpp` 已经被 `lane-vjplaw`、`lane-b2`、`lane-dedupe` 各自动过**：撞同一文件，先定谁先合 |
| `lane-loopfix` | `c+4` `f3/+626−189` `src720` | 720 行全落在 `modules/transforms/split.cpp` 一个文件上，与 `lane-splitproj`、`lane-dedup`、`lane-fsh1` 抢同一处 |
| `lane-packdots` | `c+12` `f27/+513−497` `src677` | 面太大：同时改 `stdlib/tensor.cml`、`rulesets/{jvp,transpose}.cml`、`modules/transforms/split.cpp` 与 27 个用例/计划 |
| `lane-vjplaw` | `c+1` `f21/+396−170` `src403` | 403 行在 `transpose.cpp`/`rule_expand.cpp`，同一族的七条红（见 `parked-arrpat`）与它共用 `rule_expand` |
| `lane-sumto` | `c+2` `f20/+574−295` `src551` | 面太大：`sum_to` 取 extents 本身，穿过 tensor/onnx/tiles 四处 |
| `lane-gcfreed` | `c+11` `f15/+671−13` `src585` | 面太大（13 个源文件，`mm/poison`、`runtime/program`）+ 9 个合并提交，基线旧 |
| `lane-gs6` | `c+11` `f17/+705−39` `src372` | 11 笔实提 + 6 个合并提交；`EffectLivenessFacts` 的七个装配点与 `:op/set_index` 那条（`docs/handover/questions/2026-11-02-can-any-spelling-reach-set-index.cn.md` 不在 develop 上） |
| `lane-tidy` | `c+7` `f1125/+1551−1546` `src796` | **面最大的一条**：把 `agents/`、`docs/`、`exemplars/` 整体挪进 `internal/`，删 `agents/README.md`，1125 个文件。这是结构性搬迁，不是 lane，不能按"合一条"处理 |
| `lane-bisect` | `c+7−3` `f18/+655−70` `src261` | 12 笔 + 2 个合并；`modules/tiles` 与 `src/compile/gir` 两侧同时到，且 3 笔已进 develop 的页 |
| `lane-edges` | `c+5−1` `f12/+365−186` `src60` | **孪生**：与 `lane-edges-green` 是同一件事的两次尝试（两条各 3 笔、互不包含，develop 只收了它们共有的那 1 笔）。只能二选一，选哪条要闸门读数 |
| `lane-edges-green` | `c+5−1` `f7/+160−114` `src33` | 同上，孪生的另一条；它比 `lane-edges` 少改 `src/passes/trans/cpp/cpp_types.*`，也少一个 `refuse_narrow_float` 用例 |
| `lane-rc00` | `c+2` `f3/+136−2` `src40` | **撞路径**：与 `lane-rc04` 都新建 `test/plans/feat/opt/adb67e081.plan.toml`；而它那 40 行 `src/passes/opt/defun/defunctionalize.cpp` 的修不在 `lane-rc04` 里。两条都合不了，先在 `rc04` 的 plan 上重放它那一笔 |

## 5. C 类：取代依据（15 条）

| 分支 | 读数 | 谁取代了它（可复核） |
|---|---|---|
| `lane-arrpat` | `c+2` `f18/+540−145`；`git rev-list --count lane-arrpat..parked-arrpat` = 26、反向 = 0 | **被 `parked-arrpat` 全部包含**（后者保留）。关掉它不丢东西：那 26 笔里含它自己的两笔 |
| `lane-docsync` | `c+0−1` `f1/+2−1` | `46bd7e454`（同一句 subject 的重新生成已在 develop） |
| `lane-stdlibref` | `c+0−1` `f1/+18` | `08150337a`（`docs/stdlib-reference.md#may_be_rearranged` 已在） |
| `lane-fsh1-pre2` | `c+0−2` `f2/+102−53` | `2ccce5f09` + `1640796ef`（两笔各自在 develop 上有等价笔） |
| `lane-exitfix-probe` | `c+0−1` `f1/+24−4` | `3d47285a4`（"the loop and the join hand on the cotangent they built" 已在 develop）。名字是 probe，但内容已经进了 |
| `lane-mult` | `c+0−0`（只有 merge）`f6/+282` | 两个父 `461114c82`、`6c5fa05e8` 都是 `origin/develop` 的祖先（`git merge-base --is-ancestor` 退出 0）；它带的两个用例在 develop 上逐字节相同 |
| `lane-powzero` | `c+0−0` `f12/+234−177` | 两个父 `0c97cde50`、`c457e7e4f` 都在 develop 里 |
| `parked-shapeext` | `c+0−0` `f30/+917−310` | 两个父 `4d789fcc1`、`bcaa5ad60` 都在 develop 里；三个新增用例在 develop 上逐字节相同。名字写 `parked`，但内容一件不少地已经在了 |
| `m-train-merge` | `c+0−0` `f11/+395−55` | 两个父 `0c7e5f20a`、`13426126f` 都在 develop 里 |
| `merge-train-20261104` | `c+1` `f5/+396−5` | **被 `lane-kconv` 取代**：它带的 `72d758fbf` 就是 `lane-kconv` 的唯一一笔（同一 subject、同一组 5 个文件），它自己那笔 `bfcf308eb` 只是把 `lane-kconv` 的树合了进来 |
| `lane-reverse` | `c+1` `f3/+150−41` | `10e097354`（同一句 subject；`modules/transforms/transpose.cpp#buildTransposeLoop` 的"rounds are undone from the last to the first"已在 develop） |
| `lane-typelaw` | `c+1` `f3/+34−26` | `c6bcbdb70`（develop 的 `stdlib/signatures/transforms.cml#transpose` 与 `docs/stdlib-reference.md#transpose` 已是 `transpose(f)(1.0)` 那一版） |
| `lane-zeroplan` | `c+4` `f7/+208−3` | `7374711ad`：它新增的 5 个文件（`scripts/plan-dir-markers.mjs` + 四个 `test/plans/**/README.md`）与 develop **逐字节相同**；只剩 `test/tools/run-tests.mjs` 那一半 develop 已另写 |
| `lane-sigtable` | `c+4` `f1/+117−1` | **要改的页已经不在 `questions/` 了**：`git cat-file -e origin/develop:docs/handover/questions/2026-11-03-who-carries-the-forward-halfs-intermediate-values.cn.md` 退出 128，页在 `docs/handover/reviews/` 下（随 fsh-dev 的 `fdf5f36b7` 进来）。四笔提交改的全是正文 |
| `lane-gaterepo` | `c+1` `f4/+1582` | **闸门入口已在树里**，且已被重写取代：`internal/tools/camel-remote.ps1` 在 develop 上经 `20a2eaacb`、`d603372fc`、`e2066057d` 三次改写，与分支那份 DIFF；只差 `internal/tools/camel-remote.local.ps1.example` 一件（`git log origin/develop --` 该路径 0 命中，**从没进过 develop**）。这一件另开一笔，不要把整条合回来 |

## 6. D 类：探针与保留（11 条）

| 分支 | 读数 | 服务于哪个结论 | 结论记在哪 |
|---|---|---|---|
| `hold/fsh-dev` | `c+1` `f31/+388−256`（含 `export_matrix.csv/md` 重录） | **作者的钱，只列不判处置** | — |
| `release` | `c+1` `f1/+8`（只动 `changes.log`） | 发布线，不是 lane；**只列不判处置** | — |
| `lane-knownau` | `f0/+0−0`（`git diff --stat origin/develop...lane-knownau` 空）；`git log -1 --format=%s` = `test merge: measure the pins lane-oneword moves (not for push)` | 量 `lane-oneword` 动的那几个 pin | **查不到**：`git grep oneword origin/develop -- docs` 0 命中 |
| `probe/revert-dry` | `c+3` `f8/+101−498` | 把 `lane-lit2` 的合并整条撤掉会怎样（B3 撤回的预演） | `docs/handover/reports/2026-11-04-b3-reverted-and-the-ten-it-does-not-explain.cn.md#b3` |
| `probe-b3full` | `c+1` `f10/+200−202` | 同上，B3 "整份"撤回那版 | 同上 |
| `probe-b3off` | `c+1` `f4/+96−82` | 同上，只撤 B3 那版 | 同上 |
| `probe-joint` | `c+1` `f5/+143−60` | 轴归约要不要自己陈述 extents | `docs/handover/reports/2026-11-04-the-three-laws-state-their-extents.cn.md#statedExtents` |
| `probe-mark-placefork` | `c+1` `f1/+4−4`（`src/runtime/draft_inline.cpp`） | `resolvePlaceForks` 回到 `8c48dbe37` 之前读 mark 的样子，看那笔是不是必需的 | `docs/handover/reports/2026-11-05-rc04-answers-nothing-and-rc00-signals-under-gir-too.cn.md#resolvePlaceForks` |
| `probe/bisect-forward` | `c+1` `f1/+2−1`（只动一页） | 把 `closures-as-values` 那页往前推一笔做 bisect | `docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md#bisect` |
| `probe/shuffle` | `c+1` `f1/+2−1`（同一页） | 一个什么都不改的单元，runner 的 plan 读不读得到 | 同上 |
| `probe/prevconv-array-conversion` | `c+1` `f1/+18`（新增 `test/cases/compile/transforms/jvp_of_an_array_conversion.cml`） | 注册之前那棵树上，数组转换有没有带着类型 | **只有用例本身是读数**（该文件在 develop 上不存在）；没找到结论页 |

## 7. 判不了（具名，缺哪条读数）

1. **`lane-edges` 与 `lane-edges-green` 哪一份是对的** —— 缺两条各自的**全量闸门读数**。我这边只有内容读数：
   两条各 3 笔、互不包含，develop 只收了它们共有的那 1 笔。**不落到"随便留一条"**，所以两条都留在 B，等一次闸门。
2. **`lane-c3-merged` 那 220 行今天还适用吗** —— 它改的 `modules/transforms/{split,transpose,jvp}.cpp` 在 develop 上
   全都被重写过（R3 三个文件全 DIFF），缺的是 `feat/transforms/split` + `feat/compile/transforms` 在它这棵树上的**一次跑**。
   我按"内容上够格"把它放进 A 的队尾（#40），**但它的适用性没量过**。
3. **`parked-arrpat` 的"七条回归"** —— 那是**合并位闸门第 46 次**的读数，不是本趟量的（这一趟不构建、不跑计划）。
   本趟关于它只有内容读数：`c+3`、`f20/+580−146`、`src560`。B 的结论依赖那条外部读数。
4. **`lane-knownau` 的结论记在哪** —— `git grep oneword origin/develop -- docs` 0 命中，`probe/prevconv-array-conversion`
   也没有结论页。这两条按 D 保留没问题（它们本来就不是拿来合的），但**"结论落在了哪一页"这一条读数是缺的**。

## 8. 这一趟没做的

- 没 merge、没 rebase、没 push、没删分支、没删工作树、没 `git stash`（来历不明的 stash 一条没碰）。
- 没构建、没跑计划：87 条**一条都没过闸门**，所以本页没有一条"绿"的读数，只有内容读数。
- 没判 `hold/fsh-dev` 与 `release` 的处置（作者的钱、发布线）。
- 没把 22 条已经"部分进 develop"的分支逐笔拆（`lane-b2` 57 笔、`lane-bisect` 7 笔、`lane-marks` 8 笔、`lane-eff-rule` 11 笔）：
  拆它们各自有入口，`lane-b2` 从 `lane-b2triage` 那一页起。

