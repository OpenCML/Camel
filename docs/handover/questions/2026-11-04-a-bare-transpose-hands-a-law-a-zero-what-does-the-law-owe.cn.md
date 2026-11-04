# 裸 `transpose` 把操作数的**零**递给读它的律时，这条律算不算"验过"

一页一问：**`transpose(f)` 手里只有余切、没有参数值，于是一条读操作数的余切律（`abs_t` 的
`x`、`maximum_t` 的 `x`/`w`）拿到的是零载体；这算"律读到了"还是"律没读"？四条律的"有没有
用例"表该填哪一格？**

（2026-11-04 作者 48 页裁定第六条第二款：「没有用例读到的律不算补上」；本页是那条要求在这一
类律上的落地边界。）

## 1. 事实

- **裸 `transpose` 的入参是余切，不是值。** `transpose(f)` 的签名是 `(cotangent) => (cotangents..)`，
  参数值不在它的入参里；一条余切律读到操作数时，walk 递给它的是该位置切空间的**零载体**。
  这不是猜测，两条读数各自把它钉死：
  - `log_t` 是 `ct / x`，`transpose(f_log)(1.0)` 答 `([inf, inf, inf])` = `1.0 / 0.0`；
  - `abs_t` 是 `where(x >= 0.0, ct, -ct)`，`0.0 >= 0.0` 为真，所以逐元素答 `ct`；在
    cotangent `-3.0` 上答 `([-3.0, -3.0, -3.0])`（不是 `[3.0, 3.0, 3.0]`），
    即 `x` 读到的既不是真值、也不是 `ct`，而是"非负的某个东西"，与零载体一致。
- **正确值是知道的，只是拿不到**：x = [1, -2, 3] 时 `abs_t` 应是 `[1.0, -1.0, 1.0]`；
  x = [1, 2, 4] 时 `log_t` 应是 `[1.0, 0.5, 0.25]`。今天是 exit 0 + 错数，**静默**错。
- **`vjp` 路线的数字对，但它不读这四条律。** `vjp` = `split` + `transpose`，前向半由
  `jvp_abs`/`jvp_log`/`jvp_maximum` 自己写掩码，线性半的律是别的律，实测：
  `abs` → `rhs=dtod_t,sum_stated_t,mul_tensor_t`；`log` → `rhs=dtod_t,sum_stated_t,divide_t`；
  `max`/`min` → `rhs=dtod_t,sum_stated_t,add_tensor_t,mul_tensor_t`。四条 `*_t` **一个都没出现**。
  所以"`vjp` 答对了"不能当这四条律的用例：那正是一棵两样都绿的树。
- **`maximum_t`/`minimum_t` 连实例化都不成立**，与载体无关：律把余切写成 `ct: T[..]`，而三参
  实例把 `T` 绑成了**操作数的整个类型**，于是 `ct` 是"元素为 `float[3]` 的开秩数组"。三种零写法
  各从一侧量到它（2026-11-04，同一棵树）：
  - `zeros_like(ct)` → `No matching function found for argument types: <> (float[3][..])`；
  - `shape(ct)` → `every candidate's 'when' condition is false … when is_numeric::<T>()`，
    而 `is_numeric::<float[3]>` 为假——同一条绑定从类型参数一侧看到的样子；
  - `ct - ct` → `operator '-' is not defined for these operand types`。
  律本来要的那个秩（标量 `where(bool, T, T)`）也写不出来：`tensor:maximum` 没有标量拼法
  （`maximum(x, 1.0)` 不过类型检查），所以没有一个秩能让这个错位消失。

## 2. 问题（要裁的一句）

裸 `transpose` 走到"律读了操作数的值、而 walk 手里没有这个值"时，应该是：

- **甲：拒绝**，并按 `TRANSPOSE-NEEDS` 家族的格式指名"这条律要 `x` 的值"（今天 `parameter-shape`
  已经是同族的一条）；
- **乙：维持零载体**，把这条记成已知缺陷，用例钉住错数并写明正确值（本车道默认）；
- **丙：换一条把值送进去的路**（裸 `transpose` 也要求操作数，或规定这类律只能经 `split` 被读到）。

## 3. 我方默认与理由

**乙，且不改 `transpose.cpp` 的载体判定。** 理由两条：
1. 改载体判定会动到树上**每一条**裸 `transpose` 读数（`transpose_*.cml` 一族），超出本车道
   （`pow_tangent` 无守卫那条裁定）的授权面；甲/丙是语义决定，该由裁定给。
2. 乙仍然满足「律要被读到」：用例 `transpose_dispatches_abs_t_and_log_t` 钉住三条读数（三个后端各一次），**删掉 `abs_t`
   或 `log_t` 会变成拒绝、改掉其中任何一条的算术会移动一个被钉的数**——2026-11-04 已做红绿各一次
   （把 `abs_t` 两臂对调、`log_t` 的 `/` 换成 `*`：三条读数变成 `([-1.0, -1.0, -1.0])`、
   `([3.0, 3.0, 3.0])`、`([0.0, 0.0, 0.0])`，恢复后回绿）。所以这两条律**算读到**，钉的是"律在跑、
   跑的是哪条律"，**不是**数学正确值；正确值写在用例注释和打印行的 `right=` 里。

`maximum_t`/`minimum_t` 按裁定第六条的"做不到就写未验证"处理：计划里标 **未验证**，卡点按第 1 节
最后一条逐条写明（先 `transpose.cml#maximum_t`/`#minimum_t` 的 `ct: T[..]` 绑定，再
`maximum`/`minimum` 缺标量拼法）。

## 4. 已钉的用例

| 用例 | 守什么 | 今天的读数 |
|---|---|---|
| `transpose_dispatches_abs_t_and_log_t.cml`（nvm/fvm/jit） | `abs_t`/`log_t` 被读到：`rhs=` 指名它们，改律即红 | exit 0；`([1.0, 1.0, 1.0])` / `([-3.0, -3.0, -3.0])` / `([inf, inf, inf])` |
| `transpose_laws_of_maximum_and_minimum_are_refused.cml` | 这两条**未验证**：律找得到、实例化不成立 | exit 0；`skip=rule-ranks … rule=maximum_t/minimum_t … (bool[3], float[3][..], float)` |

## 5. 裁了之后要改什么

- 走甲：`transpose.cpp` 里给"律的操作数位置没有值"加一条具名拒绝（与 `parameter-shape` 同族），
  上面两条用例的读数变成拒绝文本，`abs_t`/`log_t` 转成**已验证**（届时把正确值 `[1,-1,1]` /
  `[1,0.5,0.25]` 钉进去），`maximum_t`/`minimum_t` 仍卡在第 1 节最后一条。
- 走丙：同上两条用例的读数改成"要操作数"的签名错误；顺带回答"`split` 之外还有什么路能读这类律"。
- 走乙：本页即终态，只需在 `docs/handover/reports/` 里留一条"裸 `transpose` 的律读数不是数学
  断言"的说明。