
## 待建卡（任务板满 256，腾出槽位即建）

### A. `liftedLoopBodyResult` 读源图的错 + 多态体成套三处口径统一（源自 `task-344` 收敛失败，cap-probe 交回 2026-11-28）
- 读图错：`liftedLoopBodyResult` 里 `nodeOf(function, exitCopy)` 用**源图**查**本 draft** 的节点号；改成 `draft.header(exitCopy)->dataType` 只把断点移动，不解决问题
- 不成套的三处口径：`liftedLoopValueType`（节点类型）/ Fill 格的成员抬法 / `liftedLoopBodyResult`（体结果拼法）
- 探针读数：`states=3 coll=0 loopkind=0 exit=30 et=Tuple esz=3 lvt=1 copy=1 tan=0`、`LBR_STOP pairNeedsTangent j=2 copyHasPairs=0`
- 判据：`jvp_loop.struct_state_is_lifted` 与区间循环那条**两路同时绿**；`jvp_of_identity` 边不变量 0 处
- 门：等 `task-344` 压笔交回、`lane/jvp-loop` 合入后再开（否则同文件两处改会打架）

### B. `.remote-runs` 只留最新 20 对（`task-364` 交回时报出，资源决策归决策者，已 oversight_tell，等裁）
- 默认做法：上限不动，裁剪时把被删那份的 `[SUMMARY]` 行与 `__CAMEL_REMOTE_EXIT__` 追加进 `~/camel-work/.remote-runs/index.log`

### C. jvp 值投影：先打 region 表，再写投影（源自 `task-365` 第二档，lane-arrangeable 交回 `13d6503372` 后按规矩撤案，补丁在 `D:\Temp\t365-projection-attempt.patch`，142 行）
- 已到的一档：`13d6503372` 让"条件读状态"的循环不再建不自洽的图，改成具名拒绝（`skip=loop`、`cause":"loop"`）——走的是 `jvp.cpp:2944-2948` **原有**的 `RefusalCause::Loop`（提升被判 `complete=false`），**不是**"条件 region 建好了"（这是它自己更正的一处归因错误）
- 没做到：值投影两次都没命中；`CONDDIAG` 一行未打印 ⇒ `LoopCond` 分支对那条程序根本没执行；`function.region(1)` 的 kind/owner 不明
- 下一步：`if (!complete)` 之前把**每个 region 的 kind / owner / index / portCount** 不经任何过滤打一行；并 grep `liftedShapeOf`/`pairTypeOf`/`mixedMemberTypeOf` 看"对"的类型由谁拼（`TupleCode::Tuple` 判一次没命中）
- 判据：`jvp` 对 `f(x: float, n: int) = loop (i,a)=(0,x) while i<n do (i+1, a*2.0)` 给 `value=8.0 tangent=8.0`（x=1,n=3），与中心差分 `h=1e-5`、解析 `2^3=8` 三者一致；两个分母 218（209/8/1）、72（44/5/23）新增红 0
- API 形状三条：临时 `std::span` 绑局部；`camel::core::type::tree::arity` 需 include 且会被同名局部遮蔽；**`DraftNodeHeader::dataType` 是字段不是方法**
