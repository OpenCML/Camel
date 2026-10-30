# GPU 支持面：`tiles::census` 的缺口账本

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

**B 组——连语义声明都还没写：21 条**（transformer 13 + C2 12，两程序共用 `matmul_grad_lhs`、
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
计划条目原来把拒绝当成事实记着，事实变了，条目也一起改了（`negative` → `behavior`）。