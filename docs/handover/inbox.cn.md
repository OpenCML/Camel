# 给协调者的待办（滚动）

决策者写，协调者读。规则：
- 每条一个编号（`D-日期-序号`），写清要做什么和完成标志，两三行；细节在施工计划或报告里，这里只给指向。
- **协调者接手一条，就把这一条删掉**（转进车道表 `lanes.cn.md` 或自己的计划），并在提交说明里写"接手 D-…"。不整页清空，以免删掉还没读的新条目。
- 本页只放要做的事。裁定、设计结论写进设计原则、设计稿或问题页，不只留在这里。历史以 git 记录为准。
- 协调者要回给决策者的话（完成、卡住、要裁定）写在问题页或报告里，不写进本页。

## 待接手

- **D-1123-1** 开 `lane/stage2-frontend-oper`：前端改为生成 OPER，不再产出 Cast/Fill/Accs 节点（四个算子已在 `src/stdlib` 声明，`801e22a43`）。完成：前端不再产出这三种节点。
- **D-1123-2** 开 `lane/stage3-facts`：五样分析（效应汇总、使用索引、执行计划、`ControlRegions` 的计算、写法提取）挪进事实框架，原缓存删除；33 处绕过事实层的纯度判断改问事实（`docs/handover/plans/2026-11-22-refactor-stage3-graph-storage.cn.md` 3b）。
- **D-1123-3** 变换车道查明 `GCNodeKind::{Cast,Fill,Accs}` 引用 144→146 是新增还是挪动；迁读者只许让这个数减少。
- **D-1123-4** 根目录剩余：`ROADMAP.md`、`REPRODUCE.md`、`exemplars/`、`promo/` → `internal/`；删 `export_matrix.md`（`docs/handover/plans/2026-11-22-root-cleanup.cn.md`）。
