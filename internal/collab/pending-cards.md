
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

### D. `sum_to` / `broadcast_like` 的轴身份应当进调用（截稿后，源自 `task-327` 的 (b) 方向，`lane-ssa-residue` 2026-11-28 读数）
- 读数：`x: float[2,2,2]`、`x[n][c][k]=n*4+c*2+k+1` ⇒ `sum_to(x,[2]) = [16,20]`，按通道应为 `[14,22]`；`broadcast_like([10,20], x)` 落在**最后轴**，按通道应沿 `C` 展开。无报错、量级合理 ⇒ **静默错数**
- 根因：`sum_to`/`broadcast_like` 是**后缀对齐**的，而"按通道（轴 1）"在任意秩上需要与秩无关的写法；`I == C` 时形状检查完全通过，所以错数不可能被形状发现
- 本卡已先做 (a)：目标可多重对齐时**拒绝并点名候选轴**（拒绝可以，错数不行）；(b) 是把轴显式化（`sum_to(t, at, axis=…)`）
- (b) 的代价与边界：改库 API，涉及 `src/stdlib/rulesets/jvp.cml`、`transpose.cml` 与**引擎 vjp 调用点**，且与 `task-322`（Float32 分配）相邻 ⇒ 截稿后单独开卡，不并进 `task-327`
- 判据草案：(b) 落地后 `jvp` 与有限差分在 rank 2/3/4 上逐值一致，且旧签名委派新签名（不破坏既有用例）
- **更正（`lane-ssa-residue` 2026-11-28 读源）**：`task-327` 卡里那处 `conv.cpp:656` 不是 conv —— 它是 `bnTrainGeometry`（batch-norm 的 N/C/inner 几何：`C = dim(1)`、`d>=2` 乘进 `inner`、`p->dim(0)==C` 才过）。`conv.cpp` 的四个入口（`:213 :529 :551 :575 :612`）都要求 `rank == 4`，所以 conv 属"秩被钉死、不可能静默错数"
- **同为疑似（待实测，`batch_norm`）**：参数是 `[C]`（只有长度、没有轴身份）；rank 3 且 `I == C` 时长度与轴 1、轴 2 同时对得上 ⇒ `[N,I,C]` 布局的调用者形状全过、统计量却按轴 1 算。构造 `[2,2,2]` + `[2]` 参数、手算"按轴 1"与"按轴 2"两组统计量即可判定；若成立，它与 `sum_to` 是同一类

### E. 离散参数在转置构出侧的第四处组装点（`task-301` 收口后剩下 2 名，cap-probe 2026-11-28 交回）
- 起点 `54ccaacc55`（`lane/arg-parse-uaf-w`，工作树 `witness`）；两名红：`grad_array_with_int.fvm`、`grad_array_int_parameter_first.nvm`
- 卡点原文："声明侧（类型法则）现在说 `(float[], int)`，转置构出侧仍是 `(float[], ())` ⇒ `vjp.cml:132` 报「返回类型 `(float, (float) => (float[], ()))` 与声明的 `(float, (ct: float) => (float[], int))` 不符」"
- **四处组装点，必须一起改才闭合**：① `transpose/signature.cpp:199` 参与位成员（机械 `space.space` → `gradientTypeOf(parameter)`）；② `transpose/walk.cpp` 出口组装（原 4114–4293，加 `answerTypeOf`/`answerZeroOf`，离散位用 `discreteZeroSlot` 物化同型零）；③ `split` 提升处把该位当 `LiftedPlace::Value`；④ 提升法则给该位声明的答案形状
- 实测："只改前两处时构出侧仍是 `()`" ⇒ 第三/第四处之一才是产出 `()` 的那一处；下一人应从 ③④ 起手
- 判据：两名转绿 + `smoke + feat/trans + feat/compile` 对 `2821 / 18` 逐名新增红 0（起点基线）

### F. `tangentSpaceOf` 的用户面与机件面要一起走（`D-1127-278` 执行记录，截稿后整理）
- 已落地：`Tangent<T>` 走用户面（`Tangent<bool> = bool`、`Tangent<int> = int`）、`jvp.cml:43` 例子改 `(int, int)`、聚合恒等（`54ccaacc55`：全成员都是自己的切空间时交回 `primal`）
- 仍存：`tangentSpaceOf` 在机件内部的零节点策略（"内部聚合只带真正需要梯度的成员"）与用户面口径在注释里未成文 ⇒ 截稿后把原则 9.9 的这段话写进 `derivative.h` 注释，别让下一个人再从测试反推
