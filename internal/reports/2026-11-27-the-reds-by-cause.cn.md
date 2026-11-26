# 快层 531 条红：按根因分类，以及下一刀切哪里

**状态：只读分析，没有改树。** 这一页只回答一个问题：剩下的红**按"同一个修复能让它们一起消失"**分成几类，
每类多少条、涉及几个计划、最可能的那一处根因在哪。它不修任何一条。

## 一、读数与口径

| 项 | 值 |
|---|---|
| 名单 | `D:\Temp\promo21-reds-531.tsv`，**531 行**（`plan<TAB>test`，无理由列） |
| 名单来自 | 快层整树一脚，531 失败 / 9161 通过 / 28 跳过 / 33 已知红，`TREE_STATE=clean` |
| 归因依据 | 同一次跑的分层日志 `D:\Temp\promo23-layer.log`：531 段 `[FAIL]` 各自的 `[INFO]` 与 `[stderr]` 尾 |
| 诊断名口径 | 期望的 `RuntimeDiag::X` / `SemanticDiag::X` 对 `src/kernel/diagnostics/registry.tsv` 的消息模板（占位符换成通配）再在输出里找 |
| 一次解析修正 | 第一遍只认出 519 段：**12 段的耗时写成 `4.99 s` 而不是 `402.52 ms`**，按行首名字匹配后 `519 + 12 = 531`，没有遗漏 |
| 自洽 | 下表八类之和 **= 531**（无"约"）；22 条的 `[stderr]` 为空段、5 条是 `gc_poison_sweep` 的合成副本，各自点名在第七节 |

**名单的时点效应（必须写在前面）**：这份名单是 `065bfb1690` 上的一次快照。写这页时，其中
`docs.headers` 那条与 `docs.ai_reference` 那条**已经绿了**（同一晚先修的头行与 AI 参考页），
所以"531"是快照数，不是此刻数。

## 二、八类，按"一处修复能消掉多少条"降序

类的判据只有一条：**同一个修复能让它们一起消失**。不是按计划名分，也不是按消息文本原文分。

| # | 类 | 条数 | 计划数 | 一处修复能消掉的量 | 最可能的根因 |
|---|---|---|---|---|---|
| 1 | **记录里的诊断位置早于两次语料改写** | **278** | 18 | 一次重录（改计划字段，不改代码） | `eb20efab7b`（返回类型移到 `=>` 后、块体拆行）与 `91bdec1676`（删 `module` 行）把用例正文挪了位；诊断本身照常发出，名字与消息都对，只有记录的 `line`/`column` 是旧排版 |
| 2 | **用例不再能编译（编译期拒绝）** | 103 | 42 | 未定：至少 5 个互不相同的子组（见第四节） | 子组各有根因；最大一组指向 `src/stdlib/prelude.cml:1229`（`range_from` 声明 `=> int..`，体里交回的是 `func ()`） |
| 3 | **用例自带的校验脚本退出 1** | 84 | 26 | 多数是 1、2 类的下游（脚本解析的输出文本变了），修完 1、2 类会自己消失 | 校验脚本读的输出片段随位置/措辞漂移；不去改脚本 |
| 4 | **文本期望缺失（`output does not contain 'file:L:C'`）** | 35 | 19 | 与 1 类同源、同一批重录 | 期望里写死的 `用例:行:列` 已不在输出里 |
| 5 | **期望诊断但输出里没有带位置的 `[Error]` 段** | 11 | 4 | 未定 | 输出被截断，或在 `stdout` 里而没有位置 |
| 6 | **读数差异（精确 stdout 4 + 金标 4）** | 8 | 3 | 每次重录 | `modules.graph*` 是节点与匿名图的编号整体后移（期望 `47` 实得 `60`，期望 `__g_51__` 实得 `__g_64__`）；`examples` 四条是金标文本 |
| 7 | **时间/CPU 预算** | 5 | 2 | 一次预算判定 | `opt.splice_place` 四条同族（`perf_fib_smoke.opt_twice.*`）是同一用例四个后端的 CPU 超预算 |
| 8 | **其他** | 7 | 3 | 未定 | 5 条是 `gc_poison_sweep` 的合成副本（跟着源计划走），另 2 条各 1 |

