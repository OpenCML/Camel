# 被调值站在“第一个操作数”位置：该删掉那条读法，还是教调用转置把它写对？

**状态**：**已按默认甲落地**（2026-11-04，本条车道，派工授权）；遗留一问见第 6 节（`with` 列表里那半仍在：调用的**值**被当成被调方，`callee=mk` 没动）。

写这页的直接原因：`lane-floatgate` 在 `modules/transforms/transpose.cpp#buildTransposeMember` 的 `argumentAt == 1` 分支旁写下“这条分支的存在本身就说明这个形状是可能的；五个 f32 用例都没这么写，所以读数不变”。**本页把“这个形状”写成了程序、量了它今天怎么走**，结论是：**它可达，但只能被“误读”可达**——走到那条分支的程序，被读成被调用的那张图**不是这次调用的被调方**；而那条分支自己写出来的 CALL 节点，运行期一律拒收。

## 1. 四条读法，各自的最小程序与读数

树 `D:\Projects\Camel-wt\fnarg` @ `34b659107`（宿主 `/home/zjwei/camel-work/fnarg`，二进制 2026-11-03T18:06:52-05:00；`nvm`）。手算与实测并列。**本节是改动前的读数**，(d) 与 (b) 两行的改后读数在第 6 节。

| # | 读法 | 最小程序 | 手算 | 实测 | 归类 |
|---|---|---|---|---|---|
| a | 函数值当**普通函数的第一个实参** | `f(x) = apply(twice, x)`，`apply(fn, x) = fn(x) + fn(x)`，`twice(y) = 2y` | `4x`，`f(3)=12`，`f'(3)=4` | `12.0` / `(4.0)`，exit 0；`TRANSPOSE function=apply type=((y: float) => float, float) => (float,)` | **对** |
| b | 函数值先交给 `h`，再把结果应用 | `f(x) = mk(x)(2.0)`，`mk(x) = (y) => x*y` | `2x`，`f(3)=6`，`f'(3)=2` | `6.0` 后 exit 1：`TRANSPOSE function=mk skip=signature`、`TRANSPOSE function=f skip=call-no-transpose node=3 callee=mk` | **明确拒绝（名字错）** |
| c | 标准库收一个函数当第一个形参 | `attempt::<F>(f: F): F!`（`stdlib/prelude.cml#attempt`，`f` 是唯一普通形参）；值层面 `attempt(boom)(2) = 5`、`attempt(boom)(0) = -1` | — | 值层面 exit 0；求导（`f(x)=attempt(div10)(x) !! 0.0`）**先死于** `skip=condition`（`a bool condition has no cotangent space`），exit 1，**到不了任何调用** | **明确拒绝（更早一步）** |
| d | 对“把函数当第一个实参”的调用求导 | `f(x) = h(g(x), 1.0)`（`h = (y,z) => y*z*x`，`g(x)=2x`）与 `h(mk(x), 2.0)` | `2x²`，`f(3)=18`，`f'(3)=12` | `18.0` 后 exit 1：`RuntimeDiag::PassInternalError ... Runtime graph 'transpose_of_f' has CALL node 2 without callee input` | **崩（编译器内部错误，不是错数）** |

**每种读法被排除/成立的理由是量出来的，不是“没想到”**：

- (a) 成立且答对，但它**不经过**那条分支：`apply` 里的被调值是**形参 PORT**，走的是“这个图收到的值”那一支（`calleeIdOf` 拿不到、`staticFunctionGraphOf` 也拿不到，但节点是 `Port`）。`Input` 位置上的函数值在图里是 DATA 或 PORT，**永远不是 FUNC 节点**，所以 `calleeIdOf(inputs.front())` 不会替它作答。
- (b) 被调值是**某次调用的返回值**，builder 把它放在 `with` 输入（`src/compile/gir/builder_calls.cpp#addCallNodeOf` 那条路对所有调用都用 `LinkType::With`）。可 `calleeIdOf` 对 **FUNC 节点**作答——那是“按名写下的调用”，它答的是**那次调用的被调方**，不是那个值里装的图。于是读到 `mk`。`argumentAt` 停在 0，不是 1。
- (c) 标准库的**高阶约定**不是“函数当普通第一个实参”：`map`/`filter`/`reduce`/`foreach` 把函数放在 `with <f: …>` 里（设计原则 11.4：被操作的值是普通实参，配置是 `with` 实参），第一个**普通**实参是数组。真正收函数当第一个普通形参的只有 `attempt`/`fold`/`jvp`/`split`/`transpose`/`vjp`；它们的值层面可用，而求导一侧被 `skip=condition`（`F!` 的故障检查）先拒。
- (d) **只有这一条走得到 `argumentAt == 1`**，而且是被误读走到的：`f` 的 GIR 里

  ```
  N0A [label="(y: float, z: float) => float pure", type="FILL", value_type="(y: float, z: float) => float pure"]
  N0B [label="g", type="FUNC", value_type="float"]
  N0C [label="1.0", type="DATA", value_type="float"]
  N0D [label="CALL", type="CALL", value_type="float"]
  N0A -> N0D [label="0|0", type="WITH"]     // 被调值：函数体自己组装的闭包
  N0B -> N0D [label="0|0", type="NORM"]     // 第一个实参：按名写下的调用
  N0C -> N0D [label="0|1", type="NORM"]
  ```

  插桩读数（`transpose.cpp#buildTransposeMember`，临时打印，已回退）：`FNARG-PROBE call node=5 kind=8 isCall=1 callee=54 argumentAt=1 norm=2 with=1 carried-by=4`——`kind=8` 是 `Call`，`carried-by=4` 是 `Fill`，`callee=54` 是 `g` 的图。`54` 与 `g` 的对应由下一行确认：同一形状把 `g` 换掉，读数变成 `callee=mk`。

