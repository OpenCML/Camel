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
