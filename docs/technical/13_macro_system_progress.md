# 宏系统推进进展

本文档记录 OpenCML 宏系统的实现进展、遇到的问题及临时解决方案，供后续开发参考。

---

## 一、功能概述

**宏系统目标**：被标记为 `macro` 的函数可在编译期执行，其参数须为编译期值（对应 GIR 的 DATA 节点）或由其他宏产生的值；宏执行结果直接重写为 DATA 节点。

**设计要点**：
- 宏是“值宏”，执行后产生 DATA，不做代码级 AST/GIR 重写
- 编译期执行尽量复用运行时能力（Frame、OPER 等）
- 任何 DATA 节点视为编译期有值；函数值以 `DATA(Function*)` 静态槽表示
- 宏返回的运行时对象直接复用结果 `slot_t` 回填 DATA 节点，不再回投成编译期 `Data`

---

## 二、实现进展

### 2.1 已落地的核心能力

| 能力 | 状态 | 说明 |
|------|------|------|
| 直接宏调用 (FUNC) | ✅ | `add_one(41)` 可编译期求值 |
| 间接宏调用 (CALL) | ✅ | `DATA(Function*)` 形式的宏调用 |
| 链式宏调用 | ✅ | 宏内调用其他宏，如 `add_two` → `add_one` |
| 宏返回函数值 | ✅ | `make_adder(x)` 返回闭包，可传给普通函数 |
| DATA 节点作为输入 | ✅ | 遍历时将 DATA 值写入 frame，避免未初始化读取 |
| CALLEE 为 DATA | ✅ | CALL 的 callee 从 DataNode 取 `Function*` |
| rearrange 后 FrameMeta 失效 | ✅ | 宏遍修改图后清除缓存，避免运行时用旧布局崩溃 |

### 2.2 关键代码改动

- **`Node::replaceUses()`**：只替换旧节点输出，不继承输入，适配宏结果替换语义
- **`Graph::isMacro()`**：暴露宏标记，`FuncNode`/静态函数常量统一复用目标图上的宏属性
- **GIR builder `visitExitNode()`**：支持将 `graph_ptr_t` 返回值包装为 `DATA(Function*)`
- **MacroExecutor**：编译期图执行器，支持 CAST/COPY/FILL/ACCS/BRCH/JOIN/CALL/FUNC/OPER 等节点
- **`getStaticNodeSlot` / `DataNode::createStaticSlot`**：运行时 slot 直接回填静态 DATA 节点

---

## 三、遇到的问题与临时方案

### 3.1 宏内通过参数调用函数值导致崩溃

**现象**：宏接收函数值参数并在内部调用（如 `apply_once(f, x)` 其中 `f` 为参数）时，在宏遍或运行时发生 access violation (0xC0000005)。

**推测原因**：
- 宏执行器在 CALL 时从 frame 读取 callee；若 callee 来自 PORT，需保证 frame 正确填充
- Frame 布局、索引或 closure 图的 setup 可能与 NodeVM 存在差异，导致读取越界或未初始化

**临时方案**：在 `tryExecuteDirectFunc` / `tryExecuteIndirectCall` 中增加 `macroCallsFunctionParam()` 检测：
- 若宏图内存在 CALLEE 为 PORT 的 CALL 节点，则**不进行编译期展开**
- 该宏保留为 FUNC 节点，由运行时执行（NodeVM 可正确处理）
- 行为：编译期跳过，运行时正常；仅牺牲该场景的编译期优化

**代码位置**：`src/execute/macro/macro.cpp` 中 `macroCallsFunctionParam()` 及调用点。

### 3.2 rearrange 后 FrameMeta 仍被复用导致崩溃

**现象**：宏遍对图执行 `rearrange()` 后，运行时仍使用旧的 FrameMeta，导致 frame 布局与图结构不一致，引发访问越界或错误。

**原因**：`FramePool::acquire()` 会缓存 `FrameMeta`；`rearrange()` 改变了 `runtimeDataType`、`staticDataArr` 等，但未使缓存失效。

