# 给协调者的待办（滚动）

决策者写，协调者读；接手一条删一条，同一编号转进 `backlog.md`。规则见 `internal/README.md`。

## 待接手

- **D-1123-6** 把 `internal/archive/` 里 `pending-work.cn.md`、`pending-work-status.cn.md`、`red-baseline.cn.md`、`held-branches.cn.md` 中没做完的条目，以 `C-` 编号转进 `backlog.md`；本 inbox 的 D-1123-1 到 -4 接手后也进 backlog。完成：backlog 是协调者手上活的唯一清单。
- **D-1123-8** 提交检查补两项（设计原则第 1 节红线 1、4、6，负责人 2026-11-23，与 ECOOP 稿的提交条件一致）：同一区域里两个冲突节点（访问同一资源、至少一个是写）之间必须有先后，遍可以颠倒、不能让它们无序；函数体访问的、图没绑定的资源必须写在签名的效应里。先核对现有提交校验做到了哪些，缺的补上，各配一条用例（删掉唯一的排序边被拒、颠倒顺序被接受）。归到图·写车道的发布检查里一起做。
- **D-1123-11** 记号三处改动（设计原则 4.4 与 9.12，负责人 2026-11-23）：① lambda 改写成 `func (参数) { … }`，旧写法 `(x) => e` 直接删除、不留诊断；花括号永远是函数体，最后一条语句是值。② `view`/`edit` 块头可写 `=> 类型`。③ `=>` 只留在函数类型、块头类型、`case`、规则。`.cml` 约 1,300 处、语言参考（2.6 运算符表、6.9、第 8 章语法、7.6）、教程、示例、格式化器、LSP、语法高亮、AST 记录同批改。完成：`.cml` 里不再有 `(…) =>` 形式的 lambda，`camel-fmt` 输出 `func (x) { … }`。
- **D-1123-12** 记号再统一六处（含⑤函数组合改为 `f <> g`：两边留空格，禁空尖括号表 `f<>`，删 `f..g` 与 `?..`，优先级与区间同级、比 `->` 松，设计原则 4.2；⑥算子 URI 一律 `模块:路径`，`:op/add` 写成 `std:op/add`，登记时检查冒号前的模块，诊断与调试器显示 Camel 名字而不是 URI，设计原则 9.3）（设计原则 4.4、4.2 区间、5.9，负责人 2026-11-23），与 D-1123-11 同一批改、`.cml` 只动一遍：① 返回类型一律 `=> 类型`（`func f(x: int) => int { … }`、`func (x) => int { … }`），`:` 不再写返回类型；② 切片改用区间写法 `t[a..b]`、`t[..b]`、`t[a..]`，带步长写 `t[range(a, b, s)]`；③ 按名字给的类型实参写 `f::<T: int>`；④ `static for … do …`（不再用 `=>`）。旧写法直接删除、不留诊断；语言参考、教程、格式化器、LSP、语法高亮、AST 记录同批改。
- **D-1123-13** 空格规则（设计原则 4.1，负责人 2026-11-23）：紧贴的一律比留空格的绑得紧，乘方是唯一例外。① `camel-fmt` 按此排空格（`x->f` 紧贴、`f <> g` 留空格），对全部 `.cml` 跑一遍，与 D-1123-11/12 同批；② 语言参考 6.12 优先级表加一列"写法：紧贴/留空格"，并核对表与原则一致（`<>` 与区间同级、比 `->` 松）；③ 加一条检查：格式化后 `git diff` 为空。
- **D-1123-17（已做）** 车道表每行已带 backlog 编号，条目已标 `doing`
- **D-1123-19（已开五条：commit-checks / notation / panic-record / 3a-transforms / 3a-exec）** 见 backlog 的 `doing` 行
  1. `lane/commit-checks`：D-1123-8，提交检查补"冲突访问有先后"与"未绑定资源在签名效应里"。
  2. `lane/notation`：D-1123-11/12/13 的记号改动（若 `lane/data-arg-migration` 就是这件，用它，并把它写进车道表——车道表里现在没有这一行）。
  3. `lane/panic-record`：`panic` 收尾——余下没写的补上，`graph.cpp` 改为从算子记录读"会不会故障"，附改动前后一项不变的表。
  4. `lane/3a-transforms`：阶段 3a（`internal/plans/2026-11-22-refactor-stage3-graph-storage.cn.md`），`src/stdlib/{transforms,autodiff,rewrite}` 与 `src/kernel/rewrite` 里直接读 `GCGraph`/`GraphDraft` 的地方改读 `GraphView`（`Graph::view()` 已可用）。
  5. `lane/3a-exec`：同上，`src/stdlib/std/{nvm,fvm,prl,cpp,exec}` 与 `src/tools`。
  3a 用一个只减不增的计数守着两个旧类型的直接引用，计数写进每次合入的说明。