合计 **531**；名单里共 **88 个计划**（树上 454 份计划文件）。

## 三、第 1 类：278 条，Δ 行数的分布就是它的证据

对每条同诊断名、同消息、**只差位置**的红，算 `实际行 - 期望行`：

| Δline | 条数 | 指向 |
|---|---|---|
| **+1** | **195** | 块体从那行拆到下一行 —— `eb20efab7b` 的格式化；其中 133 条列号也变小（行变短了），12 条列号不变 |
| **−1** | **28** | `module <name>` 首行被删，整份用例上移一行 —— `91bdec1676` |
| **+12 / +8 / +20** | **25** | `modules.stdlib_faults` 的库内位置：`src/stdlib/math.cml` 自己增删过函数（`erf`/`erfc` 那笔加了一打行） |
| **+2 / +3** | **18** | 同一波格式化的残留（用例被拆成更多行） |
| 0（位置对、其余不符） | 3 | 未定 |

合计 195 + 28 + 25 + 18 + 3 = **278**。

一处就够看懂的证据：

```
test/cases/consistency/faults/mod_zero_in_function.cml
  记录前（91bdec1676）：第 5 行 = "func rem(a: int, b: int): int { return a % b }"   46 字符，`%` 在第 42 列
  现在：            第 6 行 = "    return a % b"                                    14 列
  计划 expect_diagnostic_line = 5, expect_diagnostic_column = 42
  这次跑输出：    mod_zero_in_function.cml:6:14: [Error]: Attempted division by zero. …
```

⇒ 诊断没错、消息没错，**错的是记录**。这一类的量（278 条 = 全量的 52%）也说明它不是一堆小问题，
而是**一次语料改写之后没人重录**。`internal/backlog.md` 里已经有一条同形的裁定：`D-1126-155`
对 `consistency.attempt` 判"就是重录"，并留了一句"同时查 `eb20efab7` 动过的其他用例里还有没有带位置的金标"；
`C-1124-461` 也把 `operator_overloads` 的四条旧红归到同一个根因（"改了用例、没改计划"）。这一页回答的就是那句
"还有没有"：**有，278 条**，而且不止金标，计划的 `expect_diagnostic_line` 字段同样欠着。

**这一刀的做法**：跑这 18 个计划（`consistency.faults*`、`linear.faults`、`modules.stdlib_faults`、
`modules.tensor_int_division`、`modules.tensor.shape_inference` 等），把 `expect_diagnostic_line`
与 `expect_diagnostic_column` 按真实输出重录。**不改编译器**：位置由排版决定，代码没有变坏。

## 四、第 2 类：103 条，五个子组，别当成一刀

| 子组 | 条数 | 典型输出（原文片段） | 判断 |
|---|---|---|---|
| `prelude` 交出的类型不对 | 14 | `out/latest/stdlib/prelude.cml:1230:12: [Error]: The value the graph 'prelude::range_from' hands on has type 'FnValue___g_63___0', and the graph declares …` | **像真缺陷**：`src/stdlib/prelude.cml:1229` 写的是 `func range_from(i: int, stride: int) => int..`，体里第 1230 行 `return func () { … }` —— 声明的 Range 与交回的 `FnValue` 在图的类型检查里对不上。集中在 `opt.generic`（12） |
| `SemanticDiag::NoMatchingFunction` | 14 | `(name=SemanticDiag::NoMatchingFunction, code=0x0300000F)` | 未定：要看被拒的那次调用是不是本来合法 |
| 返回类型与声明不符 | 14 | `content_equality.cml:12:5: Return type 'Range' does not match the function's declared return type 'int[]'` | 用例把 `0 .. n` 当 `int[]` 交回；是隐式物化被收紧还是用例该改，需裁定 |
| 泛型调用被拒 | 7 | `bootstrap_medians.cml:18:18: The generic function 'shuffle' cannot be called with argument types <Key> (Range)` | 未定 |
| 过载 `when` 全假 | 4 | `adam_update_bits.cml:31:15: No candidate of 'f32' applies to (<> (Range)): every candidate's 'when' condition is false` | 未定（四条同族，一个用例四个后端） |
| 其他（`PassNotApplicable` 的 `edits` 库、`transforms: transpose has no forward transform`、`Unresolved reference`、无 stderr） | 50 | `train.cml:33:1: … PassNotApplicable: its library 'edits' does not …` | 未定：`edits` 这类**名字已经不在树上**的引用与 1 类不同，是真行为差 |

