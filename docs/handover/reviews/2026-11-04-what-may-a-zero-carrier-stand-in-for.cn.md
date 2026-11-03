# 零载体能顶替一个参数到什么程度：只经形状、还是也顶替元素？

## 问什么

`transpose(f)` 只把余切交给反函数、从不把参数的值当形参传（`modules/transforms/transpose_operator.cpp#transposeKernel`：
每个形参都是线性输入）。于是反函数里凡是**用到某个参数的值**的律，都得由别的东西把这个值补上。补法之一是
**零载体**：用该参数**类型里写出的形状**造一个零数组顶上去。

问题是：**零载体什么时候顶得上、什么时候顶不上？** 这一页要裁的正是这条边界——不是"答还是拒"，
而是"**哪些律里的那个值可以被形状换掉**"。

## 1 今天树里的三种状态（读数）

**（甲）形参是标量数组，且反函数只把它用于形状——今天答案是对的（在 `lane-trefuse fbf31b3ed` 上量到）**

`pick(x: float[3]) = x[1]`，`transpose(pick)(1.0)`：

```
([0.0, 1.0, 0.0])          exit 0
TRANSPOSE function=pick type=(float) => (float[3],) rhs=idx_arr_t
TRANSPOSE op … graph=(float) => (float[3],) same=yes
```

手算对得上：`x[1]` 的伴随把 `ct` 写到位置 1、其余为零，ct = 1 正是 `[0, 1, 0]`。零载体在这里
**没有被拿来造数**——它只提供了"几个位置"，写进去的是余切。

`mean_axis_transpose` 同理：同一条分支上 `rules_angle_args` **8/8 绿**，`x: float[2, 3]` 的读数逐元素为
`[[0.5, 1.0, 1.5], [0.5, 1.0, 1.5]]`；零载体给的是 `ones_like(xs)` 需要的形状与 `numel(xs) / numel(ct)` 的车道长度。

**（乙）同一形状在 type 里没写出来——今天被具名拒绝（同一分支）**

`pick(x: float[])`（开秩）在同一条分支上：

```
error[RuntimeDiag::TransposeNeedsParameterShape]: The transpose of 'pick' needs the shape of the parameter 'x', and the type 'float[]' does not state it.
help: …the type has to state the shape. Write the extents … 'func loss_mean(x: float[..])' becomes 'func loss_mean(x: float[2, 3])' …
```

**本树（`6560eb334`）没有这条路**：它按"成员的形参比算子承诺的多"一律拒，同一个程序给的是
```
transforms: transpose has no transpose for this function: the body reads the value of its `float[3]` parameter, and the transposed function `(float) => (float[3],)` has no parameter for it … Take the cotangent through `vjp` …
```
（改前是 `Runtime indirect call norm-arity mismatch … has 1 args … has 2 norm ports` 与 fvm 的
`expected 2, got 1` / exit 134 崩溃。）

**（丙）反函数读那个值的元素——今天到不了，一旦到就是错数**

`energy(x: float[3], w: float[3]) = sum(x * w)`、`energy2(x: float[2, 2], w: float[2, 2])`、以及
`relu_sum(x: float[3]) = sum(relu(x))`，在 `fbf31b3ed` 上都停在同一处：`skip=rule-ranks uri=tensor:multiply`
／`uri=tensor:relu`（秩 1 与秩 2 都试过）。也就是说：**今天**没有程序能从零载体那里拿到错数；风险是"再放宽一条律"就有一格。

## 2 逐条律的分类（量过右式，不是猜的）

**只用形状的律**（值被 `shape`/`numel`/`zeros_like`/`ones_like`/`full(shape(...))` 一类消费，零载体**精确**）：

| 律（`stdlib/rulesets/transpose.cml`） | 右式里对操作数的那一步 |
| --- | --- |
| `#idx_arr_t` | `scatter_into(zeros_like(xs), i, ct)` —— 只借形状，写进去的是 `ct` |
| `#take_t` | `scatter_rows(zeros_like(t), indices, ct, 0)` —— 同上 |
| `#mean_axis_t` → `#mean_axis_cotangent` | `numel(xs)` 与 `ones_like(xs)` 算车道长度与广播形状 |
| `#sum_t` → `#spread_like` | `full(shape(xs), ct)` —— 形状 + 余切，没有别的 |

**读元素、零载体一定给错数的律**（同一文件）：

| 律 | 右式里对操作数的那一步 | 零载体会怎样 |
| --- | --- | --- |
| `#mul_tensor_t` | `sum_to(ct * w, x)` / `sum_to(ct * x, w)` | 乘的是**另一个操作数的元素**；那一个被零顶替，答案整片为零 |
| `#relu_t` | `where(x > 0.0, ct, 0.0)` | 掩码取**这个操作数自己的元素**；零载体让掩码全假，答案整片为零 |

