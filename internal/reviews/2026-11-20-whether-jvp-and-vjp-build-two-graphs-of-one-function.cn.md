# `jvp` 与 `vjp` 对同一个 `f` 各造一张图 —— 两张图的边集合是不是不一致？

**状态**：**问句已量完（§6），造图那一半修在 `991ee512c`；用例仍红，停在 §7 的另一堵墙，不记 `known_fail`。**
2026-11-20 在 `fix/compositions-repin` 上量到，`4d7e1e9be`（基线）与 `2f90e9f03`（重钉之后）同句，
`compositions.jvp_vjp_f` 的 nvm 与 fvm 两条逐字相同，只差句子里的 pass 名。

## 1. 当前读数（逐字）

```
():?:?: [Error]: Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of___g_359__'
breaks its edge invariants (1 violation(s)):
  graph 'jvp_of___g_359__' node 4 (10): calls 'jvp_of_vjp::vjp<(x: f32) => f32 pure>', whose value
  is a (f32, f32) => ((f32, f32), (f32, f32) => ((f32,), (f32,))), and is a
  ((x: f32) => f32 pure, ()) => ((f32, f32) => ((f32, f32), (f32, f32...
```

exit 1，`stdout` 是这一句，`stderr` 为空。

## 2. 三件让它不是「期望钉错」的事

1. **它不是 `transforms:` 拒绝**，是 `Pass 'std::nvm' failed with an internal error`：**调度器在图外面
   发现不变量被破坏**。拒绝是我们写下的法则；内部错误是没人写下过的状态。
2. **`agents/test.md` 的规矩**：内部错误（可能崩的东西）不记 `known_fail` —— 记它等于给它发豁免，
   而下一次它会以同样的形状崩在别的 case 上。
3. **两条后端只差句子里的 pass 名**（`'std::nvm'` / `'std::fvm'`），而该条目写着
   `backends_agree = ["exit_code", "stdout_normalized"]`，所以报告里那行
   `differs from compositions.jvp_vjp_f.nvm: stdout_normalized` **是 pass 名的差，不是两个后端的语义
   差**。这一读法要写进条目，否则下一个人会把它读成两后端分歧。

## 3. 图里那一句说的是什么

`node 4` 是一个 `Func` 节点（`(10)`），它**调用** `jvp_of_vjp::vjp<(x: f32) => f32 pure>`，而两个类型
并排写出来、**不相等**：

- 这个节点**是**的：`(f32, f32) => ((f32, f32), (f32, f32) => ((f32,), (f32,)))` —— 两个普通参数，
  返回「值 + 拉起函数」的一对；
- 这个节点**被当成**：`((x: f32) => f32 pure, ()) => ((f32, f32) => ((f32, f32), (f32, f32...` —— 它的
  第一个参数是**函数**，不是 `f32`。

⇒ 同一个 `f` 上，`jvp` 认的是「`vjp` 的已割形式」，而 `vjp` 自己那张图认的是「未割的 `f` 的 vjp」。
这正是 2026-11-20 `lane-jvp-closure` 那一族改动（`2da9735da` 给的是提升答案类型的那条法则）最可能
带出来的形状：提升开始**吃掉**原本不会被提升的答案类型，于是两个方向对同一个 `f` 造出了边集合
不同的两张图。

## 4. 三条路与代价

- **A（本页默认）先把两张图的边集合各打一遍**：对 `jvp(vjp(f))` 与 `vjp(f)` 各自那份图用
  `std::jgir` 取边集合，比较是**边少了**、**边多了**，还是只有**一个节点的类型被挪位**。代价：一次
  带 `-Jobs 8` 的运行（宿主约 2 分钟），读数直接进本页。
- **B 把它们合成一张图**：让 `jvp` 去问 `vjp` 已经造好的那张，同一个 `f` 只造一张。代价：要先有 A 的
  读数说两张图真该相同；反过来，如果 `vjp` 的那张本来就是割后的形式，合并会把两个阶段绑死。
- **C 先加一条 `--check` 让内部错误在编译期就红**：代价：它只让信号更早，不回答「哪张图对」。

## 5. 默认（写死，按它开工）

