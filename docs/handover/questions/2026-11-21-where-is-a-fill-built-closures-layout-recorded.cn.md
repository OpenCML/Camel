# 一个由 Fill 造出的函数值，闭包布局记在哪里

**日期**：2026-11-21 12:50（+08:00）
**来源**：`jvp-closure` 车道按 `3fc9bb01d` 第 1 条落地时，在落点上缺一个句柄，停手报。

## 一、量到的（可核）

要落的是 `3fc9bb01d` 那句：

> 被变换的函数体**里面造出来的**函数值（体内的闭包构造节点、`split` 前向一半里造 `lin` 的那一步）：它的捕获是这一层算出来的值，切向就是这一层正在传播的切向。提升这个构造节点时，新闭包捕获 `(x, dx)`。

实测的障碍是**没有写入口**：

1. **闭包类型是**图级**属性，不是 Fill 的属性。** `setClosureType` 只有 `draft.setClosureType(TupleType*)` 一处（`include/camel/runtime/draft.h:680`）；`jvp.cpp` 只在**端口捕获那条路**用它（`jvp.cpp:957` / `:960`）。**闭包构造 Fill 上没有任何设置闭包类型的入口。**
2. **`FunctionType` 没有换闭包类型的方法。** 只有 `withExitType(Type*)`（`include/camel/core/type/composite/func.h:177`）与 `withResources(...)`（`:183`）；`create(withTypes, normTypes, returnType)` 只按**参数与返回** intern。**没有 `withClosureType`**，也没有「把函数类型的闭包布局换掉」的现成函数。
3. 因此 `split_of_jvp_of_sq_backward` 的闭包 `(f32,)` 是**编译期由 `split` 侧决定的函数值布局**；`jvp.cpp` 的闭包构造 Fill 分支**既读不到它、也写不了它**。

## 二、为什么这不能由车道自己定

两条路都要动本格授权之外的东西：

- **路 A（记在函数类型上）**：需要 `FunctionType::withClosureType(TupleType*)` —— 一行声明 + 一次 intern。这是**类型层的新 API**，改的是「函数类型的身份」。
- **路 B（记在 Fill 的模板/负载上）**：需要知道 encoder 怎么从 Fill 推出 `Function::tupleType()` —— 那是**编码器语义**，不在 `jvp.cpp` 的文件里。

两条都不是「改一处分支」，而是**闭包布局归谁所有**的裁定。

## 三、为什么必须先问

若先按任一条做下去：**它会绿**，而绿的来历无法与「本该如此」区分 —— 因为无论布局记在类型上还是记在编码器负载上，`jvp_vjp_f` 都会打印 `1.0 / 2.0 / 3.0`。**而这两条路的差别会在别处显出来**：记在类型上，则所有读到该函数类型的地方都会看到配对布局；记在负载上，则只有执行到那个 Fill 的地方看到。

**⇒ 所以这是「同一条绿、两种世界」，不能由实现者选。**

## 默认建议（写死，本轮按它开工）

**走路 A：给 `FunctionType` 加 `withClosureType(TupleType*)`。**

理由：

1. **闭包布局本来就是函数类型的一部分** —— `jvp.cpp:957`/`:960` 已经在**图级**设它，说明「闭包布局跟着函数值走」是既有读法；只是**类型层缺一个对应的入口**，让它能跟着函数类型走。
2. **路 B 把类型信息放进编码器的负载里，而类型层看不到它** —— 那会让「同一个函数类型的两个值有不同的闭包布局」成为可能，而 `ports == promised` 那类不变式正是靠「类型声明即真相」守着的。
3. **路 A 的影响面可枚举**：`withClosureType` 只在「体内造的闭包要配对」这一处被调用；不改变任何既有 `create(...)` 的结果（默认无闭包类型）。

**执行口径**：
- 加 `withClosureType(TupleType*)`（返回新 intern 的 `FunctionType`），**不改** `create(...)` 的签名与语义；
- `jvp.cpp` 的闭包构造 Fill 分支在**体内造的闭包**上调用它，根的 Port 捕获那条路不动；
- 判据：`split_of_jvp_of_sq_backward` 的 `closureType` 从 `(f32,)` → `(f32, f32)`；`jvp_vjp_f` → `1.0 / 2.0 / 3.0`；`grad(grad(x ↦ x⁴))(2) = 48`；`floatgate.f32_closure_member_ports` 与 `residuals_in_the_closure` 逐字 PASS；`4296/4291/0/5` 不变。

**若走路 A 时发现 `FunctionType` 的 intern 键里必须包含闭包类型（否则两个只差闭包类型的函数值会被 intern 成同一个）⇒ 那正是这条路要付的代价，如实报出，不绕。**

## 四、本页默认已作废（同日撤回）

落地的车道量到两件事，**推翻上面那条默认**：

1. **`FunctionType::intern` 的键里没有闭包位置**：`src/core/type/composite/func.cpp:197-257` 的键是 `withCount / 参数数 / exitType / hasNames / hasRest / withWithRest / constas / withConst / resources(全套) / 参数与名`；`Parts`（`func.h:308-323`）里**没有**闭包字段。**只加 `withClosureType` 而不动键，两个只差闭包类型的签名会被 intern 成同一个 —— 那正是本页第三节担心的「静默合并」。**
2. **`include/camel/core/type/composite/func.h:14-16` 有一条成文的设计声明，与本页第二节理由**反向**：**

   > Captures are not part of a function type: which names a graph captures is a fact about the
   > graph (GraphDraft::captureNames, GCGraph::captureNames), not about the type of its values.

**⇒ 而本页第二节理由 1 把方向读反了**：`jvp.cpp:957`/`:960` 在**图级**设闭包类型，**正是这条声明的体现**（闭包布局是关于**图**的事实，不是关于类型的），**不是**「类型层缺一个入口」的反证。

**裁定改为路 B**（不新增类型层 API）：

- 让**闭包构造 Fill 所在的那张图**记住它造的闭包布局：`GCGraph::closureType` 与 `draft.setClosureType(...)` **都是现成的**，`jvp.cpp:957/960` 在用的就是它们；
- `jvp.cpp` 的闭包构造 Fill 分支在**体内造的闭包**上调 `draft.setClosureType(...)`，根的 Port 捕获那条路不动；
- **`FunctionType` 一字不动，`func.h:14-16` 那条注释也不动 —— 它是对的。**

**路 B 的代价是真实的，而它是**设计**，不是缺陷**：同一个函数类型的两个值可以有不同闭包布局。**这正是 `func.h:14-16` 说的形状。**

**本页要记的教训**：一条成文的设计声明先于一份裁定页的默认。下一次再遇到「类型层少一个入口」，先 `git grep` 那个类型自己的头文件开头有没有成文声明 —— **它常常已经把答案写在那里，而且方向与「缺一个 API」相反。**