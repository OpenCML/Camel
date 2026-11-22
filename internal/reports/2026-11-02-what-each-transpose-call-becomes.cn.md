# 每一处 `transpose(f)(值…, ȳ)` 要改成什么（迁移表）

**状态：只写不落。** 作者 2026-11-02 11:30 已裁：`transpose(g)` 把**每个参数**都当线性，返回 `(ȳ) => (每个参数一个余切)`；旧写法 `transpose(f)(x, ȳ)` 改成**先把 `x` 捕获进去**：`transpose((dx) => f_lin(x, dx))(ȳ)`，读数 `transpose(lin)(1) = (12, 9)`。所以下面 23 个文件 / 50 处**一定会碎**，不是"若 b2 顺手改了才碎"。

## 一、三条改写规则（先看这三条，再看表）

1. **单参数普通函数 `f(x)`**：先取它的线性孪生，再转置，余切是唯一实参。
   ```
   旧： let back = transpose(f);        back(v, ct)
   新： let (_, lin) = split(jvp(f))(v)      // lin: (d) => float，闭包里带着 v
        let back = transpose(lin);          back(ct)
   ```
2. **已经是提升过的函数（自带方向参数）**：把**值**捕获进 lambda，方向参数留给 `transpose`。
   ```
   旧： transpose(mt)(x0, w0, dx, dw, ct)          // mt(x, w, dx, dw)
   新： transpose((dx, dw) => mt(x0, w0, dx, dw))(ct)
   ```
3. **本身就是线性半边**（`split` 交出的 `lin`）：只删掉多余实参。
   ```
   旧： transpose(lin)(0.0, 0.0, ct)    新： transpose(lin)(ct)
   ```

**为什么没有一处能纯 `sed`**：规则 1 要多插一行（`split(jvp(f))`）并且**值要选对**——`back(1.0, 2.0)` 里哪个是值、哪个是余切，只能由"被转置函数有几个参数"决定。这正是这张表存在的理由。

## 二、表（23 个文件 / 50 处）

「值/余切」一栏按被转置函数的形参个数切：**前 n 个是值、最后 1 个是余切**；`mt`/`at` 例外（形参自带方向，见行内说明）。

