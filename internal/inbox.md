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
