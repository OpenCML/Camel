# GIR 图生命周期与静态区 GC 里程碑

## 1. 目标

本文件对应第三节剩余未完成部分：

- 图生命周期集中管理，而不是长期依赖散落的 `shared_ptr<Graph>`。
- 静态区中的 GC-traced 对象最终进入 `autoSpace`，由统一根集管理。
- 图裁剪、卸载、模块销毁时，不出现循环引用泄漏或悬空 `Graph*` / `Function*`。

本里程碑暂不直接改执行语义，先定义可实施的根集、扫描边与迁移顺序。

## 2. 根集

建议把图存活权威收敛到模块级根集：

- `Module` / 编译产物持有入口图 root。
- 正在执行的 `Context` / `FramePool` 持有活跃 root graph。
- 调试器/JIT 若缓存子图，必须显式登记为弱缓存或可重建缓存，不能偷偷延长图生命周期。

禁止新增“顺手存一个 `graph_ptr_t`”的隐式长期所有权。

## 3. 可达性扫描边

从 root graph 出发，GC/卸载前需要遍历：

1. `subGraphs_`
2. `dependencies_`
3. `FuncNode::graph()`
4. 静态区 `slot_t` 中的 `Function*`，再跟到 `Function::graph()`
5. 若静态对象里还嵌套 tuple/array/struct，则递归扫描其中的 GC-traced 引用

这意味着“图引用”已经不只存在于图容器关系里，也可能存在于静态函数常量里。

## 4. static slot 到 autoSpace 的迁移策略

建议分两步：

### 步骤 A：可扫描但不搬迁

- `Graph::staticDataArr_` 继续保存 `slot_t`
- `FrameMeta::staticArea` 继续按 slot 拷贝
- 新增一层只读扫描工具：给定 `Graph`，能枚举静态区里所有 GC-traced `Object*`

### 步骤 B：统一到 autoSpace

- 由 Builder/seal 期决定哪些静态对象必须分配到 `autoSpace`
- arena 只保留 frozen 邻接、FrameMeta、非 GC POD 元数据
- `Function*`、`Tuple*`、`Array*`、`Struct*` 等静态对象由 `autoSpace` 管理

迁移顺序必须是“先有根集扫描，再改分配器”，否则对象会被过早回收。

## 5. 建议代码落点

- `include/camel/compile/gir/graph.h`
  - 增加只读遍历接口，例如枚举直接引用图/静态对象
- `src/compile/gir/graph.cpp`
  - 提供 root graph 可达闭包遍历，供卸载、调试、GC 共用
- `src/core/context/frame.cpp`
  - 保持 `FrameMeta` 安装逻辑纯拷贝，不在这里偷偷创建新的长期所有权
- `src/core/rtdata/conv.cpp`
  - 未来切换静态对象分配器时，统一从这里落点

## 6. 验收标准

启动本里程碑后，至少满足：

1. 给定一个 root graph，可稳定枚举全部可达子图与静态函数常量引用图。
2. 构造“inline 后删除唯一引用子图”“模块卸载后释放图树”两类用例，不出现泄漏或 UAF。
3. `Function*` 静态常量迁入 `autoSpace` 后，宏执行、CALL、debugger 导出语义不变。
4. 任何新增长期缓存都能明确说明其是否参与根集。
