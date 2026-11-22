# 为什么 `sqrt`/`log`/`divide` 的两腿用例停在“一个参数拿不到余切，因为没有东西用它”

2026-11-04 05:35，`integ2/rules-batch2` @ `fab17edef`，主机 `88zjwei`，`-Tier fast -Jobs 4`，
1m load 13.6 — 读数属于这棵树这一刻。

## 现象

`test/cases/compile/transforms/{sqrt,log,divide}_gradient_both_legs.cml` 的**库层腿**停在一句
和另外四条不同的拒绝上：

```
transforms: transpose has no transpose for this function: a parameter holds no cotangent:
nothing uses its value
```

同一批里 `abs`/`rsqrt`/`maximum`/`minimum` 四条仍停在
`skip=cotangent-needs-a-value uri=tensor:multiply`。也就是说七条里出现了**两个墙**，而这两个墙
都不在这几条用例的预期里：用例要的是“两腿逐位同”的数。

## 已知

- 这句拒绝来自 `modules/transforms/transpose_operator.cpp#explainRejection` 的**参数**分支：
  一个参数的类型没有余切空间（或它在被转置的图里没有消费者）时，`transpose` 按名字拒绝。
- 这七条的程序形状相同：`total_plain(xs) = sum(f(xs))` 经 `vjp` 走 `transpose(split(jvp(f)))`，
  `rhs` 走到 `mul_tensor_t`；`abs` 等四条停在那条律读开秩操作数形状的那一步。
- `lane-multensor` 已用读数证明 `mul_tensor_t` 没有安全写法（`sum_to` 的第二实参必须是
  **目标形状**，换另一个操作数在广播下答错数），并已回退。
- 本批之前（`c4391db57`）这七条都在 `multiply` 那一句上；`sqrt`/`log`/`divide` 换句子是
  **本批合并之后**的事。换句子的那三支改动里，最可能相关的是 `lane-fwdclosure`
  （`wantsCotangent` 对闭包成员答 `false`、`reachesAParticipant` 走两张表）、`lane-withport`
  （`split` 给每个捕获值声明一个闭包端口）与 `lane-f32trans`（律实例元组改成
  `lawInstanceTypes` 一处决定）。

## 没定的事（要作者或一条小车道判）

1. 这三条**本来就该**走到参数检查上（因为 `lin` 里某个参数确实不被读），还是本批把“闭包成员的切向”
   和“参数的值有没有被用”混成了一件？如果是前者，用例的期望要跟着这面墙重写；如果是后者，
   修的是 `transpose_operator.cpp` 里那条判据（或 `transpose.cpp` 交参数的方式）。
2. 若是后者，`abs`/`rsqrt`/`maximum`/`minimum` 四条为什么没换句子——它们的 `lin` 形状差在哪。

## 我这一轮怎么处置（默认建议）

- **不把这三条的 `known_fail` 藏起来，也不把期望数改小**：三条的 `known_fail_msg` 改为
  `a parameter holds no cotangent: nothing uses its value`，`known_fail_note` 里写清“它停在
  乘法律**之前**的那一步”，这样换句子时闸门会红、会把人叫来看。
- **不在这条车道上改 `transpose_operator.cpp`**：判据属于转置的签名与参数，本批已经动过三次
  （`exitfix` 的值侧类型、`fwdclosure` 的闭包成员、`f32trans` 的实例元组），再叠一次没人能说清
  是哪一次改的。这条要么归 `lane-valports`（它正在改签名与值端口），要么单开一条。
- 推荐：**先把这批按现状合，读数如实记，等 `lane-valports` 落地后重跑这三条**。如果到时还是这句，
  再单开一条车道按上面第 1 问的两种可能逐条量。

（这一页由合并车道在 2026-11-04 05:5x 写，因为“读数变了但没人知道为什么”比红更贵。）
