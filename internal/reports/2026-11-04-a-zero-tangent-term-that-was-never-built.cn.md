# 一个从来没被建起来的项：省项的读数（`pow_tangent` 无守卫，四条 `*_t` 律的读法）

这条记录回答 2026-11-04 裁定的第六条：`pow_tangent` 不许按切向量的值分支，省项发生在**规则展开**
里；以及 `abs_t`/`log_t`/`maximum_t`/`minimum_t` 各自要有"能分辨对错"的用例。计划里的 `desc` 只留
判据与墙，读数全在这页（`agents/test.md`，“Notes and comments hold the criterion, never the
measurement”）。

## 0 复读这页要带的东西

- 树：`D:\Projects\Camel-wt\powzero`，branch `lane-powzero`。合 develop 之前 tip `460e293e1`，合后
  `258056b1a`（`origin/develop = 7f697508c`）。
- 宿主：`88zjwei`，二进制 `./out/latest/bin/camel`，`build-info.json` 记 `d7f15f2b37 (dirty)` /
  `4e46974561` / `dirty: 4 file(s)`，逐个读数行的 `provenance` 都有。
- 时刻与负载：2026-11-04 00:41–00:53 UTC-5（13:41–13:53 +08:00），`load average: 22.67, 25.63,
  26.83`。**负载 22 以上，这几个数是正确性读数，不是时间读数。**
- 参与的程序（都在 `test/cases/compile/transforms/`）：`pow_gradient_of_a_caret_through_the_library`、
  `pow_gradient_through_the_library`、`pow_gradient_through_the_engine`、
  `pow_cube_gradient_through_the_engine`、`jvp_of_a_power`、`jvp_of_a_pow_call`、
  `transpose_dispatches_abs_t_and_log_t`、`transpose_laws_of_maximum_and_minimum_are_refused`。

## 1 省项：`uris=` 那串"没有进图"的节点

`caret`（写 `x ^ 2.0`）与 `named`（写 `pow(x, 2.0)`）各跑一次 `jvp` + `vjp`，每次展开前一行：

```
REINTERP jvp rule=jvp_pow omit=structurally-zero-tangent nodes=3 uris=:op/mul_d,math:log_d,math:pow_d
REINTERP jvp rule=jvp_pow_d omit=structurally-zero-tangent nodes=3 uris=:op/mul_d,math:log_d,math:pow_d
```

`math:log_d` 与 `math:pow_d` 就在被省掉的名单里 —— 那个系数项**从来没有被建起来**，所以
`NaN * 0 = NaN` 没有机会发生。这是"缺席"，不是"算出来是 0"。

同一次运行的 stdout（**两个程序的数并排在同一个程序里**，`caret` 与 `named` 是同一个函数名的两种
拼法）：

```
caret jvp(-3.0) value=9.0 tangent=-6.0
caret vjp( 3.0) value=9.0 dL/dx=6.0
named vjp( 3.0) value=9.0 dL/dx=6.0
caret vjp(-3.0) value=9.0 dL/dx=-6.0
named vjp(-3.0) value=9.0 dL/dx=-6.0
```

图规模：`caret` 是 `nodes=15 exit=14`，`named` 是 `nodes=14 exit=13`（差的那一个是 `^` 自己）。
反向都是 `rhs=add_t,mul_t` —— 没有 `log` 律参与，也没有 `dtod_t,divide_t`。转置签名两次都答
`same=yes`。

## 2 旧数一个没动（合 develop 前后各读一次，逐字相同）

```
### jvp_of_a_power                9.0 6.0 / 9.0 -6.0 / 9.0 6.0 / 27.0 56.66253179403897 / -0.037037037037037035 nan
### pow_gradient_through_the_engine       pow engine value=9.0 dL/dw=6.0
### pow_cube_gradient_through_the_engine  pow engine value=8.0 dL/dw=12.0
### pow_gradient_through_the_library      pow library value=8.0 d/dx=12.0
                                          pow fractional value=5.656854249492381 d/dx=7.0710678118654755
                                          pow exponent value=8.0 d/dn=5.545177444479562
### jvp_of_a_pow_call                    jvp pow value=8.0 d=12.0
                                          jvp pow fractional value=5.656854249492381 d=7.0710678118654755
                                          jvp pow exponent value=8.0 d=5.545177444479562
```

