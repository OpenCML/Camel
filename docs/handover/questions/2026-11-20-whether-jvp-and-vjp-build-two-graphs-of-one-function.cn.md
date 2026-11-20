# `jvp` 与 `vjp` 对同一个 `f` 各造一张图 —— 两张图的边集合是不是不一致？

**状态**：开放，**按内部错误处理，不记 `known_fail`**。2026-11-20 在 `fix/compositions-repin` 上量到，
`4d7e1e9be`（基线）与 `2f90e9f03`（重钉之后）同句，`compositions.jvp_vjp_f` 的 nvm 与 fvm 两条逐字
相同，只差句子里的 pass 名。

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