# `Transpose<F>` 的那一格写不进签名：提升签名的 value half 由谁填

- 提出：2026-11-21 07:05
- 状态：未决
- 默认建议：**保持拒绝**（见 §四），本轮已按它落地
- 上一页：`2026-11-21-whether-transpose-can-say-its-signature-is-lifted.cn.md`（裁定 B：加一格）

## 一、问句

裁定说这一格**由 `liftedFrom` 推出**。可它一旦要生效，就必须出现在**调用点读到的那个类型**里，而那个类型是 `Transpose<F>`，由 `FuncTypeResolver::resolve(const type_vec_t &with, const type_vec_t &norm)` 从**两个类型表**算出来 —— 图不在手里。本页问的是：**这一格该落在哪个读法上**，因为落在签名上这条路，今天落不下去。

## 二、读数（两次，都是这一棵树上的）

`develop = 6d6374fde`，`transpose` 的成员与承诺：

```
TRANSPOSE function=jvp_of_split_of_jvp_of_sq_backward type=(f32, (f32, f32)) => (f32,) nodes=9
```

`type=` 是**成员**的类型（`transpose.cpp` 打印的是 `signature`，不是源函数的类型）：一个 value 口 `f32`，一个余切口 `(f32, f32)`，答案一个余切 `(f32,)`。

同一次运行里把两个事实打出来（临时探针，已删）：

```
lifted=1 isLift=1 placeCount=2 captures=0
```

即：`liftedFrom` 有值、lift 形状成立、**没有 capture**。所以 `ports=2` 的第二口就是**提升签名的 value half**，不是「body 读了一个值」。这与 `known_fail_reason` 里写的「值半边加一个余切」一致，而与它旁边的 `cause=needs-a-value-outside-the-closure` 不一致：那个词说的是 capture，而 capture 是 0。

承诺那一侧，`Transpose<F>` 从类型算出来是 `(ct: (f32, f32)) => (f32,)` —— **1 口**。于是 `ports=2 promised=1`。

**为什么不能靠签名补上这一口**：`transpose(lin)` 里的 `lin` 是运行期的函数值（`stdlib/vjp.cml:134` 的 `(y, transpose(lin))`），它的图不是编译期事实；而一个**调用**的类型必须是静态的。同一个 `(f32, f32) => (f32, f32)` 既可能是一份提升签名，也可能是一次 cut 的反向半边（上一页 §三 的三例就是后者），所以类型层面加格之后，这两者仍然同型 —— 加了也仍然读不出。

## 三、本轮落地了什么

1. `transpose.cpp` 的**承诺**改由同一个事实、同一个法则算出（`transposeSignature(..., liftedSignature)`，`liftedSignature` 读 `GCGraph::liftedFrom`），于是那里 `ports == promised`，剩下的差只可能是 capture 或 `ParameterShape`。
2. 差没有消失，它**挪到了真正拥有调用点承诺的那一处**：`transpose_operator.cpp#transposeKernel` 现在拿 `Transpose<F>`（`resolver` 的 exit 类型）的**端口数**去比成员的端口数（此前比的是算子自己的形参个数，恒为 1，只在成员恰好 1 口时才是对的），`!=` 即拒，句子里报**两个数**，并且按 `liftedParametersOf` 说清多出来的是提升签名的 value half、而不是 body 读的值。
3. 于是 `compositions.jvp_vjp_f` 的第一句从

```
TRANSPOSE function=... ports=2 promised=1 cause=needs-a-value-outside-the-closure
```

   变成

```
transforms: transpose has no transpose for this function: the signature is a lift, so the member takes its value half as a port (`f32`) before the cotangent, and `Transpose<F>` promises the cotangent alone (ports=2 promised=1): ...
```

   墙挪了一层：从「walk 里两个数不等」挪到「**调用点填不出提升签名的 value half**」。它仍然是红的。

## 四、两条路，各带代价

- **R1（默认建议，本轮已落地）**：承认这一格写不进签名，**保持拒绝**，两个数报出来，修的位置在**做得出这两个值的那一侧**（cut 拿得到形状并填这些端口，`split.cpp#valuePortsOf` 已经这么读；算子拿不到）。代价：`jvp_vjp_f` 继续红，而且要改的是 `split`/`vjp` 的写法，不是 `Transpose<F>`。
- **R2**：算子这一侧读**普通读法** —— 提升签名的 value half 在 `promisedToTheOperator` 时**不是端口**，答案仍是一参数一个余切。代价：与裁定「由 `liftedFrom` 推出」相反，且必须重读上一页 §三 的三例（`floatgate.f32_closure_member_ports`、`floatgate.f32_array_zero_carrier`、`residuals_in_the_closure`）：它们若读的是**算子**这条路，R2 会把它们打回去；若读的是 **cut** 那条路，R2 可以只改算子一侧。这一条本页没有量。

## 五、本页不回答的

- `Jvp<F>`/`Vjp<F>` 要不要同样加格（上一页 §七把这条留在外面）。
- R2 若成立，`vjp` 的 pullback 类型（`Pullback<F>`）要不要跟着变。