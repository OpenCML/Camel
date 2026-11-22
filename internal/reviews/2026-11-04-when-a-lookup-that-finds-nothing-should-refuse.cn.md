# 查不到的时候，什么时候该拒绝、什么时候该答默认？

普查里 15 条“查不到就答默认”的候选，只有一条能把“默认是错的”量出来。这一页记录判据和三个实测，
供作者裁定；我按默认建议那一栏改了。

## 三个实测

1. **默认错，拒绝对** —— `jvp.cpp` 的 `for` 循环：被迭代数组的**元素**切向没有地方放，代码写
   `zeroConstantFor(端口类型)`，于是
   `f(xs) = loop s = 0.0 for e in xs do s + e` 在 `jvp(f)([1.0, 2.0], [1.0, 1.0])` 上答
   `value= 3.0 tangent= 0.0`，而 `f` 就是求和，切向是 `1 + 1 = 2.0`。零是切空间的零，答给了一个
   不是零的导数，且没有诊断。**同一个程序的 `transpose(f)` 已经拒绝这个形状**
   （`skip=range` → “the shape this program used has no transpose (the transform named it `range`)”），
   所以前向半场的 0.0 不是“支持”，是“说谎”。改法：在 lift 里拒绝并点名构造。

2. **默认是对的，不改** —— `layout_ops.cpp#elementAtInfer` 的 `t.dtype.value_or(Float64)`：
   内核 `pick_at` 取的就是元素槽本身，f32 / f64 / int / 泛型 `T[..]` 四种实测都带着正确类型；
   `utility_ops.cpp#indexInfer` 的 `return Type::Float64()` 同理 —— 核心解析器先答，这里只服务
   “元素类型没定”的形状，而今天源码里**没有那种形状的拼写**（`Tensor` 别名已删除）。这类不是默认，
   是**重复的答案**，正确的处置是留一行注释写上这次测量，不是改成拒绝（拒绝一个正确程序比默认更糟）。

3. **默认不可达，只记录** —— `creation_ops.cpp#resultElement` 的 `… : TypeCode::Float32`：
   只有 rank-0 结果的类型不是数组时才落到默认，而 `zeros::<f64>([])` 在调用处就被拒绝
   （`NoMatchingFunction`：`int[]` 不接受 `[]`）。`reduction_ops.cpp#floatLikeFirst`、
   `gradient_ops.cpp#permuteInverseInfer` 同族：唯一的读者是**重推者**
   （`onnx/exporter.cpp#inferFacts` 调 `def->infer`），运行时的值类型由写它的那条规则给。
   给不可达的默认加一个拒绝分支，只增加一条测不到的路径。

## 建议的判据（我按它执行）

**能证明“默认与真答案不同”就拒绝；能证明“默认与真答案相同”就留注释；两者都不能、且没有用例能走到
就只记录。** 判据的区别不在“有没有查不到”，在**有没有一条能走到的用例**：一张查不到的表在没有读者的
时候不是缺陷，是一个待填的位置。

## 这一条留给作者

第 1 条的改法是**拒绝**而不是**算对**：算对要给循环的迭代序列同时带上“值数组 + 切向数组”，
元素端口变成一对 —— 而循环只迭代一个序列。这是形状问题，不是类型问题，所以没有在今晚拍。
如果作者要的是算对，这个拒绝的用例（`jvp_loop_iterable_tangent`）就是那条新形状的红。默认建议：
**先拒绝**，把静默的错数换成诊断；等形状落地再改成算对。