- **D-1123-20（紧急，先停）** `lane/data-arg-migration`（把 `with` 参数改成普通参数、803 个 `.cml`、约 1,321 处）**停下，不合入**。它来自决策者对 `fill` 那页的第二段，那段推广错了，已在原页更正（`internal/reviews/…fill-declares-its-fields…`）。`append`、`contains`、`reduce` 的 `init`、`format` 等声明在 `with` 里是对的（负责人的判据：`->` 只传一个值，其余放尖括号便于串成流水线）。只保留"声明与内核不一致"的修正（如 `fill`：按声明修内核），声明与内核一致的不动；backlog 的 `C-1123-8` 按此缩到这一类。问题页 `2026-11-23-a-literal-and-the-position-it-sits-in-decide-a-type-variable` 随之不成立（那四条红是迁移带来的），已在页上答复。
- **D-1123-21（裁 C-1123-19/20，归 `lane/commit-checks`）** ① 签名改窄、不加节点也要查：红线 6 比较的是"体里的访问"与"签名的效应"两边，任何一边变了都要再比一次；改窄后体里已有的访问超出签名，就是越界，拒绝。`commit_refusal.retier` 钉的是旧的弱读法，按原则改用例，不放宽检查。② 遍新建的图同样经提交：新图的全部节点算作碰过的区域，结构、冲突访问的先后、未绑定资源三项照查；先量影响面（多少现有用例因此转红、各是哪类），读数写进车道报告，再合。
- **D-1123-22（车道表停滞）** 车道表有 5 行超过两小时没更新：`stage2-graph`（01:31）、`stage2-frontend-oper`（00:30）、`stage3-facts`（00:30）、`panic-record`（05:25）、`3a-transforms`。另有 `exemplars-path`、`notation-b2`、`commit-checks-b2` 已合入，行还写着在跑。请逐行更新：还在做的写当前读数与下一笔；已合入的标"已合入"；停了的写原因（`stage3-facts` 的 WIP 落后 139，`C-1123-14` 说不能照原样续——要么从 develop 重开，要么收车道）。在跑的真实条数若低于 6，按 D-1123-19 的余项补开。
- **D-1123-23（产出）** 两小时没有一笔代码合进 develop，6 条车道里 4 条压在"翻转"这一摞上（`stack-verify` 叠六层再一起验）。请把能单独站住的先合：每条车道至少每两小时合一笔，或在车道表那一行写明卡在哪、等谁。叠层验证只用来发现问题，修好的那一层各自合，不等整摞全绿。另两条（`stage2-graph`、`stage3-facts`）各报一行当前读数。
- **D-1123-24（开车道：图与事实两条已停，在跑的实为 4 条、都压在翻转上）** 当轮从 develop 头重开四条，旧 WIP 只作参考、不续：
  1. `lane/graph-read-b2`：2.1 收尾——`GraphRead` 余下的使用者迁到视图后删掉 `GraphRead`；`LoopView`/`LoopDraftView`、`decidingRegionOf` 两对合一；`ControlRegions`、`NodeRoleRegistry` 接受视图；一致性检验进快层。
  2. `lane/facts-b2`：先把 `facts.h` 的定义写出来（现在全树零定义），草稿上能挂事实，配用例；再按阶段 3 计划 3b 搬五样分析、清 33 处绕过事实层的纯度判断。
  3. `lane/notation-b3`：记号余项——返回类型写 `=> 类型`、函数组合 `f <> g`（删 `f..g`、`?..`、禁 `f<>`）、按空格规则跑格式化器、按名字的类型实参 `f::<T: int>`、算子 URI 统一为 `模块:路径`（登记时查模块、诊断显示 Camel 名字）。`.cml` 的机械改写仍单独一笔、由合并位排空档。
  4. `lane/3a-b2`：阶段 3a 下一批，变换、重写、执行、工具里直接读 `GCGraph`/`GraphDraft` 的地方继续迁，注意 C-1123-17（视图编号有空号，用 `nodes()` 遍历，不按 `0..nodeCount`）。
