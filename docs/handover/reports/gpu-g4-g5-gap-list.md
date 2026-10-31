# GPU 支持面：`tiles::census` 的缺口账本

> **量法**：这一页的每一行都按**节点携带的 URI** 量，不按 Operations 表量。表里有一个名字，不等于程序里有节点带那个名字——
> 轴归约一族就是反例：表里写 `tensor:sum_axis`，节点带的是 `array:sum_axis`，于是「表里有」而「程序里没有」。
> 旧的三个数是这样数错的：**旧数按表读，新数按节点读**（见文末「按节点重量」）。

**两种量法**（五行以内）：按表量问的是"我们注册了什么"，按节点量问的是"程序里真的出现了什么"；
两者不等时，**以节点为准**。

**缺口数必须带算式**：只给一个数字的账页，下一轮一定被读错。写法是三段相加、去重后写成一行，
量不到的项写"未量"，不猜：缺口 = A（C2 节点级）+ B（transformer 节点级）+ C（array: 入口里未进表的名字，去重）。今天不等的情形有两类：表里有名字而程序里没有节点带它（	ensor:sum_axis
对只调 rray.sum 的程序），以及程序里有节点而表里没有那条名字（rray:argmax_axis）。

这是 G4 的"缺口为 0 才算完成"（`docs/handover/reports/gpu-g4-g5-plan.md` G4 第 1 条）的唯一账本。数由遍
`tiles::census` 读出：程序可达图里的面内算子（`tensor` 的数组与 `nn` 的参数树）按
`modules/tiles/src/graph_regions.cpp#operations` 那张表分成"有 device 实现"与"缺口"两组，逐条打印名字。

## 怎么取数

两条命令，`std::macro` 写在前面，派生图的算子才算得进来（少了它，`grad`/`with_grad` 还没展开）：

- `camel benchmarks/transformer/model.cml std::macro tiles::census`，要 `CAMEL_PACKAGES=<repo>/benchmarks/common`
  （`model.cml` 从那里导入 `camel_bench`）
- `camel showcase/c2-mlp-mnist/train.cml std::macro tiles::census`

这个遍只读程序与表，不碰设备：`CUDA_VISIBLE_DEVICES=-1` 下读数相同。所以**下面的数与本机有没有空闲卡无关**。

## 两个程序，两组数

`transformer_step`：面内 22 = 有实现 5（`tensor:add`、`tensor:matmul`、`tensor:mean`、`tensor:multiply`、
`tensor:sum_to`）+ 缺口 17。不加 `std::macro` 时是 12 = 4 + 8，那 8 条正是计划里"表外至少 8 个（三个视图名、
`softmax`、`gelu`、`layer_norm`、`cross_entropy`、`cast`）"；展开后的 17 是同一批算子加上它们的 vjp。

C2：面内 21 = 有实现 8（`tensor:abs`、`tensor:add`、`tensor:matmul`、`tensor:multiply`、`tensor:relu`、
`tensor:subtract`、`tensor:sum`、`tensor:sum_to`）+ 缺口 13。

## 缺的是哪一半：两组，逐条

30 条都是**已注册**的算子（能编、能跑、在主机上算得出数），所以缺的是设备那一半，不是注册。按
`modules/tiles/src/graph_regions.cpp#eligibility` 的分支走，30 条**全都落在第一个分支**
`unsupported_operator`：那一条问"`operations()` 表里有没有它"，而这 30 条正是表外的名字；`effect`
分支问不到它们，因为 `modules/tiles/src/census.cpp#operatorSurface` 与 `eligibility` 用同一条规矩，取数时
已经把非纯、有外部效应的算子剔掉了。分支之下还剩一问，由 `core::OperatorTable::semantics(oper)` 答：

**A 组——语义声明已在，缺的只是表里一个名字 + 一个 device 程序：5 条**
`tensor:broadcast_like`、`tensor:permute`、`tensor:reshape`、`tensor:transpose`（transformer 的 4 条）、
`tensor:argmax_axis`（C2 的 1 条）。四条视图算子 `eligibility` 过了 `semantics` 那一问就能进下标映射，
`argmax_axis` 的归约语义也已在；它们要补的是 `operations()` 那条记录与一份 lowering。