**A。** 在 A 的读数到手之前**不动 `modules/transforms`、不改这条用例的期望**：它保持红，
`known_fail_msg` 不动。

条目里现在钉的 `jvp has no forward transform for this function` 是**过时**的（它标的是提升从未
发生，而现在的读数说提升发生了、并且造出了一张违反边不变量的图）—— 但**过时不等于可以换成这一句
内部错误**：把「可能崩」写成期望，就是本条最不该做的事。

## 6. 2026-11-21 读数：不是两张图不一致，是 `jvp` 把一次**调用**写成了**函数值**

本页 §4 的路 A（把两张图的边集合各打一遍）**不需要了**：那条违例句自己已经点名了节点、被调者与两个
类型（§3 就是这句话的读法），A 只会把同一件事打得更长。顺着 §3 进 `modules/transforms/jvp.cpp` 的 walk
就能读到原因：它判「这个 `Func` 节点是**调用**还是**函数值**」用的是**节点结果类型的种类**（是
`Function` 就当函数值）。而 `vjp(f)` 的答案正是一个函数（`Vjp<F>`），于是这次**调用**被当成**函数值**走：

1. 它的实参一根都没接 —— `if (!isValue)` 把 `setNormInputs` / `setWithInputs` 挡在外面；
2. 节点类型被写成 `jvpSignature(callee->funcType())`，即**被调者自己的提升签名**，而它本该是**被调者交出
   的答案**的提升 —— 逐字正是 §1 里并排的那两个类型（`(f32, f32) => …` 与 `((x: f32) => f32 pure, ()) => …`）。

⇒ 根因是**判据**，不是「`jvp` 与 `vjp` 各自造了一遍」。修在 `991ee512c`（`fix/jvp-vjp-f-invariant`），三处、
一个法则（`Lift<A => B> = Lift<A> => Lift<B>`，`modules/transforms/lift.h`）：

- `isFunctionValueNode`：问**被调者自己的函数类型**，不问答案的种类 —— 答案是个函数的调用仍然是调用；
- `Func` 节点的答案一律按 `liftedShapeOf` 提升，函数答案 ⇒ 提升后的闭包（`valueOf`），不是一对；
- 调用穿过去的那个值是**被调者的提升自身**，由唯一一处把法则拼成节点的地方交出（`liftedValueOf`）：值被
  原样携带时是 `jvp(v)`，walk 已经写过提升时就是那个值本身（`jvp` 的提升再提升一次就是提升的提升）；
  那个节点的类型写**答案**（`Lift<F>`），不写算子签名 —— 图的出口交出闭包时同理，交的是提升自身。

## 7. 修后它停在哪（逐字）

边不变量不再被破、出口不再交出错类型的值；`jvp_vjp_f` 停在**再后面一堵墙**：`transpose` 在提升后的反向图
上拒。runner 把 `stdout` 截在两百字符左右，这句的格式串在 `modules/transforms/transpose_operator.cpp` 的
`TransposeRefusalCause::CallValueMissing` 一支：

```
(jvp_of___g_362__):?:?: [Error]: An error has occurred: transforms: transpose has no transpose for this
function: the callee's transpose was built, but the parameter at position 3 reads a value this call does
not carry. The runtime has no d…
TRANSPOSE function=jvp_of_split_of_jvp_of_sq_backward skip=call-value-missing param=2 node=4 arg=0
```

⇒ 这就是 `2026-11-20-the-tangent-argument-record-is-not-written-by-the-transform` 的 §七那件：`split` 复制
一个提升调用时照抄四个实参，而不是把它还原成原调用（`v0, v1`）。那一页 §五写明它要与 `split` 的判据**一次
落地**，所以**不在本车道**；而本页问的那件事（两张图的边集合是不是不一致）已经有答案了。

**默认**：这条用例保持红、**不记 `known_fail`**（内部错误已经不存在，但把一句拒绝钉成期望仍然是给缺陷发
豁免），`known_fail_msg` 一个字不动。修前修后 `test/plans/feat/transforms` 全目录**逐条相同**
（71 条，pass 44 / fail 17 / known 10），`compositions` 的 pass 6 / known 6 也不动。