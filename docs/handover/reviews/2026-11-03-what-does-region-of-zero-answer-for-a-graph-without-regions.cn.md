# 一张没有区表的图，`region_of(0)` 该答真记录还是报错？

## 问什么

`modules/graph/read.cpp` 的 `regionOf` 对 0 号区有一个兜底：图没有区表时，答一条记录而不是报错。
它原来是一条共享的 `static const GCRegion`（全零）。要裁定的是：这个位置该答什么。

**默认建议：答"编码器会为这张图的根体写下的那条记录"，按值返回，不合成 `result`/参数/端口**（第 4 节，
已按此落地）。理由不是"省事"，而是 **`result=图的出口` 那条路会制造一个新的假值**——见第 2 节路 B。

## 1. 让它是问句的读数

- 谁读它：`git grep -n "regionOf(" modules/graph` 只有两处调用方 ——
  `modules/graph/read.cpp#regionKindKernel`（`graph.region_kind`）读 `region.kind`；
  `modules/graph/read.cpp#regionParentKernel`（`graph.region_parent`）在 0 号区**先**返回 `NullSlot`，
  根本走不到这里。语言侧可见的区接口只有 `region_of` / `region_kind` / `region_parent`
  （`stdlib/graph.cml#constant`，`modules/graph/module.cpp#GraphModule`），没有 `region_result` / `region_ports`。
  所以今天**没有任何调用方依赖"全零"，也没有任何调用方读得到 `result`**。
- 真的根体记录里有什么（A6 的探针，一次运行里 8 张图，含 jvp 与 export 的图）：
  `bodyKind=0(Root) bodyOwner=none bodyResult=none bodyParams=none bodyPorts=0`，8/8 一致。
- 为什么 `result` 恒空：`GraphDraft::setRegionResult` 只接受 `id > 0`（`src/runtime/draft_regions.cpp#GraphDraft::setRegionResult`），
  写 `result` 的全是 arm / loop 的两半 / 内联体（`git grep -n "setRegionResult("` 的 20 处无一传 0）——
  **根体不产值**，图自己的出口是另一个字段（`payload.exitNode`）。
- 于是"没有区表"这个状态本身说明什么：草稿**只有**在根体带了 tag 时才写下那条记录
  （`GraphDraft::setRegionAttrs`），而草稿一造出来就带根体（`GraphDraft`）。空表 = "根体什么都没带"。
- 可达性：A6 之后本二进制建出的每张图都带根体记录（探针：8/8 图 `encodedRegions=1`），
  所以空表只剩"不是从草稿编码来的图"（字节/会话载入）这一条路。

所以这页问的不是"空表还会不会发生"，而是"发生的时候，那个答案里的 `result` 该填什么"。

## 2. 三条路

**路 A（默认建议，已落地）：答编码器会写的那条记录，按值返回。** 空表图的根体就是 `GCRegion{}`
（`kind=Root`、无 owner/result/ports/params/attrs），与有区表的图里 `region(0)` 的内容逐字段相同。
做法：`modules/graph/read.cpp#bodyRegion` 改成按值返回、`regionOf` 按值返回，`static const GCRegion` 归零。
代价：`regionOf` 的返回类型从 `const GCRegion &` 变成 `GCRegion`（模块内部的 C++ 类型，语言侧不变），
两处调用方本来就是 `const GCRegion &` 绑定，行为不变。

**路 B：合成"真"记录——`result` = 图的出口、参数 = 图的参数、端口 = 图的端口。** 代价：**它答的是一条
编码器从不会为根体写下的记录**。有区表的图上 `region(0).result` 恒为 `invalid`，于是同一句
`region_of(0)` 会按"这张图有没有区表"给出两种不同的 `result`；而语言侧根本没有 `region_result`，
这个字段**谁也读不到**，等于为一个不可见字段引入第二套真值——正是这两条要修的那种"本该大声的状态被
悄悄变成假值"，只是换到图侧。若哪天补上 `region_result`，它会把"根体的 result 是出口"定型成规范，
而草稿侧的编码器并不这么写（那时要么改编码器，要么改这条读数，两边都得动）。

**路 C：没有区表就报错**（`throwRuntimeFault(InvalidArgument, "the graph has no such region")`）。
代价：这是**语言可见**的行为改变 —— 今天能取到根体的程序（例如 `graph.region_of(n)` 打在空表图的节点上）
明天会抛诊断；而"0 号区是根体"本来就是这套模型里恒真的一句（`nodeRegion` 对每个节点都答 0），
拒绝回答反而比回答多一条特例。

## 3. 若裁定是路 B，范围在哪

只改 `modules/graph/read.cpp#bodyRegion`（把 `GCRegion{}` 换成从图合成的记录）并让
`regionOf` 拿到 `const GCGraph &`（它已经有了），不动 `src/runtime` 的编码器，也不动
`stdlib/graph.cml` 的声明。但按第 2 节的理由，这条要连"根体算不算产值"一起重新裁定，
不是一次改动能收口的。

## 4. 默认建议

**按路 A 落地，不等回复。** 理由：`result`/`params`/`ports` 在根体上恒空是编码器的既有事实（8/8 实测），
路 B 会让 `region_of(0)` 答一条没有生产者的记录；路 C 会让一句恒真的问题变得会失败。路 A 只做一件事——
把"共享一条静态记录"换成"按图答一条与编码器一致的记录"，`static const GCRegion` 因此归零
（`git grep -n "static const GCRegion"` 全树 0 命中），而语言侧读数一个字不变。
作者若裁定路 B 或 C，只需回退 `modules/graph/read.cpp` 里的 `bodyRegion` 与 `regionOf` 的返回类型，
别的层不受影响。