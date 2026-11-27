
## 待建卡（任务板满 256，腾出槽位即建）

### A. `liftedLoopBodyResult` 读源图的错 + 多态体成套三处口径统一（源自 `task-344` 收敛失败，cap-probe 交回 2026-11-28）
- 读图错：`liftedLoopBodyResult` 里 `nodeOf(function, exitCopy)` 用**源图**查**本 draft** 的节点号；改成 `draft.header(exitCopy)->dataType` 只把断点移动，不解决问题
- 不成套的三处口径：`liftedLoopValueType`（节点类型）/ Fill 格的成员抬法 / `liftedLoopBodyResult`（体结果拼法）
- 探针读数：`states=3 coll=0 loopkind=0 exit=30 et=Tuple esz=3 lvt=1 copy=1 tan=0`、`LBR_STOP pairNeedsTangent j=2 copyHasPairs=0`
- 判据：`jvp_loop.struct_state_is_lifted` 与区间循环那条**两路同时绿**；`jvp_of_identity` 边不变量 0 处
- 门：等 `task-344` 压笔交回、`lane/jvp-loop` 合入后再开（否则同文件两处改会打架）

### B. `.remote-runs` 只留最新 20 对（`task-364` 交回时报出，资源决策归决策者，已 oversight_tell，等裁）
- 默认做法：上限不动，裁剪时把被删那份的 `[SUMMARY]` 行与 `__CAMEL_REMOTE_EXIT__` 追加进 `~/camel-work/.remote-runs/index.log`
