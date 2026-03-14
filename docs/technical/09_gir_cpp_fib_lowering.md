# GIR 到 C++ 的 `fib` 特化 Lowering

本文档定义 `test/run/linear/fib.cml` 的首版 GIR 到 C++ 转译契约，目标不是把现有 `Frame` 机械地搬成一个 C++ 栈上 `slot_t[]`，而是在纯数值图上尽可能把 `Frame` 完全编译掉，让 C++ 编译器直接看到参数、局部变量和结构化控制流。

## 1. 适用范围

当前契约只覆盖以下语义子集：

- 标量 `int` / `bool`
- `DATA`
- `PORT`
- `OPER`
- `BRCH`
- `JOIN`
- `FUNC`
- `EXIT`

下列节点暂不在首版 `fib` 路径中直接支持：

- `CALL`
- `BIND`
- `FILL`
- `ACCS`
- `COPY`
- `CAST`
- 任意 GC-traced 对象

这些节点在通用路径中仍可回退到运行时 helper，但不会进入“去 Frame 化”的最优路径。

## 2. 逻辑帧与物理帧

### 2.1 逻辑帧

编译期间仍保留 `data_idx_t -> 值` 的映射，这是为了：

- 对齐 `NodeVM` / `FastVM compile` 的既有语义
- 追踪每个节点结果落在哪个 GIR 槽位
- 在 `JOIN` 处做 phi 合并

### 2.2 物理帧

生成 C++ 时不再物化通用 `Frame`：

- `PORT` 槽位 -> C++ 形参
- `DATA` 槽位 -> 字面量或 `constexpr`
- 中间结果槽位 -> SSA 风格局部变量
- `EXIT` -> `return`

换句话说，`Frame` 只存在于 lowering 阶段，不存在于最终的纯数值 C++ 代码中。

## 3. 节点到 C++ 的映射

### 3.1 `DATA`

若常量是 `int32_t/int64_t/bool`，直接映射为 C++ 字面量：

- `Int32(1)` -> `1`
- `Int64(1)` -> `1LL`
- `Bool(true)` -> `true`

### 3.2 `PORT`

端口直接映射为形参。例如：

- `n: int` -> `camel::core::rtdata::Int32 n`

### 3.3 `OPER`

首版仅对纯值算子做内联：

- `:op/le_i` -> `<=`
- `:op/sub_i` -> `-`
- `:op/add_i` -> `+`

若 `OPER` 不在支持列表，则生成运行时回退调用，不进入 `fib` 的最佳路径。

### 3.4 `BRCH` / `JOIN`

`BRCH`/`JOIN` 在 `NodeVM` / `FastVM` 中通过“分支索引槽 + phi 选择”实现。
在纯数值路径中，应该尽可能恢复成结构化控制流：

- `BRCH(cond)` + `JOIN(thenVal, elseVal)` -> `if (cond) ... else ...`
- 若 `JOIN` 仅用于返回值，则直接生成 `return`
- 若 `JOIN` 仍有后继节点，则生成一个 merge 局部变量

### 3.5 `FUNC`

若目标图静态已知且无闭包，直接降为 helper 函数调用：

- `fib(n - 1)` -> `fib_impl(n - 1)`

这也是 `fib` 样例中递归调用的目标形式。

### 3.6 `EXIT`

`EXIT` 不单独生成节点，直接转成 C++ `return`。

## 4. `fib` 的期望输出形态

对 `fib(n: int): int`，理想生成结果应尽量接近手写代码：

```cpp
static inline camel::core::rtdata::Int32 generated_fib(
    camel::core::rtdata::Int32 n) {
    if (n <= 1) {
        return n;
    }

    auto lhs = generated_fib(n - 1);
    auto rhs = generated_fib(n - 2);
    return lhs + rhs;
}
```

以下形态都不是首选：

```cpp
slot_t slots[8];
```

```cpp
Frame frame(...);
```

```cpp
switch (pc) { ... }
```

因为这些形式都会把高层结构信息重新压扁，减少编译器的优化空间。

## 5. 与现有执行器的对齐关系

### 5.1 对齐 `NodeVM`

语义参考：

- `NodeVM` 中 `BRCH` 会写分支索引
- `JOIN` 再根据分支索引选择候选值
- `FUNC` 在尾位置可触发尾调优化

纯数值 C++ lowering 不复制这些运行机制，只保留其结果语义。

### 5.2 对齐 `FastVM`

lowering 参考：

- 从 `exitNode` 逆向找可达节点
- 控制输入优先于数据输入
- `JOIN` 本质是 phi
- `FUNC` 是静态已知调用

这意味着 C++ emitter 应先建立拓扑和控制流结构，再输出最终语句。

## 6. 何时回退到运行时路径

以下任一条件成立时，放弃“去 Frame 化”路径，回退到较保守的运行时感知生成：

- 出现 GC-traced 类型
- 出现 `CALL`
- 出现 `BIND`
- 出现 `:mark/*`
- 出现当前不支持的 `OPER` URI
- 无法将 `BRCH/JOIN` 重建为结构化控制流

回退策略应是“部分回退”，而不是整个图直接失败。

## 7. 当前结论

对 `fib.cml`，最优路线不是：

- “把 `Frame` 放到 C++ 栈上”

而是：

- “在 lowering 阶段利用 `Frame`/槽位语义做分析，在生成阶段把它彻底消掉”

这条路线最符合当前仓库的 `NodeVM` / `FastVM` 语义，也最有利于让 C++ 编译器释放性能。
