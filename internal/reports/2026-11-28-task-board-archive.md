# 任务板留底：已完成任务清单（2026-11-28）

**为什么要这份清单**：任务板上限 256 条，满了以后按 `D-1127-163/165` 的老办法 —— 先把已完成的任务一次写留底，再连续删；open / doing / blocked 与当天新建的不动。这份清单是删除前的唯一记录。

**读数**：删除前 `total 256`、`completed 107`、`in_progress 89`、`blocked 15`、`pending 45`（`oversight_readings` 的 `backlog` 块给的是 backlog 行数，不是任务数；这四项按 `team_task_list` 的分页合计）。

**每条的含义**：`id` / `revision`（删除时的 CAS 前置值）/ 标题。最终状态全部是 `completed`；读数与提交留在各自车道的交回与提交正文里，本清单不复制读数，只保证"哪条任务曾经存在、叫什么、最后状态是什么"不丢。

| id | rev | 标题 | 最终状态 |
|---|---|---|---|
| `task-29` | 3 | 收工前跑一轮四个 GitHub Actions 工作流（build-pypi 显式 publish_to=none） | completed |
| `task-30` | 4 | frame.h 去 include runtime/graph.h：GCGraph 的小记录拆成窄头（B 方案） | completed |
| `task-31` | 5 | lane/fvm-diff 换底并进合并；lane/rescue-1 归档 | completed |
| `task-35` | 3 | conv2d 的 *_shaped 走读值端口，落地后律改原版算子并删掉三个 *_shaped | completed |
| `task-37` | 3 | 审计 test/ 与 src/ 里对 internal/ 页面的引用是否还指得着（只报不改） | completed |
| `task-39` | 3 | 修 20 条失效的内部页引用并按生成命令重生成 coverage-table（misc） | completed |
| `task-41` | 3 | fvm 的 OOM 诊断写进了 stdout（应只写 stderr 一次） | completed |
| `task-47` | 3 | 一个模块里同名的普通函数与算子要合成一组候选（Range 的 map/filter/foreach 从别的模块调不到） | completed |
| `task-48` | 4 | 把第 14 跑残留的 27 条红在当前尖端上逐条判类 | completed |
| `task-49` | 3 | onnx 里剩下那两行 `:op/` 字符串：漏改还是必须留 | completed |
| `task-72` | 4 | 慢层真红第五族：`feat.trans.cml` 两条逐条判类 | completed |
| `task-74` | 3 | 返工：算子名字不许散写在调用点 —— 进固定编号表，exporter 的 `:op/` 拼接改成问表 | completed |
| `task-75` | 3 | cse 13 条的根因：`facts.cpp` 的 `anchorOf` 不认草稿 —— 改用与 `read.cpp` 同一处句柄解析 | completed |
| `task-78` | 3 | 拼法清扫的覆盖面有洞：把 `test/cases/modules` 纳入 lint，并出 33 处分诊清单 | completed |
| `task-79` | 4 | 重落地“诊断写 stderr”：含文档、116 条用例、读流工具、plan-selection 并集规则、gc_live_large 两侧 | completed |
| `task-80` | 3 | ref_boxes 5 条改判 nvm 偏：退出前把待终结的外部资源做完，`finalized == released` | completed |
| `task-81` | 5 | conv2d 的 stride/padding 改回编译期参数（更正 D-1126-138），规则左边按 `sum<axis, keepdims>(xs)` 的方式绑 | completed |
| `task-86` | 3 | 命令行重构第 1 步：进程级选项改名与合并、信息类改子命令、plan args 一次性迁移 | completed |
| `task-88` | 5 | 先判 cse_written_variable 一族红是“旧金标”还是“归一化没生效”，重录只用 -Entry 点名 | completed |
| `task-92` | 3 | Record the two graph_facts cases the facts-draft commit added: stderr assertion and AST hashes | completed |
| `task-110` | 5 | 承接无主分支 `lane/effect-audit`：剥 ast_hashes、换底、补文字收尾、一次读数，判“可合/弃” | completed |
| `task-112` | 3 | 重生成 `test/selection/coverage-table.json`（生成件 drift，`--check` exit 1） | completed |
| `task-114` | 3 | 接手无主分支 `lane/resource-eq`：`resource` 五个修饰词的整条链，判“补完/可合/弃” | completed |
| `task-115` | 3 | 把 `coverage-table` 的重生成接进批量合并的“生成件”步骤（不新增闸门） | completed |
| `task-116` | 3 | `by_camel` 的两条残余红：`cxx_isolation.cse/fold` 的差分记账是否等价 | completed |
| `task-118` | 4 | 块追踪开关不要走环境变量：`CAMEL_MM_BLOCK_TRACE` 改成 pass 参数（账本只减不增） | completed |
| `task-119` | 4 | `adam_update_bits` 四条既存红：位精确比较比的是哪一处位 | completed |
| `task-123` | 3 | `range_from` 声明返回区间、体里交回函数值：14 条红的同一处代码 | completed |
| `task-125` | 3 | 7 条“诊断名/诊断阶段变了”的红：判真缺陷还是记录过期（不许重录抹掉） | completed |
| `task-126` | 3 | `a @= 2` 报的是内部名 `__assn_mat__` 未导出，而不是“operator '@=' 不适用” | completed |
| `task-143` | 4 | `std::cxx::specialize`：副本的副本被留下，23 张存活图 vs `specializations=16`（簇 D，先取证） | completed |
| `task-145` | 3 | 把 130 条“不再编译”归成族（只读、零 run）：每族一句 root cause + 判类 + 代表用例 | completed |
| `task-146` | 3 | 改写提交把被替换的乘积留在图里（`array_combine` nodes 9 vs 8、`tensor_mul 3 vs 2`） | completed |
| `task-148` | 3 | 三族未定类的红（when 条件 14 / 需要转换 5 / never 实参 4）判到能开车或能发问句 | completed |
| `task-149` | 3 | `tutorial.examples.05_arrays` 四后端全红：判类 + 一次 run 验证 | completed |
| `task-150` | 3 | `--timeit` 跨迭代复用签名（应与 Context 同生同灭）—— 工具缺陷 | completed |
| `task-152` | 3 | `std::opt` 拒绝用到流的程序 —— 真缺陷 | completed |
| `task-153` | 3 | 实现 `Range` 按期望类型转 `int[]`（设计 §415；`D-1123-41` 解封） | completed |
| `task-155` | 3 | 84 条“校验脚本”红：逐条判“脚本过期 / 脚本坏了 / 程序缺陷” | completed |
| `task-156` | 4 | C 类 19 条用例过期（12 Range + 7 循环）+ 追加 14 条 WhenConditionUnmet 按甲改 to_array | completed |
| `task-175` | 3 | task-175：第 2 轮为何没重新导出 `:range/to_range` 的规则（只读数，在导出入口无条件打印） | completed |
| `task-176` | 4 | task-176：乙的修法 —— 进程只留 `uri → C++ 实现`，原生组/规则/签名/resolver 归 Context | completed |
| `task-177` | 3 | task-177：块作用域主体 —— 示例逐字重抄 + inline 收节点实参 + 块内局部值名层 | completed |
| `task-182` | 5 | task-182：28 条计划改成实测接受的拼写（`--gc` + `key=value` 两 argv；`-A`；`--explain`） | completed |
| `task-183` | 3 | task-185：两条过期生成物（questions_index 差 1 行、stdlib-reference 差 11 行） | completed |
| `task-184` | 4 | task-184：gpu 唯一那条红的真因 —— `resident_loop.mjs` 的 JSON.parse 吃掉了程序输出 `36.0` | completed |
| `task-186` | 3 | task-186：用例侧 `--gc` 拼写余波 —— 逗号后空格与裸 kind（8 条 gc.born_old.*） | completed |
| `task-189` | 3 | task-189：`inspect --ops` 被当成顶层选项（9 条 registry.* 红）—— 先查是不是 cmd-word 的回归，再修解析归属 | completed |
| `task-191` | 3 | task-192：收尾 cmd-word 正解 —— 3 条 explain.* + usage 遍历 + 全快层新增红 0（从 5d375363c3 继续） | completed |
| `task-198` | 6 | task-198（3a 残留·transforms）：`src/stdlib/transforms` 的直接引用清到“实现方三类”为止（1 处已落 + 三类判出界） | completed |
| `task-220` | 4 | `src/stdlib/std` 604 处：分类表已出（载荷体 369 / 身份 0 / 写方 5 / 候选 ~230），先做 3 文件探路批，再按族立卡 | completed |
| `task-221` | 4 | `src/stdlib/std/exec/` 族：先出 `count-graph-readers` 基线与“三类计数”，再定整族留还是迁一片 | completed |
| `task-222` | 3 | `src/stdlib/tiles`（60 处）整族归属判定 + 那 8 处纯声明面（`placement.h`/`residency.h`/`graph_values.h`）迁不迁 | completed |
| `task-223` | 3 | `src/compile/src/gct/graph_encode.cpp` 8 处：属“读形状”的现在改走视图 | completed |
| `task-224` | 3 | `src/stdlib/std` 230 处候选：逐文件（52 个）归类到“视图答得了”或三类，视图答得了的当场改 | completed |
| `task-232` | 3 | 工作树配置盘点收尾：缺 `.husky/_` / `camel-gate.local.json` 的树 —— 能用 `-Reuse` 补齐的补齐、没人用的判删（清单先给决策者） | completed |
| `task-240` | 3 | 四样 kind 的旧店读者清单：还剩谁在读 `computeShape/Instance/Reorder/Permissions`，逐处给目标形态与判据（只读，为“删旧登记”铺路） | completed |
| `task-242` | 4 | B1 下一步：新店全程序导出（复用 inspect --facts 的逐图遍历）+ 四样 encode/decode + 往返用例，facts_export 改读新店 | completed |
| `task-245` | 3 | 8 棵脏树的处置判定：每处脏内容“可丢弃/应提交/应留证据”，并判它在别处有没有（只读） | completed |
| `task-247` | 3 | 只读研究：约 79 条遍声明 → 各自该指向的 `#op` 映射表（含逐文件计数与“无指向”清单），供 task-243 补 `#op` 用 | completed |
| `task-248` | 3 | C7 的 run：模型从 with 取出、ORT 会话（dlopen）、DLPack 交接与错误值 | completed |
| `task-265` | 4 | 浮点打印统一到最短往返（B1+B2 都改）+ 因此变动的金标按裁定重生成并注明原因 | completed |
| `task-266` | 4 | 检查器：编译步与运行步共用一处命令行构造 + 删 PENDING 表 16 条 | completed |
| `task-267` | 3 | 超越函数断言改成按 ulp 界比（依据原则 §7:557，容差只在 diff-compare.mjs 一处） | completed |
| `task-268` | 3 | `onnx::export_model` pass 的输入契约：从\"读请求节点\"改成\"读模型式输入\"（接 task-248 记下的缺口） | completed |
| `task-271` | 3 | 追 `frontend.ast_record` 的 16 条 AST stale：二分到笔、判\"期望过期 / 回归\"，说得通才重录 | completed |
| `task-273` | 6 | 修 sanitizers 腿的 heap-use-after-free：`clipp::group` 在 lambda 里析构后，`parsing_result` 仍被读（config.cpp:1093 → :1119） | completed |
| `task-275` | 3 | check-vjp-parity 回到两边都比：共同规则逐项比，单边规则照实列出（去掉\"哪边该有\"的预设） | completed |
| `task-277` | 4 | The vjp @example: why the library's own doc check does not run it, and fix that one place | completed |
| `task-278` | 3 | The runner learns \"flaky\": a failing case is retried once, and a pass on the retry is neither red nor a lost pass | completed |
| `task-281` | 4 | ast_record: 16 block-probe rows are the front end moving, not a golden lag (measured on branch and integration tip) | completed |
| `task-283` | 3 | vjp-parity compares kinds, and the five missing jvp rules make transposition carry the reverse | completed |
| `task-286` | 7 | range's foreach must infer the lambda's parameter on the same path the array form takes | completed |
| `task-287` | 3 | Independent check of the Draft<F> claim: five greens, three signature edits, and both assignability directions | completed |
| `task-289` | 3 | The integration tip's 16 flipped ast records are ours: restore them and record the three missing probes | completed |
| `task-291` | 3 | Where the minimal C5 re-land sits on the \"block value is a closure\" shape (read only) | completed |
| `task-293` | 3 | fault_deep.prl_off aborts on 4 of 10 first runs: that is a bug, not flakiness | completed |
| `task-301` | 4 | A discrete result gets a zero tangent of its own type at the user boundary, not a rejection | completed |
| `task-306` | 4 | A lambda whose parameter is known and whose result is still open: the range form of map and reduce | completed |
| `task-307` | 5 | The jvp law for `:op/idx_arr` declares an operand shape the operator does not have | completed |
| `task-308` | 3 | A citation in vjp-parity points at a file that no longer exists | completed |
| `task-309` | 3 | Decide whether lane/restore-header-channel still fixes anything, 361 commits behind | completed |
| `task-310` | 3 | Decide whether task-57 and its 12 commits still fix anything on today's tip | completed |
| `task-311` | 3 | The entry form ignores dynamic_axes entirely | completed |
| `task-312` | 3 | A wrongly typed law body aborts the compiler instead of reporting a diagnostic | completed |
| `task-314` | 3 | Keep one probe from the discarded editview batch: a view block's value is the function's value | completed |
| `task-315` | 3 | Check whether the generated stdlib reference still matches its generator on the current tip | completed |
| `task-316` | 3 | Classify the 75 new reds between promo55 and promo56 as expected change or regression | completed |
| `task-317` | 5 | The axis form of ':op/idx_arr' has no forward rule, so no read but the first dimension can be differentiated | completed |
| `task-318` | 5 | Re-pin the 80 expectations without emptying the tests that used the exit code to observe a value | completed |
| `task-322` | 3 | The softmax cross-entropy gradient allocates Float32 whatever the logits type is | completed |
| `task-325` | 7 | The transpose reverse loop is written for the graph form the range-ri migration replaced | completed |
| `task-327` | 3 | sum_to and broadcast_like align on suffixes, so a per-channel reduction or broadcast is silently wrong above rank 2 | completed |
| `task-328` | 3 | jvp refuses every function that contains a loop, and the plan comment says otherwise | completed |
| `task-330` | 4 | facts.readers_sweep checks almost nothing (read=0 of 4149) and cannot be green because a fixture never returns | completed |
| `task-333` | 5 | The checkout lock must say who holds it, and a run must die with its local side | completed |
| `task-334` | 3 | Retire the old facts sweep after moving its two behaviours into the one that remains | completed |
| `task-341` | 3 | jvp has no forward rule for tensor:reshape — decide whether the shape op needs a law or a derived path | completed |
| `task-344` | 4 | jvp through a loop: give the walk a record read for nodes with no tangent space, and guard it with a negative case | completed |
| `task-346` | 3 | review-1127: rebase both commits, run the three plans, and prove the formula changed but the numbers did not | completed |
| `task-347` | 4 | resource-wordtable: rebase and four readings | completed |
| `task-350` | 3 | Blocks-as-scopes acceptance 3: the four nesting cases do not exist | completed |
| `task-358` | 6 | Rebase cc-blocktail (292 behind, carries a revert pair) and judge whether that pair still stands | completed |
| `task-362` | 3 | opt::cse does not merge anything inline creates, while std::cxx::cse does | completed |
| `task-363` | 3 | diagnostic file suffix: per-case state instead of a module-level map | completed |
| `task-364` | 5 | The checkout lock's fd leaks into long-lived descendants | completed |
| `task-366` | 8 | edit/view: the block head's name is the block's root | completed |

删除方式：`team_task_update` action `delete`，`expected_revision` 取上表的 rev。删除按 id 升序分批，每批不超过 25 条。

