# 块根拿不到「声明它那个算子的模块」：四次推断被读数否证，缺口在语言层

分支 `lane/editview-gir`，tip `e261f762d7`。`test/plans/probe/editview.plan.toml` 停在
`total: 15, pass: 2, fail: 13, killed by a signal: 7`，这是**已知未完成**，不是新红。

## 一、现在到哪了

块头 `edit d = f { … }` 的 `d` 是块所工作的那张图，块根为此要一次 `graph:open(f)` /
`graph:graph_of(f)`。AST 层已经把块头的初始值做成 `BlockRootLoad`（块的种类在 `blockKind()`，
`dataType()` 仍是 `BlockRoot`——**这一条不能反**：返回 `EditExpr`/`ViewExpr` 会让分派把块头当成块
去读子节点，读到 `vector::_m_range_check`），GCT 造 `DREF(编号) + LINK(1)`，GIR 按编号解析。
卡在最后一步：那个编号在程序里没有可解析的声明。

## 二、四次否证（因）

这一节记的是**被推翻的推断**，因为它们比结论有用：每一条都曾看起来成立，且都能把人带到错的下一步。

| # | 当时相信的 | 否证它的读数 | 之后正确的认识 |
| --- | --- | --- | --- |
| 1 | `open(f)` 按名字能跑通（`test/cases/runtime/edit_origin/spliced_write.cml` 是绿的）⇒ `graph:open` 一定有一个算子组 | 探针：程序导出表里**没有任何算子组**；`importModule("graph")->getNativeEntity("open")` 的 `present=0`；`nativeOperatorGroup(编号)=null` | 按名字能通只证明**模板集**能通。`open` 是无体的泛型声明，**它没有组**；组只属于有规则的算子 |
| 2 | 所以登记点该在 `ProgramExports::exportEntity`，把 id ↦ 组补上就行 | 同一个探针：组表空是**必然**（没有组可登记） | 载体是模板集不是组。登记形状改成「id ↦ 模板集」，键 `<module>:<name>` 在导出时算一次 |
| 3 | `graph` 是预编译模块 `stdlib/graph.cmo`，所以不经过程序导出表 | 带 importer 的探针打出 `path='.../stdlib/graph.cml'`——**源码**，不是 `.cmo` | 那一轮我把 `Context::importModule` 里的一句**注释**当成了证据。注释解释的是「为什么同目录优先」，不是「`graph` 是 `.cmo`」 |
| 4 | 合成 import 发生在体阶段，所以「没 import」不是全部原因，但登记逻辑已对 | 同一组读数：`openPresent=0` 且 `templates=0` | 不是「登记逻辑错」，是**声明根本没建**。体阶段补 import 太晚：声明期看的是 `importSites()`，那时已经过去了 |
| 5 | 那就「在声明期替程序加一条 import」 | 读 `module_source.cpp:280-311` 与 `module.h:238/339`：`Module` 里只有 `importSites_`，没有「只依赖、不进名字作用域」的登记处；块根只在体访问器里被建，声明层没有挂点 | 两条不改可见性的路**都不存在** ⇒ 这是设计缺口，不是实现缺口 |

**链条的形状**：没有组 → 组表空是必然 → 不是预编译模块 → 不是「登记逻辑错」而是「声明没建」 →
入口根本不存在。每一步都把范围缩小一格，最后落到语言层。

## 三、被留下的两笔（不足以转绿，等裁定后在**同一个分支**继续）

```
54492213d7  parse, compile: a block's root is the template a body-less generic declaration makes
            块根按编号取模板集，建与「名字调泛型」同形的节点（空图集 + genericDrefs_），
            不造任何组：全仓 OperatorGroup::create 仍是 4 处，可复算
37fb882f4e  compile: a block's root imports the module whose operator opens it
            块根把 owning module 真正 import 进来（load 不等于 import）；注释写明「实测不够」
e261f762d7  questions: …（缺口的问题页）
```

半落地比不落地更坏，所以**这两笔不单独送去合入**：它们各自都对，但合起来仍不转绿。

## 四、待裁

三条候选（甲 隐式 import 改可见性 / 乙 `Module` 加「只依赖不进作用域」的登记处 / 丙 从 `graph`
作为库编译的结果按编号取）见问题页
`internal/questions/2026-11-27-where-a-blocks-root-gets-the-module-that-declares-its-operator.cn.md`。
裁定下来之前不动这条线：三条落在语言可见性、模块登记机制、库编译三层，先动哪一处都是替语言
作者做语义决定。

## 五、判据（裁定后照这个量）

1. `templates.has_value()` 非空 —— 现在停在它；
2. **同形证明**：同一程序里 `open(f)` 按名字调用 与 块根从编号拿到，两次 `std::jgir` 的
   `kind` / `label` / operand 边 / context 边逐字段比，只允许「来源不同」；
3. `probe/editview` 转绿且 `killed by a signal` 归 0，全仓 `OperatorGroup::create` 处数不变。