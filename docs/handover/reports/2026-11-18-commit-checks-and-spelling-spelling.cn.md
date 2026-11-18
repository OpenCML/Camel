# 看不到写法：`nodes` 给出规范顺序，而读写法要声明

- 日期：2026-11-18
- 车道：`lane-spell`（分支 `lane-spell`，从 `b62b76de5` 开出）；合并基线 `origin/develop` = `2e1e77324`
- 计划：`docs/handover/plans/2026-11-18-commit-checks-and-spelling.cn.md` 的**丙：变换看不到写法（原则 9.5）**，只做丙，加上验收 (a)
- 甲（提交检查）与乙（拼写检查）不是这条车道的活；本报告不含它们

## 1. 结论

三句话：

1. **默认查询给规范顺序**：`nodes`、`id` 回到 `src/runtime/canonical.cpp` 的 `CanonicalWalk`（没有第二套走法），`id` 就是规范编号。
2. **读写法是一个能力，和效应同一套机制**：`name`、`origin`、`show`、`creation_id` 在 `stdlib/graph.cml` 里声明 `spelling`，语法位置和 `pure`/`uses` 相同（返回类型之后、`when`/`sync` 之前）。正文读了没声明 ⇒ **编译期报错** `SpellingNotDeclared`，报在声明上、并指出正文里第一次读的位置。
3. **标准库的 pass 和规则不读它**，诊断/调试代码声明了才读：`stdlib/graph.cml` 之外没有任何 `.cml` 变红，`opt`/`trans` 的规则一条都没改。

验收 (a) 通过：同一个程序两种写法，一个贪心融合变换给出的结果是**一个**规范形态（两个哈希逐字相同）；同一变换改读 `creation_id` 就变成两个（哈希不同）。torch.fx 的对照在同一台机器上跑出来了，两种写法给的是两个答案。

## 2. 机制（为什么是「同一套机制」而不是另开一套）

效应声明的链路是 `AST::ResourceClauseLoad` → `gct::Builder::resourceSignatureOf` → `sem::ResourceSignature`（挂在 `FunctionType` 上）→ `Builder::checkInferredResources`。`spelling` 走的是**同一条链路**，只在三处加宽：

- `sem::ResourceSignature` 多一个 `bool spelling`（`include/camel/core/resource_semantics.h`），`signatureOf` 复制它；`covers` **不**加拼写规则（见 §7 第 3 条）。
- 语法：子句的**前导词** `spelling`，后面可以跟 `pure`/`uses ...`，也可以什么都不跟（只声明能力，不对资源作任何承诺 —— 这种子句的模式仍是 `Generic`）。
- 传播：`gir::Builder::readCalleeSignature` 把被调者的 `spelling` 并进调用者的可达集（与效应同样的并集），所以「A 调了 B，B 读写法」时 A 也要声明；函数值一并传播。lambda 正文按设计 5.9 豁免。

报错的样子（`SemanticDiag::SpellingNotDeclared`，`0x030001D5`）：

```
undeclared.cml:7:1: [Error]: This body reads the spelling of the program, and its type does not declare it. [the first reading of the spelling in this body: it is what the type would have to declare at 8:16]
```

## 3. 验收 (a)：一个变换，两种写法，一个答案

程序是 `x*2 + y*3 + x*y`（再加一个 `x*2`，否则没有乘积相同的一对可比）。两种写法：两条语句换位、每个局部改名。变换 `greedy::fuse` 用 Camel 写，`nodes` 顺序上相邻的一对乘法若算同一个乘积就并成一个节点；`greedy::fuse_by_creation` 是同一段配对逻辑改读 `creation_id`（它声明了 `spelling`，因为读的是写法）。

```
canonical by nodes     a=6c9b46b309122b72bfea13fc2a0e7ba822a2789959f4743a1bced1941c42fd4a b=6c9b46b309122b72bfea13fc2a0e7ba822a2789959f4743a1bced1941c42fd4a same_graph=True
canonical by creation  a=c431a56c70457f27b1b3a68515561b7fb23e7a428474f2c95086de49d0beced7 b=6c9b46b309122b72bfea13fc2a0e7ba822a2789959f4743a1bced1941c42fd4a same_graph=False
```

第一行是验收要的：两种写法过同一个贪心融合，**一个规范形态**（`std::jgir canonical=true` 逐字节比较，两个哈希都印出来）。第二行是对照：同一段配对逻辑读 `creation_id`（写法本身）时，两种写法分开 —— 这正是能力声明要拦住的那种依赖。

### torch.fx 的对照（同一台机器，torch 真的在）

`test/cases/trans/spelling/torch_contrast.py`（不是计划条目，理由和 `test/cases/opt/writings/fx_greedy_fusion.py` 一样：它需要一个带 torch 的 Python）。宿主机 `zjwei` 上**有** torch —— 用计划条目会用的同一个解释器，探针答 `torch 2.14.1+cu130`。所以 `test/cases/opt/writings/fx_greedy_fusion.py` 那句「the build host does not have it」在这台机器上不成立。