⇒ 第 2 类里真正"一处修复消掉十几条"的只有第 1 子组（`src/stdlib/prelude.cml:1229` 的 `range_from`）；其余是逐用例的判断，
没有一条能靠改一个文件收掉。

## 五、后端族：337 条红只对应 63 个用例

把测试名末尾的后端后缀（`.nvm` / `.fvm` / `.jit` / `.prl` / `.inline_jit` / `.tf` …）去掉再数：

| 项 | 值 |
|---|---|
| 531 条里落在"同用例多后端"族里的 | **337 条** |
| 这样的族 | **63 个** |
| 其余独立用例 | **194 条** |
| 含义 | 每个族里的四条（或六条）共享**同一份期望**，坏在共享代码里而不是某个后端 |

⇒ 这条很重要：**531 条不是 531 个问题，而是 63 个族 + 194 个独立用例**。任何一族的修复都按整族消。

## 六、下一刀的次序（按"一处修复消掉多少条"排）

1. **重录诊断位置**（第 1 类，278 条 + 第 4 类的多数）：改计划字段，不改代码；一次机械重录，按 18 个计划分批。
2. **`src/stdlib/prelude.cml:1229` 的 `range_from`**（14 条）：一处代码，先判是缺陷还是用例（声明 `=> int..`，体里交回 `func ()`）；其中 `opt.generic` 占 12 条。
3. **第 3 类多半会随 1、2 消失**（84 条）：重录后再测一次，剩下的再逐个看，不要先改校验脚本。
4. **第 2 类剩余子组**（103 − 14 = **89 条**）：逐用例判"隐式转换被收紧 / 过载条件变了 / 库改名"，需要裁定。
5. 最后才是第 6–8 类（20 条）：读数与预算，量小。

## 七、口径与已知偏差（这一页不装作没有）

- **名单只有两列**（计划、测试名），没有理由列；本页的理由全部取自同一次跑的日志（531 段 `[FAIL]` 各自的 `[INFO]` 与 `[stderr]` 尾），不靠名单之外的转述。上一张带理由的红表只覆盖其中一部分，这里没有拿它兜底。
- **5 条 `gc_poison_sweep` 合成副本**：失败来自源计划（`source plan test/plans/feat/...`），不单独归因，跟着源计划消。
- **22 条 `[stderr]` 为空段**：日志只印尾部，有的段被截断；它们落在类 2 的"其他"里，标"未定"而不是猜一个原因。
- **时点**：名单是 `065bfb1690` 的一次跑；此后落地的两笔（头行、AI 参考页）已让其中两条转绿。**531 是快照数**。
- **"位置"类的判据**是"同一诊断名 + 同一消息模板出现，只是行的位置不同"，不是"我猜它挪了"；`Δline` 的分布（195 条 +1、28 条 −1、25 条 +8/+12/+20）与两次改写的形状完全一致。

## 八、名单之后落地的改动里，可能改到名单的只有五条

从快照到现在，落在 `test/cases` 或 `test/plans` 上的改动只碰到五个计划：`compile.effects_syntax`、
`linear.toplevel_var`、`modules.resources`、`tutorial_examples`、`probe.resnet_block_space`。把这五个和 531 条名单对一遍：

| 名单里的条目 | 计划 | 为什么可能已经不一样 |
|---|---|---|
| `sync.pure_call_is_arranged_freely`、`sync.pure_call_has_no_edge_to_the_statement_beside_it`、`sync.effect_call_stays_on_the_chain`、`sync.faulting_call_stays_on_the_chain` | `compile.chain` | `pure` 的承诺从"不产生效果"改成"不写"；这四个用例正是拿 `pure` 的两份写法比图，判定变了它们就该跟着变 |
| `resnet_block_space` | `probe.resnet_block_space` | 这个探针已移出快层，下一脚快层不会再跑它 |

