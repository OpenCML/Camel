# 认领顺序不是根，计数才是

2026-11-17，`lane-sortd` 量的一页。**给下一个想「只要把 `ssaAssignmentOf` 挪到 `inPlaceRebindingOf`
后面就好了」的人看的**：那条路上没有任何东西把「另一个还活着的名字」计入，程序会静默印错值。

## 1. 那个程序，和那一次测量

用例 `test/cases/consistency/new_values/sort_rebinding.cml` 的 `main`，第一行打印的是 `before` 与
排过序之后的 `xs`（`xs = sort(xs)` 之后新旧两个名字都还在，`before` 必须还是原顺序）。基线
（`fe4dd1afa`）在 `main` 上的读数是：

```
camel inspect --copies → main updates 9 in-place 0 by-count 0 declared 0 copied 9
```

改法（已回退）：让 `inPlaceRebindingOf` 接受 SSA 目标，并在语句的值没人取时，让
`ssaAssignmentOf` 先试就地形。读数立刻变成：

```
main updates 9 in-place 0 by-count 9 declared 0 copied 0
```

`new_values.sort_rebinding.graph` 因此拿到 `array:sort_d_in_place`、`feat/modules/array_sort_kernels.paths`
也翻绿。**但程序印出来的是：**

```
期望   [3, 1, 2] [1, 2, 3]
实际   [1, 2, 3] [1, 2, 3]        ← `before` 被一起排了，两个名字指向的同一个数组被就地排序
```

`kept` 同样错：`[-1, 0, 1, 2, 3]` 而不是 `[0, 1, 2, 3]`。连带 `sort_rebinding.{nvm,fvm,jit,prl}`、
`append_new.*`、`peek.*`、`macro_append.*` 一起红（27 红 / 65）。整段改动以
`git checkout -- src/compile/gir/builder_operators.cpp src/compile/gir/builder.h` 撤回，源树回到基线。

## 2. 机制，一句话

**`var` 的两个名字在运行时是同一个持有者**：`xs = xs->sort` 之后，`xs` 与先前那个 `let` 指向同一块
存储；`Array::writableInPlace()`（`src/builtin/operators/array.cpp`）只看
`block_->unique() && block_->camelOwned()`，**它看不见另一个名字**，于是答「独占」。这个答案是真的
（存储确实只有一块），**但它不是「没有别人还要读」的证明**。

换认领顺序正是把「就地」交给这个答案：`in-place` 拿到了，值跟着丢了。

## 3. 判决

**认领顺序不是根，计数才是。** 缺的东西是「一个值有几个活着的名字，就有几个持有者」——
`docs/handover/plans/2026-11-16-direct-ssa-construction.cn.md:84` 的 **S3b**，其判据是
`camel inspect --copies` 对 `v = v->append(x)` 循环与案例训练步报「原地」；**S3b 不过，第一件不算完成**。
作者 2026-11-17 对「调用之后参数归谁」的裁定把同一条讲成了语义：参数绑定是一个持有者，在调用方的
最后一次使用处交出，否则加上——所以**只有计数为 1 时才可能复用**，没有移动，也没有跨过程分析。

## 4. 另一条线的独立证据（同一个方向，反着量到的）

`src/execute/graph_runtime_support.cpp#writeRuntimeFillSlots`（:1110）把值填进 Tuple／Array／
Struct／闭包捕获时是**直接 set，不加一**。于是
`let w = zeros(...); let p = {weight: w}; sgd(p, ...)` 一开内核写回就改掉了 `w` 的那块存储。
`holder` 那条线的原话：**`sgd_leaves_its_argument` 现在绿，靠的是「内核分配」，不是「计数」**。

两条线、两个方向、同一句话：**`writableInPlace()` 说「独占」时，那个答案不是证明。**
两条线都选择了回退，而不是让它变绿。

## 5. 三条给下一个人的

1. **不要用「挪认领顺序」去换 `in-place`**：先问「另一个活着的名字算进去了吗」。没算进去，就是
   静默印错值——不是测试错了，是程序错了。
2. **进场前提是诚实的计数**（S3b），以及 `writeRuntimeFillSlots` 的填充要记账。两者都是实现，不是
   语言规则。
3. **判据要两件事一起读**：`camel inspect --copies` 的 verdict **加上**程序的输出值。verdict 从
   `copied` 变 `by-count` 而值变错，是这条线上最典型的假绿；反过来，值对而 verdict 是 `copied`，
   是 S3b 还没做，不是缺陷。

配套：`docs/technical/62_generator_type_has_two_answers.md`（另一个「一个东西两个答案」的形状，
生成器体里的闭包），以及 `test/plans/feat/consistency/new_values.plan.toml` 的 `new_values.sort_rebinding.graph`
——它今天仍是 develop 上的一条真红，按 `agents/test.md`「可运行程序的真实编译器内部错误不打 known」，
它**留在 fail 里**。