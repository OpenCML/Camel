# `transpose` 收残差：量到哪、卡在哪

作者 06:50 的裁定要的是「已知值从参数换成残差，形状不变」。这一轮把**类型那一半**打通了、把**图那一半**的墙找到了，但**没有给出 `(12.0, 9.0)` 这两个数**——原因是墙上有一个还没做的事，不是参数不对。这一页记的就是那面墙。

## 一、`split` 的后一半实测长什么样

`test/cases/transforms/split/split_two_params.cml`（`f(x, y) = x*x*y`，x=3, y=2）在这台机器上的读数：

```
REINTERP split function=jvp_of_f forward=69 backward=70 type=(float, float) => (float, (float, float)) pure residuals=2
REINTERP split op signature=(f: (float, float) => (float, float)) => ((float) => (float, (float)), ((float), float) => float) resolved=((float, float) => (float, (float, float)) pure, ((float, float), float, float) => float pure) residuals=2
forward y= 18.0
tangent dx= 12.0
tangent dy= 9.0
```

后一半的类型正是 `((float, float), float, float) => float`：**一个元组参数装残差，后面是两个切向量**。残差 `res` 是 `(3.0, 2.0)`。

## 二、类型那一半已经通了（这一轮做的）

`transposeSignature` 现在按形状分两种：

| 被转置的 `g` | 转置出来的类型 |
|---|---|
| `(float) => float` | `(float, float) => float` —— 今天直线/分支/循环的那一支 |
| `((float, float), float, float) => float` | `((float, float), float) => (float, float)` —— 残差 + 余切进，两个余切出 |

判定用的是**位置与形状**：第一个参数是元组（里面全是 `float`）= 残差捆，其后每个 `float` = 一个切向量；转置的结果是它们的余切组成的元组。这不是我发明的约定，是 `split` 生产出来的形状，我在页里记下来是为了让下一个人知道**它是被读出来的，不是被指定的**。

配套的还有三处，都在 `modules/transforms/transpose.cpp` 里：

1. **端口**：残差捆一个端口（类型是那个元组），余切一个端口。
2. **`forward(port)`**：第 0 个端口（残差捆）原样；第 i 个端口（第 i 个切向量）的已知值 = `accs(残差捆, i-1)` —— 这就是「残差就是为前向值准备的」那条裁定落到代码里的样子。
3. **已知值不再只认常数**：`mul_t`/`div_t` 里那个「另一个操作数」现在走 `knownValue()`：常数直读，别的走残差。取不到才 `skip=residual`。

## 三、墙：后一半的图里有一个 `Call`

图那一半走下去，在**第 11 个节点**停住：

```
TRANSPOSE function=split_of_jvp_of_f_backward skip=kind node=11 kind=8
```

`kind=8` 是 **`Call`**：`split` 的后一半不是把 `x*x*y` 内联展开的纯 `:op` 链，它**调用另一个图**（前向图）来取它要的值。所以「转置一个调用」是这一笔真正缺的那一步，而它有自己的一整套做法：给被调用的图建一份自己的转置（`jvp` 那边叫 `callees`，一棵调用图一份），再把调用点换成「调它的转置、参数照旧接」。

**这一步当时没有做。** 它现在是做的了的，见第五节。

## 四、当时的状态（这一轮之前）

- **通了的**：类型、端口、残差取已知值。**拒收的位置从类型检查挪到了走图上**——`transpose(lin)` 是「被接受但图转不了」，不是「参数类型不对」。
- **没给的**：两个数。
- **当时树里留下的**：只有 `origin/develop` 的合并（`d97789d52`）。残差那一半的代码我撤回了——它能让类型通、却停在一个 `Call` 上，留着就是一个「接受但永远拒收」的半套，比没有更糟。

## 五、转置一个调用（已落地，`bf1768147` 之后那一笔）

调用能转了：被调图**一份自己的转置**（`jvp` 的 `callees` 做法，一棵调用图一份，所以同一个函数被调两次只转一次），调用点把「它已有的实参 + 余切」传进去，拿回的那条边接回原调用点。最小案例 `g(x) = 2.0*x`、`f(x) = g(x) + g(x)` 实测 `f(1.0) = 4.0`、`transpose(f)(1.0, 1.0) = 4.0`，且 `TRANSPOSE function=g` 只打一行——「每个被调图一份」是可数的（`test/cases/compile/transforms/transpose_call.cml`）。

**这一笔最贵的两个坑**（都在这台机器上量出来的，不是猜的）：

1. **直接调用在本图模型里有两种写法**：`CALL` 节点（第一个 norm 输入是被调值，`kind=8`），或者 **`FUNC` 节点本身就是这次调用**（`kind=10`，实参直接是它的 norm 输入）。`f(x) = g(x) + g(x)` 走的是后者——`std::gir` 里 `N04 [FUNC g] value_type=float`，根本没有 CALL 节点。只认 `CALL` 会得到 `skip=kind node=2`。
2. **`addFuncNode`/`addCallNode` 的那个 `type` 是「这次调用的值类型」**（这里是 `float`），不是被调图的签名。传签名进去，边的校验会报 `calls 'transpose_of_g', whose value is a float, and is a (float, float) => float`。

## 六、被调值是谁：四类，测过的

「转置一个调用」不是一个功能面，是四个。判据不是读代码猜的，是每类各写一条最小程序量出来的（`camel <file> std::gir` 看节点，跑一遍看拒收原文）：

| 类 | 最小程序 | 今天 |
|---|---|---|
| **`Func`**（直接调用：`FUNC` 节点本身就是调用，或 `CALL` 的第一个 norm 输入是被调值） | `f(x) = g(x) + g(x)`，`g(x) = 2.0*x`（`test/cases/compile/transforms/transpose_call.cml`） | **过**：`transpose(f)(1.0, 1.0) = 4.0`，`TRANSPOSE function=g` 只一行 |
| **`Data`**（函数常量：`let h = g` 后 `h(x)`，闭包/函数值存成静态值，调用以 `with` 输入带着它） | `func f(x) { let h = g; return h(x) + h(x) }` | `skip=call node=3 callee=none value=0:1 with=1:0`——`calleeIdOf` 只认 `FUNC` 节点，`DATA` 里的函数常量名字取不到 |
| **`Call``**（被调值是某次调用的返回值，即“算出来的”） | `split(jvp(f))` 的后一半（`transpose_residuals`） | `skip=call node=11 callee=none value=10:8 with=7:8` |
| **`Port`**（高阶：被调值是形参） | `func apply(fn: ((y: float) => float), x) { return fn(x) + fn(x) }`，`f(x) = apply(twice, x)` | `TRANSPOSE function=apply skip=signature` → `f skip=call node=2 callee=apply` |

**`Func` 这一类本来就能过**——所以「转置一个调用」的清单从「猜」变成了：**一类过、三类按名拒**。`Data`/`Call` 两类的替代品**没有造**（闭包当值那条等作者裁定）；`Port` 这一类要的是泛化 `transposeSignature`（函数参数不是 `float`），那是另一个面。

## 七、这一笔没做到，是因为判断错了预算，不是判断错了方向

我先按「残差版就是把端口和已知值换掉」估了两小时以内，实际动手才发现后一半带调用——那是另一个功能（转置调用），不是这一次改动的延伸。**下次估「收下一种新形状」之前，先把它要走的图打出来看一眼有没有 `Call`/`Dref`/循环**：图解形状比类型形状更早暴露工作量。**这条已经兑现过两次**：先发现直接调用有两种写法（`CALL` / `FUNC`），后发现在 `let` 里存下的函数值既不是 `Func` 也不是 `Call` 而是 `Data`。