**这条分支为什么“没人写得到”**：全仓 `addCallNode` 的每一处（builder 五处、`split`/`vmap`/`jvp`/`pullback`/`tiles`、以及 `transpose.cpp` 自己除该分支外的全部调用点）都把被调值放在 **`with`**；而 `src/runtime/graph.cpp#GCGraph` 的编码检查要求 `Call` 节点**至少有一个 `with` 输入**，没有就抛 `Runtime graph '…' has CALL node … without callee input`。也就是说“被调值站在普通操作数位置”的图，**产不出、也跑不了**。

插桩扫全量 `test/cases`（3494 个 `.cml`，`std::nvm`，逐条抓 stderr，`timeout 15`，`-P 12`）：**命中只有本页新增的三个程序**——`transpose_call_first_argument_is_a_call.cml`（`argumentAt=1 ... carried-by=4`，`Fill`）、`transpose_call_first_argument_is_a_function_value.cml`（同上）、`transpose_callee_is_a_returned_function.cml`（`argumentAt=0 ... carried-by=10`，`Func`，即 `with` 那一路的误读）。**存量用例一条都没有**：这条分支与“`with` 里那个 FUNC 节点”这两个读法，今天没有任何用例走过。

## 2. 那条分支自己写出来的图，运行期不认

（改前）`argumentAt == 1` 时它把被调值塞进 `callArgs` 的第一个位置（`draft.addFuncNode`），而 `setWithInputs` 只在 `argumentAt == 0` 时发生。于是 `transpose_of_f` 里多出一个**没有 `with` 输入**的 CALL 节点，`nvm` 直接抛 `std::runtime_error`，外壳报成 `PassInternalError`（`help` 已经写明“这是编译器的缺陷，不是程序的”）。

它不会给出错数：运行期在跑之前就拒了，所以按今晚的次序 `对 > 明确拒绝 > 错数 > 崩`，这一格是**崩**（进程没死，但是编译器内部错误）。

同一个形状，把第一个实参从“调用”改成“函数名”（`h(twice, 2.0)`），读数**立刻变对**：`skip=call-no-transpose node=5 callee=none value=3:0 with=2:4 reason=function-closure`。差别只在“那个实参节点会不会替自己答出一个图”——DATA 不会，FUNC 会。这就是误读的全部分界。

## 3. 两条路

| 路 | 做法 | 代价 |
|---|---|---|
| **甲：删掉第三问** | `modules/transforms/transpose.cpp#buildTransposeMember` 里 `calleeId = calleeIdOf(inputs.front()); argumentAt = 1;` 两行去掉；被调值无可命名的图时，落到已有的 `callee == nullptr` 那支，按 `staticFunctionGraphOf` 的 `reason` 拒绝（闭包 `function-closure`、调用的值 `not-a-data-node`）。`argumentAt` 退回只有 0 一值 | 那条分支与其后的 `addFuncNode` 写法一并作废（**这就是正在被另一条车道重写的那一段**，冲突面最大）；三条本页新增的负向用例从 `known_fail` 转正；**存量用例一条都不用改**（3494 个 `.cml` 全扫，命中只有本页那三条新程序，已量） |
| **乙：教它写对位置** | 保留第三问，但写出的 CALL 把被调值放进 **`with`**（与全仓一致），`argumentAt` 只用来定位实参偏移 | 分支“合法化”了，可**它的前提仍然是错的**：`inputs.front()` 是这次调用的**第一个实参**，不是被调方；写对位置只会让误读的图跑起来，可能给出**错数**（`g` 线性时 `g_t` 会拿到第一个实参的值当参数，实测 `2x²` 的导数是 12，会读成 2）——按今晚的次序，这比“拒绝”差 |
| 丙：先不动 | 只留本页与五条用例（已入库，其中三条带 `known_fail`） | 分支继续躺着一个“看起来可达、实际只被误读可达”的读法；下一个人仍会以为它守着一种真实形状 |