**B 组 21 条分三类：5 条 `nn:*` 根本不在算子表里**（`lookup` 返回 none，id=none）——要**注册**；**15 条 `tensor:*` 在表里但没有语义声明**（`undeclared major=-1`）——要**写声明**；**1 条 `tensor:softmax`（id 424）已有声明 `major=1`**——它其实属于 A 组，缺的只是表里一个名字与 device 程序。三类修法不同，排期不能合并（读数 `TILES_DECLARATIONS_1..6`）（读数：`TILES_DECLARATIONS_*`，`CAMEL_TILES_TEST_DECLARATIONS=1`）。（transformer 13 + C2 12，两程序共用 `matmul_grad_lhs`、
`matmul_grad_rhs`、`numel`、`shape` 四条）
`nn:sgd`、`nn:adam`、`nn:adam_state`、`nn:softmax_cross_entropy_labels`、
`nn:softmax_cross_entropy_labels_grad`、`tensor:gelu`、`tensor:gelu_grad`、`tensor:layer_norm`、
`tensor:layer_norm_grad`、`tensor:softmax`、`tensor:softmax_grad`、`tensor:permute_inverse`、
`tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`、`tensor:zeros_like`、`tensor:cast_like`、
`tensor:gt`、`tensor:strict_eq`、`tensor:take`、`tensor:numel`、`tensor:shape`。
这一组先要有语义声明（`docs/design/op-semantics`），才谈得上 device 程序。

两组怎么读出来的（下一笔照同一条路复算）：`tensor:` 的声明只有一张表
`modules/tensor/ops/semantics.cpp#declarations`（由 `attachSemantics` 贴到每个 `OpDef`），`nn:` 的只有
`modules/nn/tensor_ops.cpp#poolSemantics`（池化两个）——名字不在表里的就是 B 组。这一层是**读声明表**
得到的，不是运行时打印的；`camel inspect --ops` 只有纯度，没有语义声明。

先清 A 组划算：5 条里 4 条是视图（两个程序都在用），语义已写好、只差 lowering；B 组 21 条要动语义声明，
是后面几笔的大头。另有一问留给计划主人：`tensor:shape`、`tensor:numel` 是查询，`nn:sgd`、`nn:adam`、
`nn:adam_state` 是参数树更新，它们可能该被移出面，而不是补 device 程序。

## 怎么读这张账本

缺口清空 G4 才允许记完成；这份文件只记读数，一个算子都不在这里补。账本是快照：表长一个名字、程序多一个
算子，数就变；重跑上面两条命令即可。表本身今天 24 个名字，全在 `tensor:`，`nn:` 一个都没有 device 实现。

## A 组的四条视图清完之后（同一命令复算）

表里加了 `tensor:transpose`、`tensor:permute`、`tensor:reshape`、`tensor:broadcast_like` 四条，表从 20 个
名字变成 24 个（仍全在 `tensor:`）。`tensor:argmax_axis` 这一条**没有加**，原因在下面。复算的三条读数：

- `transformer_step`（带 `std::macro`）：22 = 9 + **13**（原来 22 = 5 + 17）。表内多了 `broadcast_like`、
  `permute`、`reshape`、`transpose` 四条。
- `transformer_step`（不带 `std::macro`）：12 = 7 + **5**（原来 12 = 4 + 8）。剩 `nn:sgd`、`gelu`、
  `layer_norm`、`shape`、`softmax`；`broadcast_like` 只在派生图里出现，所以这一组没有它。
- C2：21 = 8 + **13**，**没变**。A 组在 C2 里只有 `argmax_axis` 一条，而它没进表。

缺口总数 30 → **26**（17+13 → 13+13）。四条视图各自的"device 程序"一句话：

- `tensor:transpose` / `tensor:permute`：不是算出来的，是同一块缓冲区按另一个下标读。`eligibility` 把调用
  的实例改写成 `reindex` 后交给 `kernel::coreViewAddress` 校验（`modules/tiles/src/graph_regions.cpp#eligibility`），
  与 `modules/tiles/src/compiler.cpp` 给这两条算子落的节点是同一条路——没有核可写，只有地址。
- `tensor:reshape`：同一块缓冲区换一个形状标签，元素数必须相等，与 `compiler.cpp` 的 `reshape` 分支同一条
  规矩（不等就报 `unsupported_index_contract`，不是"表里有、程序没有"）。
- `tensor:broadcast_like`：第二个操作数只给结果的形状，地址按第一个操作数读；`coreViewAddress` 只收一个数据
  操作数，`eligibility` 因此只把第一个传给它。
- `tensor:argmax_axis`：**仍缺口，而且不是"表里加了、程序还没有"**——它没有可写的程序。结果是 `i64` 下标，
  而 device 侧今天只收 f32/f64 结果（`compiler.cpp` 要求静态 f32 形状），归约契约
  （`modules/tiles/src/reduction_kernel.cpp#reductionAccess`）产出的是数值，没有"带下标"的归约。补它要新的
  结果类型加归约契约的一条，是后面单独的一笔；这一笔不把名字放进表，因为放进去就等于宣称有程序。