其余 526 条在这批改动里没有被碰到：**没有一笔在重录诊断位置**，所以第 1 类仍然是欠着的一刀。
`tutorial_examples` 的四条也在名单里，但那一笔改的是计划描述里的散文（`10_pure_callback` 那段的说法），不是期望字段，它们仍属第 2 类的逐用例判断。

> 一句话：**这 531 条里一半以上是"记录没跟上改写"，不是"程序坏了"**；真正需要动代码的，最大一处是
> `src/stdlib/prelude.cml:1229` 的 `range_from`，其次是一批逐用例的编译期判断。
## 附：第 3 类（84 条“用例自带校验脚本 exit 1”）的分类读数

取数：`D:\Temp\t122\fail\**` 里已落盘的 `meta.json.failures[]`（脚本自己那一句就在 `verification output:` 段，不用翻日志）。

**84 条是怎么来的**：531 里带 `verification` 记录 100 条、脚本退出非 0 共 88 条；88 减 4 条首行为
`expected diagnostic RuntimeDiag::…`（属“诊断位置过期”）后得 84，落 26 个计划 —— 与该页第 3 类的 `84 / 26` 对上。

| 类 | 条数 | 判据 |
|---|---|---|
| A 断言值不同 | **84** | 84/84 都是脚本自身 assert 抛出的“期望 vs 实际”不符 |
| B 脚本自己崩 | 0 | 88 条里 `TypeError/SyntaxError/ReferenceError/…` 一次未出现 |
| C 缺输入或路径 | 0 | `ENOENT/Cannot find module/no such file` 0 次 |
| D 超时或被杀 | 0 | `timed_out=False`、`signal=null`（runner 的 `meta` 与 `verification` 两处都查过） |
| E 其他 | 0 | 84/84 都带得出一句脚本消息 |

**A 的三种形状**：位置漂移 G2 30 条/9 计划 · 程序不再跑得起来 G1 10 条/3 计划 · 输出文本/值/计数 G3 44 条/17 计划。

**按“程序到底跑起来没有”再切一刀（这一刀决定处置）**：
- **46 条** stderr 有 `[Error]` ⇒ 程序根本没跑到输出，是**第 2 类（编译期拒绝）的下游**，与第 2 类一起消失，不单开车道；
- **37 条** 程序正常跑完、只是脚本期望不符 ⇒ **记录过期，但过期的不在计划字段里**，在计划 inline `verify` 的 JS 文本（外加 3 个用例自带的 `.mjs`）；
- **1 条** 退出 1、stderr 空、stdout 是一整张 GIR ⇒ 单独记。

**处置**：不单开修复车道；G2 那 21 条（程序跑得起来、只差位置）与“位置重录”同一批（同一次 run 取位置），
单列子项“计划 inline verify 脚本的期望文本”；其余 16 条 G3 之后逐条判。

**口径补一处**：`test/tools/writing-freedom.mjs`、`have-native-jit.mjs` 这类**计划级脚本**在 531 里有 10 条
（如 `compile.chain` 4 条），失败时首行同样是 `expected exit 0, got exit 1` 但 `verification` 字段为空 ⇒
按“脚本 exit 1”口径会被漏掉。故这一类实际为 **84 + 10 = 94**；本页表仍按“失败首行”口径记 531，不重算。
**那 10 条（`verification` 字段全空，按“脚本 exit 1”口径一条都抓不到；8 个 `.mjs` + 2 个 `.py`）**：
```
compile.chain/sync.pure_call_is_arranged_freely                      writing-freedom.mjs
compile.chain/sync.pure_call_has_no_edge_to_the_statement_beside_it   writing-freedom.mjs
compile.chain/sync.effect_call_stays_on_the_chain                     writing-freedom.mjs
compile.chain/sync.faulting_call_stays_on_the_chain                   writing-freedom.mjs
compile.region_roots/region_roots.writing_freedom_of_two_writes        writing-freedom.mjs
modules.stdlib_registry/registry.no_env_switches                       check-env-switches.mjs
opt.second_pass/second_pass.perf_fib_smoke                             opt-cost/second_pass.mjs
opt.progen/progen_seed_4                                               progen/progen.py
trans.cpp/agree_with_fvm                                               cppexec/run.py
trans.source_spelling/every_generator_writes_source_the_parser_reads   spelling/check.py
```
（`registry.no_env_switches` 这一条已在别处修好 —— `4c38e91fa5` 把块追踪改成 pass 参数并合进 integration；它留在这张名单里是因为**当时**的首行就是它。）