`jvp_of_a_power` 里每一对都是"值 / 切向量"，最后一对是 `-0.037037037037037035 / nan`：那一条在
本次改动之前就是这么读的（用例自己的注释解释为什么），本轮逐字未动，不属于本轮的判据。

## 3 `inf` 三个后端各一次

`transpose_dispatches_abs_t_and_log_t.cml` 用 `std::nvm`、`std::fvm`、`std::jit` 各跑一次，三份
stdout 逐字相同（`inf` 三处都写作 `inf`）：

```
### std::nvm
abs  ct=1.0  ([1.0, 1.0, 1.0])  right=[1.0, -1.0, 1.0]
abs  ct=-3.0 ([-3.0, -3.0, -3.0])  right=[-3.0, 3.0, -3.0]
log  ct=1.0  ([inf, inf, inf])  right=[1.0, 0.5, 0.3333333333333333]
### std::fvm
（逐字同上三行）
### std::jit
（逐字同上三行）
```

`inf` 的写法是这三个后端上唯一可能分歧的读数，所以计划条目把三个后端都列上，并用
`backends_agree = ["exit_code", "stdout_normalized"]` 把"三份必须一样"写成断言。

## 4 `abs_t` / `log_t`：红一次、绿一次

绿（三后端同上）。红：把 `abs_t` 的两臂对调（`where(x >= 0.0, -ct, ct)`）、把 `log_t` 的 `/` 换成
`*`，同一棵树重建后：

```
abs  ct=1.0  ([-1.0, -1.0, -1.0])
abs  ct=-3.0 ([3.0, 3.0, 3.0])
log  ct=1.0  ([0.0, 0.0, 0.0])
```

恢复律 → 回到上面三行。这就是"这条用例能分辨对错"的证据，而不是"它绿着"。

要把 `rhs=` 的读法一起记下来：裸 `transpose` 这条路上是 `rhs=dtod_t,sum_from_type_t,abs_t` 与
`rhs=dtod_t,sum_from_type_t,log_t`；**`vjp` 那条路上四条 `*_t` 一条都不出现**
（`abs` → `dtod_t,sum_stated_t,mul_tensor_t`，`log` → `dtod_t,sum_stated_t,divide_t`，`max`/`min` →
`dtod_t,sum_stated_t,add_tensor_t,mul_tensor_t`，实测 x = [1, -2, 3] / [0, 2, 4]）。所以 `vjp` 答对
不能当这四条律的用例：两样都绿。

## 5 `maximum_t` / `minimum_t`：三次尝试各自的错误文本

律被走到，律自己的余切实例化不了。三次零写法，同一棵树：

| 写法 | 拒绝文本 |
|---|---|
| `where(x >= w, ct, 0.0)`（原文） | `No matching function found for argument types: <> (bool[3], float[3][..], float)` |
| `zeros_like(ct)` | `No matching function found for argument types: <> (float[3][..])` |
| `shape(ct)` | `No candidate of 'shape' applies to (<> (float[3][..])): every candidate's 'when' condition is false. Candidates: (t: T[..]) => int[] — when is_numeric::<T>()` |
| `ct - ct` | `operator '-' is not defined for these operand types` |

`is_numeric::<float[3]>` 为假这一条是根因的正面证据：`ct: T[..]` 在三参实例里把 `T` 绑成了**操作数的
整个类型**，`ct` 是"元素为 `float[3]` 的开秩数组"，`float[3][..]`。律本来写的标量秩
（`where(bool, T, T)`）也写不出来（`tensor:maximum` 没有标量拼法），所以没有一个秩能让这个错位消失 →
**没有用例能分辨对错**，不是"暂时想不出用例"。四次尝试（含 `ct * T(0)` 的 `Unresolved reference:
'T'`）之后律已还原为原文，只有 `relu_t` 上方那句例证注释改过。

## 6 闸门

- `feat/compile/transforms`：合 develop 前 172 total / fail 0（矩阵化后 174 / fail 0）。
- `feat/compile` + `feat/docs_links`：2355 total / fail 0。
- 完整 `feat`：见合并后的那一轮，门槛 `fail = 3` 且逐字是三个 `matmul_t` 基线、`killed 0`。