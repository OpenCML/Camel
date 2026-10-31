# B 类 33 条：三个算子各接手哪一步

对象：`docs/handover/m4-autodiff-case-classes.cn.md` 的 B 类（33 条，考的是图结构）。
设计稿第 4 节把结构分给三个算子，每条结构只由**一个**算子处理一次，其余算子接着往下传：

| 结构 | `reinterp`（4.1） | `split`（4.2） | `transpose`（4.3） |
|---|---|---|---|
| 分支 | 条件原样保留，两臂重新解释 | 条件是残差，后一半按同一条件选臂 | 同一条件选转置后的臂 |
| 调用 | 换成 `reinterp<H>(被调函数)`，同一被调函数配同一 `H` 只生成一份 | 被调函数各自拆分，残差按调用嵌套 | 线性被调函数的转置 |
| 循环 | 状态按 `Lift` 换掉，循环体重新解释 | **每轮残差作为收集输出保存；后一半按同样轮数执行，逐轮取残差** | 倒着走，逐轮取 `split` 留下的残差 |
| 效应 | `H` 没有规则时拒绝；前向规则集让效应原样执行、切向量为零 | 效应只在正向 | — |

三个数：**照旧 22 条 / 改写 7 条 / 消失 4 条**（22 + 7 + 4 = 33）。

## 分支与调用/闭包（8 条）

| # | 用例 | 谁接手 | 判定 |
|---|---|---|---|
| 1 | `control_flow_grad` | `reinterp` 分支步骤（两臂各自解释）+ 调用步骤（递归按调用处理） | 照旧 |
| 2 | `grad_closure` | `reinterp` 调用步骤：闭包是调用节点，闭包体建图期已知 | 照旧 |
| 3 | `grad_function_values` | 同上 | 照旧 |
| 4 | `grad_function_values_arms` | `reinterp` 分支步骤 + 调用步骤 | 照旧 |
| 5 | `grad_function_values_effects` | 上述两者 + `reinterp` 效应步骤（效应原样执行、切向量为零） | 照旧 |
| 6 | `grad_unexpanded_inline` | **无对应物**：前提是“求导留给程序运行期（不展开）”，库层路线在建图期就完成 | **消失** |
| 7 | `grad_specialized_callee` | **无对应物**：前提是被调函数留到化简后才特化 | **消失** |
| 8 | `grad_unexpanded_specialize` | **无对应物**：前提是“grad 留在程序里跑”，`reinterp` 之后不存在这个状态 | **消失** |

## 循环（22 条）

“等 M2 的哪一半”一栏是**后一半（读残差的那一半）**：循环携带的状态量正是 `split` 要作为残差
收集输出的东西；前一半只跑正向、把每轮残差存下来。

| # | 用例 | 谁接手 | 等 M2 的哪一半 | 判定 |
|---|---|---|---|---|
| 9 | `loop_grad_sum` | `reinterp` 循环步骤 + `split` 循环步骤 | 后一半：每轮残差作收集输出 | 照旧 |
| 10 | `loop_grad_product` | 同上 | 后一半 | 照旧 |
| 11 | `loop_grad_affine` | 同上（int 分量无切线，`Lift` 逐分量） | 后一半 | 照旧 |
| 12 | `loop_grad_tuple_state` | 上述 + `Lift<Tuple>` 同形 | 后一半 | 照旧 |
| 13 | `loop_grad_struct_state` | 上述 + `Lift<Struct>` 同形 | 后一半 | 照旧 |
| 14 | `loop_grad_zero_rounds` | 上述；但断言里“tape 为空、反向不跑”是旧机制的说法 | 后一半 | **改写** |
| 15 | `loop_grad_nested` | 上述；外层每轮收集的是**内层的反向闭包**，新路线收集的是残差 | 后一半（内外各一层） | **改写** |
| 16 | `loop_grad_capture_table` | 上述 + 捕获数组的读取规则；断言点名了具体算子名 | 后一半 | **改写** |
| 17 | `loop_grad_iterable` | 上述，fvm 调度反向循环 | 后一半 | 照旧 |
| 18 | `loop_grad_iterable_records` | 上述，记录数组逐位相同 | 后一半 | 照旧 |
| 19 | `loop_grad_iterable_tensors` | 同上 | 后一半 | 照旧 |
| 20 | `loop_grad_record_tensor_fd` | 同上，有限差分对照 | 后一半 | 照旧 |
| 21 | `loop_grad_iterable_length` | 上述；断言“反向循环里没有数组梯度算子”是图形状 | 后一半 | **改写** |
| 22 | `loop_grad_literal_array` | 上述（数组字面量，无端口） | 后一半 | 照旧 |
| 23 | `loop_grad_parameter_array` | 同上 | 后一半 | 照旧 |
| 24 | `loop_grad_refuse_no_element_tangent` | `reinterp` 循环步骤 + `transpose` 循环步骤（倒着走） | 后一半 | **改写**（名字说“拒绝”，实际是正例） |
| 25 | `loop_grad_tensor_iterable` | 同上，macro 预构建梯度 | 后一半 | 照旧 |
| 26 | `loop_grad_refuse_slice_iterable` | `reinterp` 循环步骤 + 读取规则 | 后一半 | 照旧 |
| 27 | `loop_grad_refuse_capture` | 同上，macro 收集 | 后一半 | 照旧 |
| 28 | `loop_grad_in_place` | `split` 循环步骤；断言“原地写需要 tape”是旧机制 | 后一半（原地写要靠残差而不是 tape） | **改写** |
| 29 | `loop_grad_composite_read` | `reinterp` 循环步骤 + `split` | 后一半 | **改写** |
| 30 | `loop_grad_closure_in_body` | **无对应物**：钉的是旧 tape 构造器读到已删节点的段错误 | — | **消失** |

> 第 30 条编号与上表对齐：上表共 22 条循环用例（9–30）。

## 效应（3 条）

| # | 用例 | 谁接手 | 判定 |
|---|---|---|---|
| 31 | `grad_effects_kept` | `reinterp` 效应步骤（原样执行、切向量为零）+ `split`（效应只在正向） | 照旧 |
| 32 | `grad_captured_var_written` | 同上；写是效应，版本是资源状态 | 照旧 |
| 33 | `grad_module_var_sync` | 同上，`std::opt` 在调度之前的排列也要成立 | 照旧 |

## 消失那 4 条要交代的事

`grad_unexpanded_inline`、`grad_specialized_callee`、`grad_unexpanded_specialize`、`loop_grad_closure_in_body`
的前提都是**旧机制的一个状态**（求导不展开、特化后才建、tape 构造器读旧节点）。
新路线里这些状态不存在，所以拒绝/断言也消失。**但“这件事有人管”不能跟着消失**：
四条各自要有一条新用例钉住等价性质（函数值可求导、特化后仍可求导、闭包在循环体里可求导、建图期读的是当前图），
否则删掉的是**覆盖**，不是**失效的断言**。

## 判据

「消失的拒绝要变正例——删掉一条拒绝，等于把‘这件事有人管’也一起删了。」

## 边界

只写表，未改 `modules/autodiff`；循环那一堆的“等哪一半”统一标为 `split` 的后一半（读残差的那一半）。