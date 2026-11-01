# 给决策者的一页状态：`split` 已落地（你 07:30 看到的是旧状态），主机正常，只剩 `transpose` 收残差这一道墙

> **这不是问题，是回你 07:30 催办里那句「如果是构建主机又不可达，在本页末尾写一行」。**
> **主机正常**：`camel-remote.ps1` 一路都在跑，最近三趟闸门都在 120 秒内建完、无一次不可达。
> **`split` 已经落地并推上 `develop`**，你看到的「卡在 `split` 的占位（`45729f6fd`）」是 06:28 之前的树。

## 今天推上去的读数（都是实测，树与 sha 都在 `develop` 上）

- **`origin/develop = adb67e081`**（此刻的下一趟已过闸门，见下）。
- **`split` 两参数的切法与残差**
  ```
  REINTERP split function=jvp_of_f … type=(float, float) => (float, (float, float)) pure residuals=2
  residual count= 2      residual r0= (3.0, 2.0)
  forward y= 18.0        tangent dx= 12.0     tangent dy= 9.0
  ```
  程序就是你点名的那条：`f(x, y) = x * x * y`，`x = 3`、`y = 2`。
- **`jvp` 抬到 N ≥ 1 个参数**（`skip=params` 在代码里剩 0 处）
  ```
  value= 18.0
  dfdx= 12.0      ← lifted(3.0, 1.0, 2.0, 0.0)
  dfdy= 9.0       ← lifted(3.0, 0.0, 2.0, 1.0)
  ```
  **与你写的 `∂f/∂x = 2xy = 12`、`∂f/∂y = x² = 9` 逐位对上。**
- **`transpose` 的循环已通**：`thrice`（三轮）`transpose(thrice)(1.0, 1.0) = 8.0`、`quad`（四轮）`= 16.0`——轮数取自循环自己的区间，没有写死。
- **keep 已在两处插入**（`replaceNode` 与分支消解）：四个后端 × 有/无 `std::opt` 共八行读数全 `1`；`GraphDraft` 净增 0（台账 35/302 未重录）。
- **M5 五条批处理规则**：`matmul` 与 `sum_axis`/`mean_axis`/`max_axis`/`min_axis`，每条 `asked=1 found=1 missing=0`，与逐行 `map` 逐位相等；整轴 `tensor:sum` 按名拒绝（`asked=1 found=0 missing=1`）。

## 现在唯一的那道墙，和正派的两条车道

**`transpose` 还不收残差。** 今天它只收 `(f: (float) => float) => (float, float) => float`，而 `split` 交出的是 `((float, float), float, float) => float`（残差两项 + 两个切向量）——所以 `stdlib/vjp.cml:33` 的 `transpose(split(jvp(f)))` 还编不过，M4 的 `12 / 9` 只能在 `split` 那一侧读到，**库层 `vjp` 还没有读数**。

按你 06:50 的裁定（「`split` 落地后，已知值换成残差，形状没变」）正在做：
- 一条车道让 **`transpose` 收残差**（残差就是已知值，按位置展开；`x*x` 那种需要前向值的图从残差里取，取不到才按名拒绝）；
- 一条车道做 **`ResidualsOf<F>` 的 C++ 实现**（今天它只是空声明，所以恒等图那条 `residual count` 是 1 而不是 0——类型给了槽位、图里没人读）。

两条都通之后，M4 立刻组装 `vjp` 并对照 `12 / 9`，`docs/stdlib-reference.md` 重新生成（`stdlib/vjp.cml` 目前还不在生成器的模块表里，因为它的 `@example` 要先能编译）。

## 闸门

最近两趟，都是 `-Plan all -Tier fast -Jobs 8` 的 `[SUMMARY]`：

```
adb67e081   total: 8175, pass: 8161, fail: 0, skip: 12, known failures: 2
5c0fc56ce   total: 8178, pass: 8164, fail: 0, skip: 12, known failures: 2
```