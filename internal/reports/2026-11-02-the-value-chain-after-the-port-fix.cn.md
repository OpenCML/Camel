# 端口类型修好之后：`grad_of_an_array_parameter` 的移交

**2026-11-02，`lane-m4b`。移交一件，不是本车道的题目。**

## 已经修掉的（`beeb887f4`）

被转置成员的端口类型不再是写死的 `float`，4 条边不变量归零。这一条不再需要任何人回头看。

## 移交的现象

`grad((xs) => xs[1] * xs[0])([3.0, 2.0])`，期望 `[2.0, 3.0]`。观测到的四条读数：

```
[stdout] 6.0
[stderr] (empty)
[INFO] expected exit 0, got null
[stdout] REINTERP split function=jvp_of_g forward=88 backward=89 type=(float[]) => (float, (float[], int, float, int, float)) pure residuals=5
```

## 判断，及依据

- `6.0` 是**前向值**（3.0 × 2.0），出现在程序输出里；
- 那个块里程序输出**只有 `6.0` 一行**，其余 `[stdout]` 行是编译期的 `REINTERP …`；
- **`[stderr] (empty)`**，并且没有任何 `invalid …` / `assert` / `segmentation` 一类诊断；
- `expected exit 0, got null` ⇒ **没拿到退出码**。

⇒ **形状是"被信号中止或没跑完"，不是"跑完了但值错"**：跑完了会有退出码，值错会有第二行输出。

## 没有观察到的

**崩溃点没有观察到，因为观测手段不允许。** 仓库的远程入口只吃 plan 文件或目录（`-Plan`），没有"跑一个文件"的模式；`-Check` 是仓库自己的检查器，不是任意命令。要看退出状态或信号，必须直连那台机器，那越出了这条规矩。**写这一句比猜一个崩溃点有用。**

## 结论

边不变量已归零、签名契约未动、端口类型有两侧证据 ⇒ **剩下的缺口在 vjp/split 的值链上**，与转置无关。

## 用例的处置

`test/cases/compile/transforms/grad_of_an_array_parameter.cml` 已写好，**期望值 `[2.0, 3.0]` 是对的、不改**；它与 `transforms.plan.toml` 的登记一起**留在 stash 里不提交**。等值链修好后取出并登记。**不把它登记成 `known_fail`**：没修好的东西不进"这是对的"那一栏。