另有一条同批暴露出来的账：`tensor:sum_to` 早就在表里，但此前没有任何 `#gpu` scope 走到过它。视图进表后，
派生图第一次走到它，`eligibility` 的通用归约分支把**等形状**的 `sum_to`（实例是 `Unique` 写，其实是 copy）
按归约校验，于是判成不支持。这一笔把它分成两条：`Unique` 写按视图地址校验，其余按归约契约校验
（`modules/tiles/src/graph_regions.cpp#eligibility`）。读数是 `test/plans/feat/modules/tiles.plan.toml#gpu_scope.macro_expansion_publishes_the_derived_graph`
从"拒绝这个 scope"变成"接受、且 scope 里算出的值与它的主机孪生一致（`moved 0.0`、`wrong 0.0`）"——这条
计划条目原来把拒绝当成事实记着，**事实变了，所以断言跟着变**：它的 `desc` 原文写的是"refused for a tensor
operator of the backward pass (`broadcast_like`, which the device back end does not lower)"，而今天
`broadcast_like` 在设备侧 lower 了，于是条目由 `negative` 改成 `behavior`，断言从"必须报
`GpuScopeUnsupported`"改成"必须接受、placement 仍是 `gpu-required`、两条读数都是 0"。这是**账被改对**，
不是红变绿：拒绝的理由消失了，值的一致性多了一条读数。
## 按节点重量：轴归约一族（今夜的读数）

列的读法：**节点 URI** 是 `std::jgir` / 探针在图里看到的名字；**表**是 `graph_regions.cpp#operations`；
**声明**是 `OperatorTable::semantics`；**lowering** 是 `modules/onnx/lowering.cpp` 的键。

| 算子 | 节点带的 URI | 表里有节点名？ | 有声明？ | 有 lowering（同名）？ | 真实结论 |
| --- | --- | --- | --- | --- | --- |
| `sum_axis` | `array:sum_axis` | **没有**（表里是 `tensor:sum_axis`） | 有（挂在 `tensor:` 的 id 上） | **没有**（键是 `tensor:sum_axis`） | 进不了区域、导不出 |
| `mean_axis` | `array:mean_axis` | **没有**（同上） | 有（`tensor:`） | **没有** | 进不了区域、导不出 |
| `max_axis` | `array:max_axis` | **没有**（同上） | 有（`tensor:`） | **没有** | 进不了区域、导不出 |
| `min_axis` | `array:min_axis` | **没有** | 有（`tensor:`） | **没有** | 进不了区域、导不出 |
| `argmax_axis` | `array:argmax_axis` | **没有** | **没有**（`undeclared major=-1`，声明在 422） | **没有** | 进不了区域、导不出 |
| `argmin_axis` | `array:argmin_axis` | **没有** | 有（`tensor:`） | **没有**（连 `tensor:` 键都没有） | 进不了区域、导不出 |

**三条新总数（按节点，六条轴归约）**：真正有实现 **0**；缺 lowering **6**；不在表里（按节点名）**6**。
**旧数（按表）**：这三条曾被读成"`sum_axis`/`mean_axis`/`max_axis` 有实现"，`operations()` 里的
`tensor:sum_axis`、`tensor:mean_axis`、`tensor:max_axis` 三条因此**对真实程序是空的**。

**而这一族的 6 条不在那 26 条缺口里**——账本把它们读成"有实现"，所以缺口数被低估了 3（按表有、按节点无），
另 3 条（`min`、`argmax`、`argmin`）本来就没进过表面清单。**26 这个数因此不是"还剩多少没做"，而是"按表读数
还剩多少"**；按节点重量的完整 26 行需要各跑一次 `tiles::census` 取节点名（这一步未做，不在本页冒充完成）。

### 按节点重量（一）：C2 的 21 条，节点名与表名一致

读法：`tiles::census` 给出表名与分组；`std::jgir` 的 `"label"` 给出节点携带的 URI；两者逐条比对。

**读数**：`TILES_CENSUS ops=21 device=8 gap=13`，而 jgir 的算子标签里，那 21 个名字**逐条存在且拼法相同**
（`tensor:abs`、`tensor:add`、`tensor:matmul`、`tensor:multiply`、`tensor:relu`、`tensor:subtract`、
`tensor:sum`、`tensor:sum_to` + `nn:adam`、`nn:adam_state`、`nn:softmax_cross_entropy_labels`、
`nn:softmax_cross_entropy_labels_grad`、`tensor:argmax_axis`、`tensor:cast_like`、`tensor:gt`、
`tensor:matmul_grad_lhs`、`tensor:matmul_grad_rhs`、`tensor:numel`、`tensor:shape`、`tensor:strict_eq`、
`tensor:take`）。**C2 的 21 条在节点级成立：8 有实现、13 缺口，旧数与新数一致。**

### 按节点重量（二）：一处上一笔说错的地方，改正

