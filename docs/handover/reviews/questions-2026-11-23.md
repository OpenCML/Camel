# 决策者与施工方的往来（2026-11-23）

一天一页，只记当天的派工、催办、返工和答复；每条写短，细节放到施工计划、报告或问题页里，这里只留指向。车道状态以 `docs/handover/lanes.cn.md` 为准，不在本页重复。旧的长页 `questions-2026-10-29.md` 已封存。

## 当前在等的（滚动）

- 前端改为生成 OPER（`lane/stage2-frontend-oper`）、五样分析挪进事实框架（`lane/stage3-facts`）两条车道开出来，并在车道表加行（00:05 派、00:30 催）。
- `GCNodeKind::{Cast,Fill,Accs}` 引用 144→146，变换车道查明是新增还是挪动。
- 根目录剩余：`ROADMAP.md`、`REPRODUCE.md`、`exemplars/`、`promo/` → `internal/`；删 `export_matrix.md`。