## 4. 我方默认

**甲。** 三条理由都是读数：**（i）** 全仓没有生产者把被调值放在普通操作数位置，运行期也不接受这种 CALL 节点——“这个形状是可能的”这句话在今天的代码里**没有对应物**；**（ii）** 那条分支写出来的图一跑就 `PassInternalError`，它今天能“成功”的唯一方式是把别人的被调方当成自己的；**（iii）** 同一个形状改一个词（名字 → 调用）读数就从“对的拒绝”变成“错的名字/内部错误”，说明误读面在“节点会不会自己答出一个图”，与形状无关。

甲要动的面只有一处（`buildTransposeMember` 的 CALL 分支）。它**已按甲落地**，见第 6 节；若将来有人想走乙，请同时写清“`argumentAt` 是实参偏移，不是被调值位置”，并给本页 (d) 的线性 `g` 变体加一条数（乙会让它跑出数，而那个数按第 4 节的推算是 2，正确值 12）。

## 5. 钉住的用例（`test/plans/feat/compile/transforms.plan.toml`）

| 用例 | 守什么 | 今天的读数 |
|---|---|---|
| `transpose_takes_a_function_parameter.cml` | 函数值在**普通实参位置**、被调值是 PORT 时答对（4，不是 0） | exit 0，`(4.0)` |
| `transpose_call_of_a_body_closure_with_a_bare_name.cml` | 同一形状写成**函数名**时，拒绝的名字对（`callee=none reason=function-closure`） | exit 1，正确拒绝 |
| `transpose_call_first_argument_is_a_call.cml` | **原来 `argumentAt == 1` 的入口**：第一个实参是按名调用，被调值是闭包；现在钉“具名拒绝 + 内部错误不再回来” | exit 1，`callee=none … reason=function-closure` |
| `transpose_call_first_argument_is_a_function_value.cml` | 同上，实参携带**函数值**（最字面的一种读法） | exit 1，同上 |
| `transpose_callee_is_a_returned_function.cml` | 被调值是**调用的返回值**时，`with` 里那个 FUNC 节点被当成被调方 | exit 1，`callee=mk`（**仍带标记**，见第 6 节） |

## 6. 落地（2026-11-04，本条车道）与遗留的一问

改动只有一处、三行：`buildTransposeMember` 里
`calleeId = function->calleeIdOf(inputs.empty() ? ref : inputs.front()); argumentAt = … ? 1 : 0;`
那一问删掉，注释改成“操作数不是被调值来的地方”，其余（端口分类、`wantsCotangent`、`ruleCotangent`、形状通路、`transposeSignature`、`argumentAt == 1` 那条现在**不可达**的写 CALL 分支）**一个字没动**——那一段归重写 CALL 转置的车道，删死代码由它决定。

改后读数（同一棵树、`nvm`）：

| 程序 | 改前 | 改后 |
|---|---|---|
| `h(g(x), 1.0)`（本文 (d)） | `PassInternalError … has CALL node 2 without callee input` | `skip=call-no-transpose node=5 callee=none value=3:10 with=2:4 reason=function-closure`，exit 1 |
| `h(mk(x), 2.0)`（本文 (d) 的函数值变体） | `TRANSPOSE function=mk skip=signature` + `callee=mk` | 与上一行**逐字相同**，exit 1 |
| `h(twice, 2.0)`（对照组） | 正确拒绝 | **不变** |
| `apply(twice, x)`（本文 (a)） | `(4.0)` | **不变** |
| `mk(x)(2.0)`（本文 (b)） | `callee=mk` | **不变** |

**遗留一问（就是第 1 节 (b) 那半，需要裁一句话）**：`with` 列表里那一问用的是 `calleeIdOf`，它对**任何 FUNC 节点**作答——包括“这个节点的值是这次调用的被调方”的那种。于是 `mk(x)(2.0)` 仍被读成对 `mk` 的调用：**判决对（拒绝、exit 1、不给数），句子错（名字指到 `mk` 头上，理由说成“这个被调方没有转置签名”，而真正的原因是那个值没在图里命名任何图）**。要把它也修掉，就是把那一问的取值函数换成“这个**值**命名哪张图”（`staticFunctionGraphOf`，对这个节点回 `not-a-data-node`），与本次改动是同一形状、只差一个输入列表。**我方默认：等重写 CALL 转置那条车道一起做**，因为同一处代码它已经在改，本车道再动第二次就是两处各自正确、合起来错一次的风险；用例 `transpose_callee_is_a_returned_function.cml` 与它的 `known_fail`（msg `callee=mk`）就是那条车道的红灯，标记里写明“归类对、名字错”。