上一笔说"`tensor:sum_axis`/`tensor:mean_axis`/`tensor:max_axis` 三条对真实程序是空的"。
**按节点重量后这句要收窄**：C2 与 transformer 这两个 showcase 走的是 `stdlib/signatures/tensor.cml`
的函数，节点带的就是 `tensor:` 拼法，那三条对它们是**有意义的**。空的只发生在**调 `array` 模块函数**的
程序上（`x->sum<0>()`、`x->argmax<1>()`），那些节点带 `array:`。**所以这不是"表错了"，是"有两个入口"**：
`tensor.argmax_axis` 与 `array.argmax_axis` 是同一个算子的两个入口，账本只量过前者。

**第三个面**因此存在且未被账本覆盖：`stdlib/array.cml` 里那批函数（`sum`、`mean`、`min`、`max`、`argmax`、
`argmin`、`sort`、`argsort`、`merge_sorted`）在程序里出现时，节点带的都是 `array:` 名字，账本的 21 条**与它们
没有交集**。**缺口真实数 = 账本的 21 条（C2，节点级已确认）+ 这批 `array:` 入口中未进表的名字（未逐个量完）**。

### 算式与两个未量项（本轮到此为止，不猜）

`缺口 = A（C2 节点级，13）+ B（transformer 节点级，**未量**）+ C（`array:` 入口里未进表的名字，**未量**）`

- **A = 13**：C2 已按节点确认（`TILES_CENSUS ops=21 device=8 gap=13`，jgir 标签逐条同名）。
- **B 未量，且有具体原因**：账本引用的程序 `test/cases/perf/transformer_step.cml` 在**今天的主干上不通过
  类型检查**（`NoMatchingFunction`：`adam(b, gradient, opt, lr)` 与 `<nn:adam>(params, grads, state, lr,
  beta1?, beta2?, eps?)` 对不上），所以它的 13 条无法复现；要量必须先让那个程序与今天的 `nn:adam` 对齐，
  那是另一笔（会改测试程序，不属于本lane）。
- **C 未量**：`array:` 入口已知会带 `array:` 名字的至少有 `sum`、`mean`、`min`、`max`、`argmax`、`argmin`
  （`stdlib/array.cml` 的 `#op('array:*_axis')` 六条），`sort`/`argsort`/`merge_sorted` 另有各自拼法；
  九个要逐个跑一次 census 才成表，**这一轮没跑**。

**所以本页今天能站住的只有 A = 13；26 这个数字既不是 A、也不是 A+B+C，它是"按表读 C2+transformer"的旧量法。**

### 算式落定：A = 13，B = 14，A∪B = 19，C 未量

`缺口 = A + B + C（去重）`

- **A = 13**（C2，节点级）：`TILES_CENSUS ops=21 device=8 gap=13`。
- **B = 14**（transformer，节点级）：`TILES_CENSUS ops=25 device=11 gap=14`——这个数今晚才第一次读得到，因为程序原先不通过类型检查（见下条）。
- **A∪B = 19**（去重后）：两条程序共用的缺口是 `nn:adam`、`nn:adam_state`、`nn:softmax_cross_entropy_labels`、
  `nn:softmax_cross_entropy_labels_grad`、`tensor:cast_like`、`tensor:gt`、`tensor:matmul_grad_lhs`、
  `tensor:matmul_grad_rhs`；只在 transformer 里的另有 `tensor:dim`、`tensor:expand_axis`、`tensor:softmax`、
  `tensor:softmax_grad`、`tensor:zeros`、`tensor:zeros_like`；只在 C2 里的另有 `tensor:argmax_axis`、
  `tensor:numel`、`tensor:shape`、`tensor:strict_eq`、`tensor:take`。
- **C 未量**：`array:` 入口（`sum`/`mean`/`min`/`max`/`argmax`/`argmin`/`sort`/`argsort`/`merge_sorted`）
  逐个跑一次 census 才成表，**本轮没跑**。

**旧账 26 与新账 19 的差**：旧账 transformer 那一半读的是**程序跑不起来之前**的表数（13），而今天节点级是 14；
C2 的 13 恰好相同。所以 26 **不是**节点级事实，19 是——**且 19 不含 C**。

### 一条可读性缺口（记在这里，因为今天没有诊断指向它）

**`grad of P` 要求梯度与参数同形**：参数是静态形状时，梯度不能用 `f32[..]` 声明。今天报的是
`NoMatchingFunction` 加一串重载，**没有一个字说“哪个字段的形状不一致”**——`transformer_step.cml` 因此坏了
很久（`perf` 不在 `-Tier fast`，没人看见）。这条诊断缺口比那个测试坏掉更值得修：它让每一个“形状不匹配”
的调用都变成一串重载列表。
