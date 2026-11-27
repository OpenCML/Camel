
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

### G. `batch_norm` 的 `[C]` 参数在 rank ≥ 3 时指不出是哪一根轴（第三条静默错数，`lane-ssa-residue` 2026-11-28 实测）
- 程序：`x: f32[2,2,2]`（按 `[N,I,C]` 布局，`I == C == 2`）、`mean=[1,101]`、`var=[1,1]`、`gamma=[1,1]`、`beta=[0,0]`、`eps=0`
- 实际：`[[[0,99],[-99,99]],[[2,299],[-97,299]]]` = **按轴 1 算**（契约读法），逐值与手算一致
- 按调用者布局（channels 在轴 2）：`[[[0,-1],[1,99]],[[2,199],[3,299]]]` ⇒ **8 个元素里 6 个不同**，形状全过、无任何诊断
- 定性：与 `sum_to`/`broadcast_like` 同类——**参数里只有长度、没有轴身份**；`bnTrainGeometry` 的"`d>=2` 乘进 `inner`"是轴 1 契约的**定义**，本身没错
- 修法（已派）：`rank >= 3` 且参数长度与多于一根轴相等 ⇒ 拒绝并点名候选轴（`axes 1 or 2`），用既有 `TensorDimensionMismatch`；`rank == 2` 不动
- 判据：探针改拒绝（逐字+位置+exit code+不 abort）、rank-2 契约读法逐值不变、`feat/modules/nn`+`nn_laws` 新增红 0、`vjp-parity --strict` PASS

### H. `layer_norm` 的 `gamma/beta` 只核"尾随 extents"，不核"该尾随是否唯一"（疑似第四条静默错数，`lane-ssa-residue` 2026-11-28 读源，**未实测**）
- 依据：`signatures/tensor.cml:340-345` "whose extents are the trailing ones of x, over the trailing axes"；`@fails` 只覆盖 "gamma or beta does not match the trailing extents"（`:343`、`:350`）⇒ **只核尾随，不核是否唯一**
- 疑似错数：`x = [2,3,2]`、`gamma=[2]` ⇒ "尾随一轴（轴 2）"与"轴 0"都对得上；调用者通道轴不在尾随时，形状全过、按尾随轴算 ⇒ 与 `sum_to`/`batch_norm` 同族（"参数里装不下轴身份"）
- 下一步：实测两组手算值（按尾随轴 vs 按轴 0）；若确认 ⇒ 同判据 `rank>=3` 且多数组合法 ⇒ 拒绝并点名候选轴
- **分类备注（`lane-ssa-residue` 提，我认）**：只有"参数里**装不下**轴身份"的才立卡（`sum_to`/`broadcast_like`/`batch_norm`/`layer_norm`）；"参数里装得下但被省略"的（`softmax`/`log_softmax` 的 `axis=-1` 默认）**只写文档提醒、不立卡**
- **已闭合**：`sum_to` 的静默窗口只剩 rank≥3 且多数相等那一个；`[3,2,2]` 上 `[3]` 已被 `requireSuffixTarget` 逐位比较挡掉（`position 2 is 3 where the tensor has 2`），走不到"落最后轴"

### F. `tangentSpaceOf` 的用户面与机件面要一起走（`D-1127-278` 执行记录，截稿后整理）
- 已落地：`Tangent<T>` 走用户面（`Tangent<bool> = bool`、`Tangent<int> = int`）、`jvp.cml:43` 例子改 `(int, int)`、聚合恒等（`54ccaacc55`：全成员都是自己的切空间时交回 `primal`）
- 仍存：`tangentSpaceOf` 在机件内部的零节点策略（"内部聚合只带真正需要梯度的成员"）与用户面口径在注释里未成文 ⇒ 截稿后把原则 9.9 的这段话写进 `derivative.h` 注释，别让下一个人再从测试反推

### J. 后缀 `!` 拆 `T!`：文档说支持、实现连最小样本都拒绝（语法/解析层，与 C2 无关；已报决策者 2026-11-28 01:5x）
- 文档（`docs/design-principles.cn.md`）：`:58` "`x!`，`x` 是 `T?`／`T!` ⇒ …另一臂是拆开的值"；`:228`/`:230` 把"后缀 `!`"与 `??`、`!!`、`match` 固定模式并列为拆包手段
- 树里取证（`lane-editview-a`）：`git grep` 命中**全在类型位置**（`=> int!`、`x: int!`），`(let|return|=) … )!` 的**拆值用法 0 命中** ⇒ 无现成样本可照抄
- 最小探针（不依赖任何算子）：`func one() => int! { return 1 }` + `let x = one()!` ⇒ `t205_suffix_bang.cml:7:18 No matching function found for argument types: <> (string); operator '!' is not defined for these operand types`
- 同形报错在四种自然写法里重复（`let h = g!`、`match g { case ok(h) => … }`、`…(f)!(3)`、`one()!`）；其中 `let h = g!` 会**吃掉下一行的 `let`** ⇒ `!` 被解析成作用在后续 token 上的算子调用
- 影响：`task-205`（C1→C2）的程序面 `c2.by_program` 因此红；命令行侧 PASS 且与 `8ce4bd4e4e` 金标逐字节相同
- 未试的一条线索：文档 `:58` 说"与 `!! fail` 是同一种结构" ⇒ **前缀 `!!`** 可能是真正的拆包形态（T5 `let x = !!one()`）；若成立则本缺口是文档措辞问题（文档归协调者改），否则按语法缺口立卡
- 处置：`task-205` 四笔**不进合并队列**（带红进去就是新红），等这条定论