注意 `#mul_tensor_t` 是**按位置**分类的：对 `x` 来说那个值只出现在 `sum_to(..., x)`（形状），对 `w` 来说出现在
`ct * w`（元素）。所以"某条律允不允许零载体"这个问法不对齐——要问的是**这条律的这个位置**。

**没有 `#sum_axis_t`**：本树的 `sum_axis` 没有转置律（`stdlib/rulesets/transpose.cml#sum_t` 是整数组求和的那条），
按形状类用它的名字会指向一条不存在的律。

## 3 两条路与代价

- **（甲）允许零载体，但只在该参数的值"只经形状类运算"被用到时。** 好处：`pick(x: float[3])`、
  `mean_axis_transpose(x: float[2, 3])` 这类**本来就有唯一答案**的转置能答出来（且作者 2026-11-03 16:15 已裁
  "形状写在类型里"）。代价：**这条分类今天没有任何东西守着**——它是对每条律右式的性质判断，一条律改成"按元素赋值"
  就静默落进错数那一栏，而不显形（页末 `#idx_arr_t` 那条就是这种改法的样子）。
- **（乙）一个零也不许顶，凡需要参数的值的转置一律拒。** 好处：一个开关，没有"这条律算不算形状类"的判断，错数不可能发生。
  代价：把 `pick(x: float[3])`、`mean_axis_transpose(x: float[2, 3])` 这些**答案唯一且已经算得出来**的程序一起拒掉——
  本树 `6560eb334` 现在就是这样，实测它拒掉了 `([0.0, 1.0, 0.0])`。
- **（丙）零载体 + 一条守它的检查**（分类写进律的声明或注册表，转置时问一句"这个位置是不是只经形状"）。
  好处：甲的好处 + 错数有一道哨兵。代价：**要一个新的声明面**（律的右式性质在今天的语法里没地方写），
  而且它必须和每条律的右式保持同步——那是页末那条未来风险的制度化版本。

## 4 默认（写死）

**走（甲）的边界：零载体只允许出现在"该参数的值只经形状类运算被使用"的律里；其余一律拒绝。**

理由只有一条：**错数比拒绝坏一档**，而零载体在"值参与运算"时**必然**给出错数、且不显形。所以边界不是"能不能算"，
是"算出来的那个数是不是由余切和形状决定的"——是，就答；不是，就拒。

顺带把当前两支的关系写明：**本树 `6560eb334` 比这个默认更保守**（它连形状已写明的 `pick` 也拒）。所以：

- 若作者认这个默认，`6560eb334` 的句子应只留给"读元素"那一格（丙栏的律），`pick(x: float[3])` 归入答案；
- 那一刻 `test/plans/feat/compile/transforms.plan.toml#transpose_refuses_a_parameter_value_it_cannot_be_handed`
  的断言要换成数字 `([0.0, 1.0, 0.0])`（用例文件末段已经写明这条翻转条件）。

## 5 未来风险：`#idx_arr_t` 站在两栏的边界上

`idx_arr_t` 今天是形状类的：它造 `zeros_like(xs)` **只为形状**，写进去的是 `ct`。但它离错数只差一步——
如果哪天有人把它写成"按元素赋值"（拿 `xs` 的元素参与，例如 `where(some(xs), …)`、或按 `xs` 的元素筛位置），
它就从允许栏掉进错数栏，而**没有任何东西会红**。所以（丙）那条检查的价值不在今天，在这里。

## 6 读数出处

| 读数 | 树 | 命令 |
| --- | --- | --- |
| `pick(float[3])` → `([0.0, 1.0, 0.0])`、`same=yes` | `lane-trefuse` fbf31b3ed | `camel pick.cml std::nvm` |
| `pick(float[])` → `TransposeNeedsParameterShape` + help | `lane-trefuse` fbf31b3ed | `camel pick_open.cml std::nvm` |
| `rules_angle_args` 8/8（`x: float[2, 3]` 逐元素） | `lane-trefuse` fbf31b3ed | `camel-remote.ps1 -Plan …/rules_angle_args.plan.toml -Tier fast -Jobs 4`（load 14.3–16.3 / 32 核） |
| `skip=rule-ranks uri=tensor:multiply`（秩 1、秩 2 与 `relu`） | `lane-trefuse` fbf31b3ed | `camel energy.cml` / `energy2.cml` / `relu_sum.cml std::nvm` |
| 崩溃两式（`norm-arity mismatch` 与 exit 134） | 5e461c590 | `camel pick.cml std::nvm`／`std::fvm` |
| 改成拒绝（两边 exit 1） | `lane-arityrefuse` 6560eb334 | 同上，读数：`feat/compile/transforms` 93 = 90 通过 / 2 红 / 1 known |