同一个程序、同两种写法，`torch.fx.symbolic_trace` 之后按节点顺序做同一个贪心配对：

```
torch 2.14.1+cu130
--- writing A: 7 operation(s) in the order it was written ---
  the greedy pairing fuses 1 pair(s): [('mul', 'mul_1')]
--- writing B: 7 operation(s) in the order it was written ---
  the greedy pairing fuses 0 pair(s)
the two writings trace to one graph, text and all: False
the two writings give the greedy pairing one answer: False
```

A 写法里两个 `x*2` 相邻 ⇒ 并一对；B 写法在它们之间插了一条 `y*3`（同一段程序）⇒ 一对都并不了。图的文字也不同。这就是原则 9.5 说的那件事：默认顺序若是写的顺序，变换的结果就带着写法。

## 4. 编译期反例（逐字）

`test/cases/trans/spelling/undeclared.cml`：`entry_name` 读 `name`，`main` 调它，两个正文都没声明。

```
$ camel undeclared.cml
undeclared.cml:7:1: [Error]: This body reads the spelling of the program, and its type does not declare it. [the first reading of the spelling in this body: it is what the type would have to declare at 8:16]
exit 1        (code=0x030001D5 SpellingNotDeclared)
```

同一个程序把 `spelling` 写进两个子句（`declared.cml`）就编译并运行，印出它读到的名字 `main`。这两条现在是计划里的正式用例（`trans.spelling`），不是只写在脚本里的字符串 —— `catalog.pinned_coverage` 只认「有用例按编号钉住」，所以反例必须是一个真用例。

## 5. 读数

### 六个计划（树 = `73b692621`，提交干净，`BUILD_EXIT=0`）

```
[SUMMARY] total: 5393, pass: 5371, fail: 0, skip: 2, known failures: 20
[TREE] clean: the run left nothing under the checkout (git status --porcelain -u normal: 0 before, 0 after, 824 directories watched)
```

六个计划（`compile`、`linear`、`opt`、`parse`、`para`、`tools`）一起跑，**fail 0**：作者给的新基线目标是 `5393 / 5371`，逐字对上。`skip: 2` 与 `known failures: 20` 和基线同一批（e9 的 268/268 在 `opt` 里照旧）。上一轮在这条车道上还是 `5385 / 5356 / 7`，那 7 条逐条归属见 §6 与 §7。

### feat/modules 整目录（树 = `b82b3b5ee`）

```
[SUMMARY] total: 7459, pass: 7421, fail: 2, skip: 9, known failures: 27
```

这一跑比 HEAD 少一个文件（`test/cases/trans/spelling/torch_contrast.py`，没有计划条目读它），其余相同。

两条红是**基线上本来就红的**，见下一节。

### 两条基线红（不是本车道的）

`origin/develop`（`2e1e77324`）本身、不带本车道任何改动：

```
BUILD_EXIT=0
[FAIL] core                             132.74 ms
[FAIL] array_sort_kernels.paths         129.46 ms
[SUMMARY] total: 14, pass: 12, fail: 2
```

根因写在 `docs/handover/questions/2026-11-18-an-in-place-call-now-builds-as-ssa-rebinding.cn.md`：`v = v->sort` 现在被 `Builder::ssaAssignmentOf` 接走，`inPlaceRebindingOf` 永远轮不到，`inPlaceCalls_` 为空，in-place 形式不再被选中。搜索范围 `b62b76de5..2e1e77324`，默认归执行计划那条线（F/H）。

## 6. 默认顺序变了，动到哪些用例（一条一条）

「只把数字改掉」是不允许的，所以每条都写清三件事：现在断言什么、旧值当初为什么对、不改为什么错。

| 计划 / 用例 | 现在断言 | 旧值当初为什么对 | 不改为什么错 |
|---|---|---|---|
| `feat.modules.graph#node_kinds_match_the_graph` | 八个图各自的 kind 列表，按 `nodes` 给的顺序；其中三个（`pick`、`kinds`、`main`）换了次序，值与顺序都取自运行输出 | 那时 `nodes` 给的是创建顺序，`pick` 的 `join` 写在两条操作之前 | 旧值要求创建顺序回来，而创建顺序正是 9.5 要拿掉的东西；值本身是从 fail 产物里取的整行 727 字节，不是手改 |
| `feat.modules.graph#facts_of_graphs_and_nodes` | `[f32 [?], int [?], f32 [2, 3]]` | 创建顺序里 `int` 那个张量先写 | 同上：三个事实没变，顺序是走法自己的 |
| `feat.compile.native_declarations#native.graph_declaration_import` | 末行 `int` | 那时第一个节点是图自己的入口，类型名是 `main` 的签名 `() => int uses var stdout` | 旧值把「第一个节点是入口」写死了；现在第一个节点是它 `return` 的字面量，类型名就是返回类型 |
| `feat.compile.native_declarations#native.graph_constant_import` | 六个 `true`（节点按 kind 选，不按位置） | 那时第一个节点是参数口，`constant::<T>` 对它都答 null | 现在第一个节点是 `int` 常量，六个里有一个答 false —— 那条读数测的是走法从哪儿开始，不是 `constant` 的事实 |
| `test/cases/compile/type_identity/identity.mjs`（`compile.type_identity` 的语料脚本） | `firstType` 改成在一张图的**参数口**上取 `type_of` | 它读 `type_of(nodes(g)[0])`，那时第一个节点就是图自己创建的第一个参数口 | 规范顺序下两张「写得一样」的图第一个节点是同一个节点 ⇒ 探针拿一个类型和自己比，17 对「不是一种类型」全被判成一种。改成参数口：`first(x: a, y: a)` 两个参数都是被测类型，哪个口在前都不影响 |