- **D-1123-25（催，第二轮；`graph-read-b2` 已合入，余 `notation-b3`、`3a-b2` 两条未开）** 在跑 5 条，其中 4 条压在翻转一摞上，`packed-view-lifetime` 已两小时没更新。D-1123-24 的四条只开了 `facts-b2`，请当轮再开 `graph-read-b2`、`notation-b3`、`3a-b2`；并先把 fsh-dev 整枝合进 develop（领先 17 笔，含 D-1123-20 起的各条与三页问题的答复）。
- **D-1123-26（开车道：在跑只剩 3 条——`3a-b2`、`flip-consumers`、`packed-view-lifetime`）** 当轮开四条，开了在车道表加行、backlog 标 `doing`：
  1. `lane/facts-b3`：阶段 3 计划 3b 的余下部分——效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取五样挪进事实框架，原各自的缓存删除；与已落地的"可自由安排"同一套 kind。
  2. `lane/record-uses`：按问题页 `what-a-read-of-a-cell-costs` 的答复，登记时把 `OperatorTable` 的效应语义作为 C++ 入口的陈述写进 `Record::uses`，事实层只读记录；加一条检查列出两个入口都没说的算子。
  3. `lane/3a-b3`：与 `3a-b2` 分目录并行（如 `src/stdlib/std/{nvm,fvm,prl}` 与 `src/tools`），直接读 `GCGraph`/`GraphDraft` 的引用（现 2,730 处）继续减。
  4. `lane/tooling`：backlog 里的工具欠账一并清——`C-1123-15`（远端只带回一份基准）、`C-1123-16`（构建戳不标识构建）、`C-1123-7`（`-AstOnly` 参数绑定），这三条是"重录后绿、再跑就红"一类问题的根，修了以后各车道的读数才可信。
  另：记号的语料改写（返回类型 `:` → `=>` 等，约 4,248 个文件）等翻转那一摞合入后，由合并位排空档一笔做完，然后删掉旧写法。
- **D-1123-27** 车道表去重后在跑的是 5 条（`tooling`、`flip-consumers`、`facts-arrangeable`、`record-uses`、`onnx-lowering`，前三条各重复了一行）。请删重复行，并补开 D-1123-26 的 `3a-b3`；`C-1123-33`（5 个用例、1 处注释还写 `[a:b]`）顺手在一条车道里改成 `[a..b]`。
- **D-1123-28（在跑只剩 3 条：`onnx-lowering`、`oper-identity-write`、`device-programs`）** 先把 fsh-dev 整枝合进 develop（领先 15 笔，含 D-1123-25 起的派工与三页问题的答复），再当轮开到 6 条以上：`3a-b3`（直接读 `GCGraph`/`GraphDraft` 的引用继续减，分目录）、`facts-b3`（效应汇总、使用索引、执行计划、`ControlRegions`、写法提取五样挪进事实框架）、`slice-cleanup`（`C-1123-33` 的 6 处 `[a:b]`，加 25 个真沉默算子逐个补 `uses`/`pure` 声明）。
- **D-1123-29（在跑 4 条，补到 8 条）** 当轮开四条，开了在车道表加行：
  1. `lane/3a-b3`：直接读 `GCGraph`/`GraphDraft` 的引用继续减，分目录，计数写进合入说明。
  2. `lane/facts-tools`：`camel inspect --facts` 接线、`computeAll` 给它与测试用；每个已落地的 kind 配一条"事实答案与旧算法逐项对照"的用例；调试器 `regionOuter` 改读 `ControlRegions`（问题页 `who-holds-the-facts-store` 的答复）。
  3. `lane/exec-plan-kind`：执行计划作为 `std::exec` 登记的事实 kind，不可变的计划进事实、fvm 的可变状态留在 fvm；删 `GCGraph::execPlan_`（同一页答复的 ③④）。与 `facts-b3` 分文件：`facts-b3` 做内核框架侧，本车道只动 `src/stdlib/std/{exec,nvm,fvm,prl}`。
  4. `lane/cleanup`：`C-1123-33` 剩余的旧切片写法、25 个真沉默算子逐个补 `uses`/`pure` 声明、`:op/` 以外的其余小欠账。
