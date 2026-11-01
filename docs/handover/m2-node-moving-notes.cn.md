# M2 搬节点：施工图（网络回来照这一页直接写）

来源：`include/camel/runtime/draft_session.h`（`GraphDraftGroup`）、`include/camel/runtime/draft.h`
（`DraftNodeInit`、`DraftNodeView`）、`include/camel/runtime/graph.h`（`GCGraph` 的读法）。
`modules/transforms/jvp.cpp::buildJvpGraph` 是已跑通的样例，照它的造法写。

## 一、六个入口的前置条件（谁必须在谁之前，谁会失败）

- `GraphDraftGroup(context, base)`：把 `base` 程序的图当底；成员与它们组成一个新程序，成员之间可以互相以 `GraphId` 引用。
- `reserve(mangledName, name) -> GraphId`：**先**占位，之后才能 `draft(id)` 往里建；**第一个 reserve 的成员是根**；一个成员只能 reserve 一次。
- `draft(id) -> GraphDraft &`：拿到的引用只在 group 活的时候有效；`setFuncType` 先定签名，再 `allocateRuntimeSlot` 定槽，**再** `addPortNode`/`addNode`（节点的 `dataIndex` 必须是已分配的槽，`dataType` 必须与槽的类型一致，否则建出来的图在 encode 时对不上）。
- `addNode(DraftNodeInit)`：`normInputs`/`withInputs`/`ctrlInputs`/`orderInputs`/`syncInputs` 里的 `gc_node_ref_t` **必须已在同一个 draft 里**；跨 draft 的引用不会被搬，图会指向不存在的节点。返回的 ref 就是后续节点引用它的把手。
- `setOutputNode(id)`：**在该节点建好之后**；出口只能在已建节点里选，指向不存在的 ref 会在 encode 时失败（`setReturnNode` 同理，`GCReturnKind::Self` 表示返回出口本身）。
- `encode()`：**全部成员建完之后调用一次**；`encoded(id)` 在 `encode()` 之前返回 **nullptr**，之后才拿到 `GCGraph *`；`adopt(program)` 要在需要引用外部图（特化、复制品）之前调。
- 先说好的用法：`reserve` 两个成员（`first_of_*`、`second_of_*`）→ 各自建 → 都 `setOutputNode` → 一次 `encode()` → `encoded()` 拿函数值。

## 二、`DraftNodeInit` 的字段（标出"端口映射"）

| 字段 | 用途 | 端口映射？ |
| --- | --- | --- |
| `dataIndex` / `dataType` | 值的槽与类型 | 否（槽） |
| `kind`（`GCNodeKind`）/ `runtimeFlags` / `attrs` / `permissions` / `region` | 节点类别与头部位 | 否 |
| `origin` / `name` | 来源与名字；重写时置 0 表示沿用 | 否 |
| `payload`（`std::span<const std::byte>`） | 节点体（算子体、常量等），`DraftNodeView::payloadAs<T>` 读 | 否（体） |
| **`normInputs`** | 普通参数口 | **是** |
| **`withInputs`** | 上下文参数口（`with` 族） | **是** |
| **`ctrlInputs`** | 控制口 | **是** |
| **`orderInputs`** | 顺序边（无数据，`GCGraphOrderEdge` 那一套） | **是** |
| **`syncInputs`** | 同一 `sync` 块里前一条语句（`GCGraphSyncEdge` 那一套） | **是** |

搬节点时被重映射的就是最后五行；前三行是数据口，后两行是效果口（顺序、同步），两者在图上都不算"端口"。

## 三、`GCGraph` 侧读一个节点输入的入口

- `normInputsOf(ref)` / `withInputsOf(ref)` / `ctrlInputsOf(ref)`：三段输入 run，**这是数据口与"控制口"的全部**（`ctrlInputsOf` 从 `ctrlBegin` 数到下一个节点头的 `inBegin`）。
- `dataPortsOf(ref)`：普通口 + 上下文口合成一段（`inBegin .. ctrlBegin`），`portRoleOf(ref, port)` 给出 `Normal`/`Context`——**搬"值节点"时按角色区分这两类口**。
- `orderPredsOf(ref)` / `syncPredsOf(ref)`：顺序边与同步边**不在输入 run 里**，它们各自是图上的独立数组（按 user 排序的一段），要单独搬。同步边背的"被使用"是活的（liveness 跟着它），所以**残差与它无关，但搬迁不能丢**。
- 节点自身的读法：`node(ref)`（拿 `GCNode` 的记录）、`nodeContentHash(ref)`（节点级事实的版本）、`nodeBodyAs<T>(ref)`（读体）。
- 失败面：`ref` 不是节点时 `normInputsOf` 等返回空 span、`node(ref)` 返回 nullptr——**用空 span 静默降级会把"搬丢了"变成"搬成空"**，所以搬之前先 `node(ref) != nullptr` 断言一次。