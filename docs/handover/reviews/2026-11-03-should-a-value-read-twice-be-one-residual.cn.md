# 同一个值被读两次，是一个残余还是两个？

`split` 在切点列残余（residual）——**要带进另一半的东西**。一个值可能被走两条路各记一次：参数的 value port 作为"跨切点的值"被记一次，又作为"切点后那半读到的参数所代表的值"被记一次。今天两份都在表里，所以同一个值占两个成员。

这不是"多带一份更安全"：残余表的顺序是**按位置**读的，`modules/transforms/transpose.cpp` 写着"slot `at` is parameter `at`'s value"。多一份副本不改变任何数值，但会把**后面每个参数挤出自己的槽位**。`sum(x * s)` 运气好（副本排在第三，参数仍在 0、1 位），`sum(s * x)` 不是：

```
sum(x * s)  residuals [x, s, s]   槽 0、1 是 x 和 s        → 24.0 / d/dx 4 4 4 / d/ds 6.0
sum(s * x)  residuals [x, x, s]   槽 1 又是 x，s 落在槽 2  → norm-arity mismatch（改前）
```

`sum(s * x)` 改前会要 1 个 norm 口、却造出 2 个：转置去槽 1 取 `s`，那里是数组，**类型检查正确地拒绝了**（数组不是 `s` 里那个数），于是它去申请这个取不到的值，多出一个口。改后（副本排到最后、成员数不变）`[x, s, x]`，槽位对上，`24.0`、`d/dx = 4 4 4`、`d/ds = 6.0`，与 `sum(x * s)` 对称，转置图 `(float) => (float[..], float)`，`same=yes`。

## 今天的证据（为什么"去重"是另一笔，不是顺手）

我先做的是**删掉重复项**。结果：`split` 从 19/19 掉到 **16/19**——`split_two_params` 的 `residuals=5` 变成 4，`split_forward_half`、`split_product_chain` 同因。三个结果 **18.0 / 12.0 / 9.0 全对**，`split` 里只有**成员数**变了。而那个数在 `split_two_params` 的 desc 里是写明的（"five residuals … the two parameters' value ports, which cross because the tangent side reads their tangents"）——那句描述的正是这份重复。

所以今天落的是**只排不改数量**（参数各归其槽、重复副本留到最后），`split` 保持 19/19。**去重没做**，因为它是冲刺后的独立一笔：它要连带更新三个期望的成员数。

## 默认：应当去重

**默认：一个值读两次是一个残余；到那时 `split_two_params` / `split_forward_half` / `split_product_chain` 三个期望的成员数随之更新，并在提交正文写清"这是去重不是修数"。**

理由：残余是"要带进另一半的东西"，同一个值带两份**不改变任何行为、只增加成员数**——属于"能脱离实物漂移的计数"（作者 2026-11-03 13:30 那条裁定的同类：计数必须钉在实物上）。今天不改，是因为 `split` 19/19 是这一轮的判据，且去重会连带三个期望值，属于冲刺后的独立一笔；而"只排不改数量"已经把**行为**上的错（槽位错位导致转置多数一个口）修掉了，剩下的只是台账里的一个成员。