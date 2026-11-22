# 给协调者的待办（滚动）

决策者写，协调者读；接手一条删一条，同一编号转进 `backlog.md`。规则见 `internal/README.md`。

## 待接手

- **D-1123-1** 开 `lane/stage2-frontend-oper`：前端改为生成 OPER，不再产出 Cast/Fill/Accs 节点（四个算子已在 `src/stdlib` 声明，`801e22a43`）。完成：前端不再产出这三种节点。
- **D-1123-2** 开 `lane/stage3-facts`：五样分析（效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取）挪进事实框架，原缓存删除；33 处绕过事实层的纯度判断改问事实（`internal/plans/2026-11-22-refactor-stage3-graph-storage.cn.md` 3b）。
- **D-1123-3** 变换车道查明 `GCNodeKind::{Cast,Fill,Accs}` 引用 144→146 是新增还是挪动；迁读者只许让这个数减少。
- **D-1123-4** 根目录剩余：`promo/` → `internal/promo/`（`ROADMAP.md`、`REPRODUCE.md`、`exemplars/`、`export_matrix.*` 已处理）。
- **D-1123-5** 协作体系重组已由决策者做完（`docs/handover/` → `internal/`，见 `internal/README.md`）。合入 develop 前跑一遍本地闸门；之后所有协作文件只写在 `internal/`。
- **D-1123-6** 把 `internal/archive/` 里 `pending-work.cn.md`、`pending-work-status.cn.md`、`red-baseline.cn.md`、`held-branches.cn.md` 中没做完的条目，以 `C-` 编号转进 `backlog.md`；本 inbox 的 D-1123-1 到 -4 接手后也进 backlog。完成：backlog 是协调者手上活的唯一清单。
- **D-1123-7** 44 个 `.cml` 用例的注释里还写着 `docs/handover/…`（决策者没改，因为改注释会移动 AST 记录）：去掉这些交接路径（按"代码里不留过程痕迹"，用例注释只写测什么与为什么），并重录 AST 记录。完成：`git grep docs/handover -- '*.cml'` 为空。
- **D-1123-8** 提交检查补两项（设计原则第 1 节红线 1、4、6，负责人 2026-11-23，与 ECOOP 稿的提交条件一致）：同一区域里两个冲突节点（访问同一资源、至少一个是写）之间必须有先后，遍可以颠倒、不能让它们无序；函数体访问的、图没绑定的资源必须写在签名的效应里。先核对现有提交校验做到了哪些，缺的补上，各配一条用例（删掉唯一的排序边被拒、颠倒顺序被接受）。归到图·写车道的发布检查里一起做。
- **D-1123-9** 协调者自己的工作流说明（问题页里引过的 "SKILL §0 闸门"）不在仓库里，决策者看不到。请把其中关于车道、闸门、派工、合并的规矩并进 `agents/dispatch.md`（只追加），以后改流程只改仓库里这一份，并与 `internal/README.md` 的流转一致；SKILL 里只留指向它的一句。
- **D-1123-10（先做）** 把 fsh-dev 整枝合进 develop：其中有协作体系的搬迁（`docs/handover/` → `internal/`）与设计原则红线 1、4、6 的更新，本 inbox 的条目也要合进去你才能按 develop 读到。合完后各车道分支先合 develop 再继续；之后协作文件只写在 `internal/`。
- **D-1123-11** 记号三处改动（设计原则 4.4 与 9.12，负责人 2026-11-23）：① lambda 改写成 `func (参数) { … }`，旧写法 `(x) => e` 直接删除、不留诊断；花括号永远是函数体，最后一条语句是值。② `view`/`edit` 块头可写 `=> 类型`。③ `=>` 只留在函数类型、块头类型、`case`、规则。`.cml` 约 1,300 处、语言参考（2.6 运算符表、6.9、第 8 章语法、7.6）、教程、示例、格式化器、LSP、语法高亮、AST 记录同批改。完成：`.cml` 里不再有 `(…) =>` 形式的 lambda，`camel-fmt` 输出 `func (x) { … }`。
- **D-1123-12** 记号再统一四处（设计原则 4.4、4.2 区间、5.9，负责人 2026-11-23），与 D-1123-11 同一批改、`.cml` 只动一遍：① 返回类型一律 `=> 类型`（`func f(x: int) => int { … }`、`func (x) => int { … }`），`:` 不再写返回类型；② 切片改用区间写法 `t[a..b]`、`t[..b]`、`t[a..]`，带步长写 `t[range(a, b, s)]`；③ 按名字给的类型实参写 `f::<T: int>`；④ `static for … do …`（不再用 `=>`）。旧写法直接删除、不留诊断；语言参考、教程、格式化器、LSP、语法高亮、AST 记录同批改。