`type_identity` 这一条要单独说清「改成什么了」，因为它是又一次把位置写死：**不再是「第 0 个节点」，而是「按 kind 选出的参数口」**。语料里两个探针函数把被测类型写成两个参数，所以任一参数口都答同一个类型；顺序再怎么变都不动。跑完 `188 checks, 17 that fail` → 0。

## 7. 没做完 / 停手的地方（诚实清单）

1. `nodes` 把**不可达的节点排在最后**（规范走法只走得到的部分在规范编号里；不可达节点接在后面，按旧编号）。9.5 只要求「两种写法同一个顺序」，这一条满足；把它也纳入规范编号是下一步的事。
2. `id` 对**草稿态**（未提交事务里的节点）仍是草稿位置，不是最终规范编号 —— 提交前它没有规范位置可言。
3. `graphs_of` 仍按 `GraphId` 顺序返回（没有改）。它不在 9.5 列的「读写法」清单里，但谁读 `graphs_of(...)[0]` 仍然是在读一个创建顺序；这是留给下一轮的问题。
4. **lambda 没有地方写子句**：一个 lambda 交给一个「位置给定的函数类型」时，那个类型上写不了 `spelling`；现在 lambda 正文按 5.9 豁免，所以读写法不会报错。这是一个已知的洞，没有解决。
5. `x*2 + y*3 + x*y` 本身**没有乘积相同的一对**，所以验收程序加了一个 `x*2`；报告里说的「两种写法」是这五行程序，不是三行。
6. `sem::covers` **没有**加拼写规则。加过一次，`canonical_pairing` 反而红了（run14），而且 in-place 那条一点没变；所以它被拿掉了，理由是「它解决不了任何事，还引入一条新规则」。
7. in-place / SSA 那条红**不是本车道的**（§5.3 的证据），本车道没有修，只把它写成了一页问句。

## 8. 提交

```
93 files changed, 1409 insertions(+), 363 deletions(-)     (git diff --numstat origin/develop..HEAD)
  code    (src, include)   15 files  +215  -21
  modules (runtime/graph)   3 files  +155   -5
  stdlib                    2 files   +48  -48
  scripts                   5 files   +39   -5
  tools (formatter)         1 file    +21   -3
  cases                    55 files  +685 -202
  plans                     4 files  +109  -59
  docs                      8 files  +137  -20
```

十笔（都在 `lane-spell` 上，未 push；本报告是第十一笔）：

| 提交 | 内容 |
|---|---|
| `184b08b70` | 丙的本体：能力声明、规范顺序、50 个 `.cml` 迁移 |
| `c1e4da329` | merge `origin/develop`（`2e1e77324`）；唯一冲突 `docs/diagnostics.md` 计数行，取 `325/303/22/15/310` |
| `a5bca2b96` | `stdlib-reference` 的示例记录（4 条读写法） |
| `50f2aacec` | 六计划找出来的四处产物：formatter、grammar 引用、catalog 计数、`type_identity` 语料 |
| `2ac915833` | AST 记录重录（54 增 51 删，删的正好是本笔改的文件） |
| `6e435fb34` | 一页问句：in-place 走 SSA 重新绑定 |
| `55e74c5be` | 五处读数（两条顺序断言 + 两条 native 用例 + vscode 语法 + 示例标记） |
| `54c3866c0` | `node_kinds_match_the_graph` 整行重钉（取自运行输出） |
| `b82b3b5ee` | 三行 AST（doc 标记与两个用例） |
| `73b692621` | torch.fx 对照 |

## 9. 尚存的、不属于这条车道的读数

| 读数 | 位置 | 归属 |
|---|---|---|
| `total: 14, pass: 12, fail: 2`（`core`、`array_sort_kernels.paths`） | `origin/develop` = `2e1e77324`，无本车道改动 | 执行计划那条线（F/H），见那一页问句 |
| `torch 2.14.1+cu130` 在宿主机上可导入 | `zjwei`，计划条目会用的同一个解释器 | 与 `test/cases/opt/writings/fx_greedy_fusion.py` 的注释「the build host does not have it」相反，值得那条线改注释 |

本报告自身是最后一个提交（只有 `docs/`），上面所有读数取自它的前一个提交 `73b692621`。