**单独记的那 1 条**：`compile.higher_order_clause/node_role_reads_the_fact` —— stdout 是一整张 `digraph GraphIR`（5499 字节）、
stderr 空、退出 1，脚本在 `<anonymous>:3:5` 断言 “camel exited 1” ⇒ **跑了、也画出了图，但退出码是 1**，
既不是“期望 vs 实际文本不符”，也不是“没跑起来”，等第 2 类修完复测时单独看。

## 附二：第 2 类（“用例不再能编译”）按诊断口径是 130，不是 103

第 2 类那个 **103** 是按**失败首行的文本形态**数的。换一个可复算的口径，同一个落下目录给出 **130**：

```
口径：`meta.json.diagnostics[0].name` 以 `SemanticDiag::` 开头 **且** `diagnostics[0].path == meta.case_path`
      （拒的正是用例自己的文件），不另筛 exit code 与 stdout 长度
读数：130 行 · 67 个不同用例文件 · 33 个诊断名
对照：第一条诊断是 `RuntimeDiag::*` 的 342 行、一条诊断都没有的 59 行、以 `SyntaxDiag`/`LexicalDiag` 开头的 0 行
试过的其它口径：121（stderr 文本前缀）/ 118（exit 1 且 stdout 空）/ 78（首行 `expected exit 0, got exit 1`）/
                 67（用例文件去重）/ 38（负向测试）/ 14（首行 `verification exited 1`）—— 无一等于 103
```
两个数**不冲突**：差别在“首行的形态”与“第一条诊断”不是同一件事（有些条目首行是 `expected diagnostic …`
但第一条诊断仍是 `SemanticDiag`，前者被归到别的类）。本节不改上面那张表（它的口径是首行形态、总和 531），只补口径说明。

**口径更正（同一次复核里发现的）**：`meta.json` 的 `diagnostics[]` **只存结构化字段**
（`path` / `line` / `column` / `name` / `code`，全 472 份带诊断的 meta.json 里**没有一份**有 `message` 字段）
⇒ 想按“诊断正文”数任何一组都是 0 行，那是**字段形状**造成的，不是没有这一类。
读诊断正文要去 `output.txt` / `stderr.txt`。下表这一类因此改写成“**失败输出里**出现 `Range`”，实测 67 行。

**根因簇（130 里的 67 行 = 51.5%）**：该用例的**失败输出**里出现 `Range` ——
`docs/design-principles.cn.md:415` 裁定“范围是值，不是数组”，`a..b` 的值是 `Range`；
而同一段“现状（2026-11-23）”自己写着“按期望类型转 `int[]`”这条路**还没落地**；
`f32` 的候选都带 `when is_compact_array`，`Range` 无一命中。
归属：`internal/backlog.md` **`D-1123-41`**（拆 a/b/c 三条，整条 blocked 在 `C-1123-61` 占着同一处名字解析代码）。
历史读数：`778e8488a2` 曾把 `f32(range(6))` 改写成 `f32((0..6))`，当时可编译 —— 这批是**迁移尾巴**，不是用例写错。

**三组（合计 130）**：迁移 14 行 / 14 文件（名字与标记过期，编译器拒得对）· 缺陷 82 行 / 26 文件 ·
其它 34 行 / 27 文件（用例仍被正确拒绝，是期望的行/列/名字/告警条数移位）。
其中 82 行里 **67 行属 `D-1123-41`**，此时新开“修 Range 转换”的车道会与那两条抢同一处代码；
剩下 **15 行**（loop 状态类型 6、空数组元素类型 5、分支/match 返回类型 2、声明类型 1、非 Range 的 `NoMatchingFunction` 1）
**先派“判定”不派“修复”**：先问“设计是不是有意变严”，再决定改用例还是补实现。

**与第 3 类的交叉**：第 3 类里 84 条“脚本 exit 1”的**46 条**，其 `stderr` 有 `[Error]` —— 它们正是本类（编译期拒绝）的下游，
本类修好时一起消失，不另开车道。
