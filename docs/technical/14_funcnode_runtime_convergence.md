# FuncNode 运行时表示收敛（已落地）

## 1. 背景

第三节核心收敛已经完成，GIR 侧不再依赖编译期 `FunctionData` / registry：

- `FuncNode` 改为持有目标 `Graph*` 与运行时 `Function*` 原型。
- `Graph` 已移除 `registerFuncData` / `lookupFuncData` / `funcDataRegistry_`。
- `Builder`、macro rewrite、inline、fastvm、taskflow、graphviz、debugger 已统一改走运行时路径。

当前剩余差距主要集中在图生命周期 GC 与静态区 `autoSpace` 迁移，不再属于 FuncNode 收敛本身。

## 2. 本次落地结果

1. `FuncNode` 热路径只读 `graph()`，不再触发 `shared_ptr` 查表。
2. 函数常量由构图阶段直接 materialize 为运行时 `Function*` 静态槽。
3. 宏结果回填改为直接复用 runtime slot，不再把 `Function*` 回投成 `FunctionData`。

1. 让 `FuncNode` 的热路径优先使用运行时可执行对象（`::Function*` 或等价句柄）。
2. 将 `FunctionData` 的职责收缩到“构图早期/桥接阶段”。
3. 把 `funcDataRegistry_` 从“必经存储”降级为“过渡兼容层”，并给出最终删除条件。

## 3. 非目标

- 不改 `Graph` 的 `shared_ptr` 生命周期模型。
- 不实现 `Graph*` 的 GC 可达性扫描。
- 不把全部静态对象强制迁移到 `autoSpace`。

## 4. 关键实现点

核心位置：

- `include/camel/compile/gir/nodes.h`：`FuncNode` 公开 `graph()` / `rtFunc()`，不再暴露 `funcShared()`。
- `src/compile/gir/nodes.cpp`：`FuncNode::create/clone` 直接绑定 `Graph*` 与 runtime `Function*`。
- `src/compile/gir/builder.cpp`：`createFuncDataNode` 改为直接构造 runtime `Function*`，未解析闭包继续走 `DATA(Function) + FILL`。
- `src/execute/macro/macro.cpp`：direct macro 与 materialize result 全部直接操作 `Function*`/`slot_t`。

## 5. 与第三节的关系

- 第三节中“Graph 内不持 `FunctionData`，由 Builder 导出运行时对象”已对函数值路径落地。
- 第三节中“子图浅拷贝 + 显式裁剪契约”与该收敛相互配套，见 `05_graph_ir.md`。
- 第三节中 GC / `autoSpace` 仍未纳入本文件范围。

## 6. 风险与回归点

- **语义风险**：函数常量中的历史图引用未按重写契约更新。
- **性能风险**：若运行时访问层仍隐式走 `shared_ptr` 包装，会退化热路径。
- **兼容风险**：若有仓外工具仍假设 `FUNC` 节点可追溯到 `FunctionData`，需要同步升级。

## 7. 建议测试清单

- 宏调用链：静态函数常量作为 CALL callee 的场景。
- inline + clone：跨图重写后函数引用正确性。
- C++ 导出：函数常量与字符串/数字常量混合图。
- Debugger/Graphviz：`FUNC` 节点标签与图名解析保持稳定。