| 文件 | 被转置的函数 | 现写法 | 目标写法 | 可机械改 |
| --- | --- | --- | --- | --- |
| `compile/transforms/a_refusal_keeps_stdout_to_the_program.cml:28` | `g2(x)` | `back(1.0, 1.0)` | `back(1.0)`，`back = transpose(lin(g2, 1.0))` | 否（要插 `split(jvp)`） |
| `transpose_branch.cml:31,32` | `pick(x)` | `back(1.0, 1.0)` / `back(-1.0, 1.0)` | 两处的值不同（`1.0` / `-1.0`）⇒ 需要**两个** `lin` | 否 |
| `transpose_branch.cml:34,35` | `merged(x)` | `merged_back(1.0, 1.0)` / `merged_back(-1.0, 1.0)` | 同上 | 否 |
| `transpose_call.cml:22` | `f(x)` | `transpose(f)(1.0, 1.0)` | `transpose(lin)(1.0)` | 否 |
| `transpose_function_value_no_capture.cml:41,43` | `g(x)` | `back(1.0, 1.0)` / `back(1.0, 2.0)` | 值都是 `1.0`，余切 `1.0` / `2.0` ⇒ 一个 `lin` 两次调用 | 否 |
| `transpose_loop.cml:26,27` | `thrice(x)` | `three_back(1.0, 1.0)` / `(1.0, 2.0)` | 一个 `lin`，余切 1 / 2 | 否 |
| `transpose_loop.cml:30` | `quad(x)` | `quad_back(1.0, 1.0)` | 另一个 `lin` | 否 |
| `transpose_loop_escape_int.cml:51,52` | `int_escape(x)` | `backI(1.0, 1.0)` / `(1.0, 5.0)` | 值 `1.0` 固定，余切 1 / 5 | 否 |
| `transpose_loop_escape_int.cml:55,56` | `g(x)` | `backG(1.0, 1.0)` / `(1.0, 3.0)` | 同上 | 否 |
| `transpose_loop_escape_int.cml:59,60` | `h(x)` | `backH(1.0, 1.0)` / `(1.0, 2.0)` | 同上 | 否 |
| `transpose_loop_escape_reads_one.cml:21` | `read_one(x)` | `back(1.0, 1.0)` | `back(1.0)` | 否 |
| `transpose_loop_escape_reads_outer.cml:25` | `read_outer(x)` | `back(1.0, 1.0)` | `back(1.0)` | 否 |
| `transpose_loop_escape_reads_twice.cml:19` | `read_twice(x)` | `back(1.0, 1.0)` | `back(1.0)` | 否 |
| `transpose_loop_escape_two_states.cml:48,51` | `carried(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 一个 `lin`，余切 1 / 2 | 否 |
| `transpose_loop_state_second.cml:25,28` | `carried_p(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 同上 | 否 |
| **`transpose_many_parameters.cml:20`** | `mt(x, w, dx, dw)` | `transpose(mt)(3.0, 2.0, 1.0, 0.0, 1.0)` | `transpose((dx, dw) => mt(3.0, 2.0, dx, dw))(1.0)` | **否（论点变）** |
| **`transpose_many_parameters_in_ct.cml:18,19`** | `mt(x, w, dx, dw)` | 同上但余切 `1.0` / `2.0` | 值 `3.0, 2.0` 捕获，余切 1 / 2 | **否（论点变）** |
| **`transpose_unread_parameters.cml:19,20`** | `at(x, w, dx, dw)` | `transpose(at)(3.0, 2.0, 1.0, 0.0, 1.0)` | `transpose((dx, dw) => at(3.0, 2.0, dx, dw))(ĉ)` | **否（论点变）** |
| `transpose_no_tangent_bool.cml:31,32` | `f(x)` | `back(2.0, 1.0)` / `(2.0, 2.0)` | 值 `2.0` 捕获，余切 1 / 2 | 否 |
| `transpose_no_tangent_closure.cml:36,37` | `g2(x)` | `back(1.0, 1.0)` / `(1.0, 3.0)` | 同上 | 否 |
| `transpose_no_tangent_tuple_member.cml:28,29` | `k(x)` | `back(2.0, 1.0)` / `(2.0, 3.0)` | 同上 | 否 |
| `transpose_straight_line.cml:25` | `sum_of_scaled(x)` | `back(2.0, 1.0)` | `back(1.0)`，值 `2.0` | 否 |
| `transpose_straight_line.cml:27` | `scaled_chain(x)` | `chain_back(2.0, 1.0)` | 同上 | 否 |
| `transpose_tuple_both_slots.cml:22,24` | `both(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 余切 1 / 2 | 否 |
| `transpose_tuple_component_first.cml:24,28` | `first(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 同上 | 否 |
| `transpose_tuple_component_second.cml:22,25` | `second(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 同上 | 否 |
| `transpose_tuple_nested.cml:22,26` | `nested(x)` | `back(1.0, 1.0)` / `(1.0, 2.0)` | 同上 | 否 |
| `transforms/transpose/residuals_in_the_closure.cml:35` | `lin`（已是线性半边） | `back(0.0, 0.0, 1.0)` | `back(1.0)` | **是**（只删两个实参） |

（表里 23 行覆盖 50 处调用：单参数 20 个文件共 44 处，`mt`/`at` 3 个文件 5 处，`residuals_in_the_closure` 1 处。）

## 三、不能机械改的那 5 处，各自为什么

- `transpose_many_parameters.cml:20`、`transpose_many_parameters_in_ct.cml:18,19`、`transpose_unread_parameters.cml:19,20`：**用例的论点就是"已知值优先"**（形参 `x, w` 是值、`dx, dw` 是方向，`transpose` 曾经按位置把它们分开）。裁定把这个约定**收回**，所以这四条不是改写法，是**改论点**：应改成"按新约定，`x`/`w` 必须被捕获，否则不被接受"，并在注释里写明"约定在 2026-11-02 11:30 被收回"。改完数字仍应是 `transpose` 早先印出的那些值 —— **数一个不动**。
- `a_refusal_keeps_stdout_to_the_program.cml:28`：它测的是**"拒绝不吞 stdout"**，`back` 只是道具；改写时不要顺手把它从"拒绝"改成"通过"，否则这条用例就不再断它名字里那件事了。

## 四、两处字符串锁（比编译器更早报错，同一次要改）

```
test/plans/feat/compile/transforms.plan.toml:26   expect_output_contains = [..., "type=(float, float) = …
test/plans/feat/transforms/split.plan.toml:155    expect_output_contains = ["TRANSPOSE function=split_of_jvp_of_f_backward
                                                   arm-call … type=(float, float, float) => (float, float) …"]
```
第一条锁的是 `jvp` 提升后的类型，第二条锁的是**当前** `transpose` 的类型。裁定的形状落地后，第二条必须跟着写成新的口数，第一条要复核是否跟着变。

## 五、一条纪律（今晚第 N 次踩到，写下来）

**迁移前先确认"这个符号是不是同一个符号"。** 全树扫 `split(` 会命中两件不同的东西：

- `test/cases/modules/random/*.cml`、`test/cases/trans/cpp/exec/draws.cml`、`test/cases/loops/compiled_native.cml` 里的 `let (a, b) = split(key(42))` / `split(4)` 是**随机数与整数的 `split`**，与 `transforms::split` 无关，**一处都不要动**；
- `test/cases/modules/nn/{lr,lrit}.cml` 里的 `squared_residuals` 是**变量名**，不是残差机制。

同理"`back(`"会命中 `native_callback(`、`test/cases/modules/onnx` 里的回调名 —— 这两处也是误命中，已从 23 个文件里剔除。