**解决方案**：在宏遍中，每次 `currGraph->rearrange()` 后执行  
`currGraph->setExtra<FrameMeta, 0>(nullptr)`，使下次 `acquire` 时重新安装正确的 FrameMeta。

**代码位置**：`MacroRewritePass::apply()` 中 `if (graphChanged) { currGraph->rearrange(); ... }` 分支内。

### 3.3 CALL 的 callee 为 DATA 时从 frame 读取

**现象**：CALL 的 callee 若为 DATA 节点，其值从未写入 frame，直接 `frame->get<>` 会读到未初始化内存。

**解决方案**：在 CALL 处理分支中，若 `calleeInput->type() == NodeType::DATA`，直接通过 `getStaticNodeSlot(calleeInput)` 读取静态槽，再按 `::Function*` 解释并调用。这样不再依赖旧的编译期常量回投路径。

### 3.4 DATA 节点值未写入 frame

**现象**：OPER、CALL 等节点从 frame 读取输入时，若输入为 DATA 节点，会读到未初始化 slot。

**解决方案**：在 `executeGraph` 的 switch 中为 `NodeType::DATA` 增加分支：`frame->set(node->index(), getStaticNodeSlot(node))`。即把图静态区中的 `slot_t` 原样写入运行时 frame，避免经 `Data` 中间层转换。

---

## 四、测试样例

所有宏测试位于 `test/macro/`，需手动传入 `std::macro`（当前未纳入默认 fallback）：

```powershell
$env:PATH = "D:/Projects/Camel/build/Release;$env:PATH"
$env:CAMEL_HOME = "D:/Projects/Camel/out/latest"
& "D:/Projects/Camel/build/Release/camel.exe" test/macro/basic.cml std::macro
```

| 文件 | 场景 | 期望输出 |
|------|------|----------|
| `basic.cml` | 单次直接宏调用 | 42 |
| `chain.cml` | 链式宏 `add_two` → `add_one` | 42 |
| `deep_chain.cml` | 多层链式 `add_five` → … → `add_one` | 42 |
| `branch_chain.cml` | 分支内调用宏 `choose` → `add_two`/`add_one` | 83 |
| `pipeline.cml` | 宏内多步调用，`let` 串联 | 43 |
| `functor.cml` | 宏返回闭包，传给普通函数 `sink` | 42 |
| `function_arg.cml` | 宏接收函数参数并调用（被跳过，运行时执行） | 20 |

---

## 五、已知限制

1. **宏内调用函数参数**：当前通过“编译期跳过”规避崩溃，该场景在运行时执行。
2. **`std::macro` 未入默认 pipeline**：需显式 `camel xxx.cml std::macro`；默认 fallback 仍为 `std::default`。
3. **CONST 关键字**：暂未实现，本版将任意 DATA 视为编译期值。
4. **返回码**：`camel xxx.cml std::gir` / `std::macro std::gir` 可能返回非 0，但 DOT 输出正常，与 CLI/pass 约定有关。

---

## 六、后续规划建议

1. **根因修复**：排查宏内通过参数调用函数值时的 frame/slot 不一致，争取在编译期直接支持。
2. **Pipeline 调整**：将 `std::macro` 作为执行 pipeline 的首个 pass，并统一 CLI/debugger 的 stage 展示。
3. **宏测试自动化**：将 `test/macro/*.cml` 纳入 CI，统一使用 `std::macro` 并校验输出。

---

## 七、相关文件

| 路径 | 说明 |
|------|------|
| `src/execute/macro/macro.cpp` | MacroRewritePass、MacroExecutor 实现 |
| `src/execute/macro/macro.h` | 宏遍声明 |
| `include/camel/compile/gir/nodes.h` | `Node::replaceUses()` |
| `include/camel/compile/gir/graph.h` | `Graph::isMacro()` |
| `include/camel/core/rtdata/func.h` | 运行时 `Function*` 表示 |
| `src/compile/gir/builder.cpp` | `visitExitNode` 对函数值返回的 lowering |
| `test/macro/` | 宏功能测试用例 |
