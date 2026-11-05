# 2026-11-05 `tree::arity` 对数组类型返回任意数；没有一条用例断言过「余切是数组」

两件独立于 `f32[3]` 转置的缺陷，各自只要读一次代码。**它们都在这条只是「让数组结果也能转置」的路上被撞出来，
但都不需要数组投影实现成功就能确定。**

## 一、`tree::arity` 对非元组、非结构体的类型没有答案，却会读另一种布局

`include/camel/core/type/tree.h#arity`：

```cpp
inline size_t arity(const Type *type) {
    return type->code() == TypeCode::Struct ? static_cast<const StructType *>(type)->size()
                                            : static_cast<const TupleType *>(type)->size();
}
```

**除了 `Struct`，它一律把类型当 `TupleType` 读。** 传进一个 `f32[3]`，它不报错、不返回零、也不落默认值，
它**读数组类型对象开头那几个字节当元组的成员数**——实测 `arity(f32[3]) == 131961841501032`。
**这比「查不到就落默认值」更坏：落默认值至少是一个说得出理由的数，这个数是另一份内存的解读。**

**`assert` 不是修法。** `NDEBUG` 把它从发布构建里编掉（`npm run build`），于是它只在开发机上响、
在用户那里不存在，而代码因为「有 assert」看起来被保护过。**「留一个不生效的守卫比不留更糟」的字面版。**
所以答案是发布构建里也生效的形态：`include/camel/core/type/tree.h#arityOf` 返回 `bool` + 出参，
非元组/非结构体一律 `false`，调用点当场处理。

### 已按这条改掉的（`modules/transforms/transpose.cpp`）

| 位置 | 处理 |
|---|---|
| `modules/transforms/transpose.cpp#resultCotangentRefusal` | 拿到 `false` 就说明「这个类型没有成员，而聚合的余切是逐成员定义的」 |
| `modules/transforms/transpose.cpp#accumulate` | 拿到 `false` 返回 `kInvalidNodeRef`（这之前会按「若干成员」遍历一个非聚合） |
| `modules/transforms/transpose.cpp`（`Join` 的槽数检查） | 拿到 `false` 按 `Component` 拒绝并点名 `why=not-a-type-with-members` |
| `modules/transforms/transpose.cpp#memberCountOfType` 及另外两处 | **先判 `Tuple`/`Struct` 再问 `arity`**，问题因此是有定义的；数组那一支按秩 1 常量维度取 |

### 仍然在给数的调用点（本笔只列清单，各自该走哪条路）

- `modules/ckpt/src/checkpoint.cpp`（`writeMembers` 一类的成员遍历）：**路径是「值必须是元组或结构体」**，
  该走 `arityOf` + 一条存档诊断，而不是默认值；
- `src/compile/gir/builder_static.cpp`（多值展开、静态元组长度的四处）：**数组值若到这些位置，问的是
  「这个值有几个成员」，答案应当是拒绝**，不是零；
- `src/compile/gir/builder_when.cpp`（`when` 的分支值展开）：同上。

**为什么没有把 `arity` 本身翻成「无答案」**：全树 `for (size_t i = 0; i < arity(type); ++i)` 这种循环有二十
余处，直接让它返回 `SIZE_MAX` 会让每一处**空转而不是报错**——那是把「读错数」换成「挂住」。所以顺序是：
先把调用点搬到 `arityOf`（看得见的 `false`），最后一个调用点搬完时 `arity` 才能收掉，**这一步记成具名欠账，
不是「以后再说」**。
## 二、没有一条用例断言过「余切是数组」

要撞到数组余切，需要**一个结果类型是数组的函数被 `transpose`/`vjp`/`grad`**（余切参数的类型才是数组）。
全树查过：

- 91 条用例调用 `transpose(`，其中出现数组形状的，全部是**张量的 `transpose`**（矩阵转置），不是 AD 变换；
- 124 条用例调用 `vjp(`，其中结果类型是数组的 11 条（`tensor_grad_check`、`macro_round_lifetime_gc`、
  `export_train_step_vjp` 等）——**它们的 `vjp` 目标是标量函数，数组只是参数或常量**；
- 即「`transpose` 接一个结果类型是数组的函数」这一格**一条用例都没有**。

**这解释了 `f32[3]` 为什么直到今天才撞墙**：不是没人试，是这一行从来没被执行过——数组余切的读写路径
是**未被测试的代码**。补上这一格时，第一件该做的事是让它在拒绝表里有一条名字（今天的兜底已经做到了），
第二件才是让那条路径工作。

**实测（2026-11-05，`merge-train-2`）**：把上面那条守卫的成员数改对之后，同一用例从「具名拒绝」变成
`killed by a signal`——计数对了、逐成员投影建出来了、**VM 跑不了**。所以这条路径缺的不只是判据，还有
执行侧的一环（数组按下标取成员不是元组按下标取成员的同一条路）。

## 结论与下一步

两件独立的事都可以先落：`arity` 的答案要能被调用点看见（本页第一条，`arityOf` 已落地三处），以及
「结果类型是数组」这一格要有用例（本页第二条）。**数组投影本身排在它们之后**，判据是逐元素可分辨
（`a=1.5 b=2.5 c=7.25`），且兜底对数组构建不再发话、对其它种类仍然发话。

## 顺带一条机器读数

`2026-11-05` 闸门 55 的 `plan 687.0 s` 是这一夜第一次落在 `674–684 s` 带外（最高值）。**单次不证明什么**
——判据是连续三次在带内才排除「是这次改动」；**若接下来三次继续往上，那是机器的事，不是代码的事。**