### J. 后缀 `!` 拆 `T!` —— **撤回：不是缺口，是底旧了**（决策者 `D-1128-2` 裁定，2026-11-28 01:5x）
- 决策者：后缀 `!` 是设计支持的语法、文档不改；`lane/bang-suffix` 23:45 已合入 integration（`4f8db392c`；解析 `7e9b30d70`、降级 `e3adf1b46`、文档 `f258e756d`），用例 `test/cases/parse/syntax/postfix_bang.cml`、`test/cases/consistency/unwrap/take_postfix.cml`
- `task-205` 那棵树的底（`8ce4bd4e4e`）早于 23:45 ⇒ 树里没有该语法；`!!` 不是拆包符（文档里 `!! fail` 是另一处用法）
- 因此本轮读数（`one()!` 报 `operator '!' is not defined`、`!!one()` 报 `Unexpected '!!'`）**不是缺陷**
- **教训（要进 skill）**：报"语法/特性不支持"之前，先 `git merge-base --is-ancestor <那次合入的提交> HEAD` 核自己的底；底旧时所有"实现不认"的读数都不成立
- 后续：换底后重跑最小探针与 `c2.by_program`；若仍报同一句，才是缺陷

### E. 第四装配点：改判 —— **阻塞在类型律层，且需要一个类型真值手段**（cap-probe 交回，2026-11-28 02:0x）
- **判据修正**：两个 E 卡用例在 HEAD 上**本来就绿**（`grad_array_with_int.fvm`、`grad_array_int_parameter_first.nvm`）⇒ 此卡是"落 甲 时别把它们弄红"，不是"修红"
- **甲 的阻塞逐字**：`vjp.cml:132` 声明 `(float, (ct: float) => (float[], int))`，构出 `(float, (float) => (float[], ()))`
- **四组阴性（各自单独上机，构出侧字节不变 ⇒ `()` 不是这四处产出）**：`pullback.cpp#planFor` 走用户切向｜`split.cpp#valuePortsOf` 换用户律｜`split.cpp#splitHalvesWith` 离散槽用 `gradientTypeOf`｜`jvp.cpp#liftSignature` 切向半用 `gradientTypeOf`
- **最可能落点**：`.cml` 类型律层（`types.cml:246 Jvp<F>`、`:268 Transpose<F>`；`split_operator.cpp#splitOperatorSignature` 把 `Split` 的答案声明成 `halves.forward`）；`vjp` 是 `#macro`，`lin` 拿的是**声明类型**
- **下一手**：纯 `.cml` 探针绕开 `vjp` 宏，直接实例化那两条类型律，看切向半是 `int` 还是 `()`；三支判读（律里就是 `()` ⇒ 改律｜律里是 `int` 而宏里是 `()` ⇒ 宏声明层｜探针写不出该形状 ⇒ 报"设计上不可达"）
- **取类型的手法**：把值传给形参、让 `NoMatchingFunction` 打印真实类型（沿用 T1 的做法），**不新造工具、不改 plan 断言**
- **已记的坑**：把 `lift.cpp` 的**普通**参数读法也改成"`int` ⇒ `Tangent` 位"会一次打红 **59** 个 transforms 用例；只能动"提升签名的切向半"

### I. onnx G2：8 条 refusal 位置重录（撤销作废的残影）—— 已派 `lane-backend-speed`（`lane/onnx-g2` 自 `ba5a45e9bf`）
- 判据：8 条绿到断言层；`export_impure_op`、`as_refusal.constant_fails.pass/.operator`（丙）不变；报 153 六数 + 对基线 `9c6f270c87` onnx 6 行的逐名差集；每条贴旧壳/新壳两段原文并排
- 边界：只动 `test/cases/modules/onnx/refusals/` 8 条 case 与计划里那几条 entry；不碰 `export_*.cml`、不碰 `src/`

### J. onnx 他族 12 条：只读分诊 —— 已派 `lane-witness`（用 `Camel-wt\witness` 树，`ba5a45e9bf` 上只读）
- 12 条逐名：`ort_core_slice`、`ort_var_parameter`、`ort_const_under_as`、`ort_duplicate_outputs`、`ort_struct_casts`、`example_refusal.tuple.pass`、`example_refusal.shape.pass`、`typed_constant_ends_unrolling.pass`、`function_branch.pass`、`as_refusal.unproven.pass`、`as_refusal.always_fails.pass`、`resnetbn_train_step_export`(timeout)
- 每条要：第一条诊断原文｜归类（src 缺陷/断言过时/超时/未迁移）｜是否在基线 6 行里｜一句话根因

> 更正（02:1x）：**J 改派 `lane-oper-shape-guard`**，排在 G1c → G1b 之后（原定的 `lane-witness` 上下文不足，且其树停在 `bb26b78dda` —— "测试与二进制必须同树"，所以在别树跑 onnx 计划测的不是目标尖端）。
