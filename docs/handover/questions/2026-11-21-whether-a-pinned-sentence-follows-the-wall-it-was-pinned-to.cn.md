# 一句被钉住的拒句，墙自己挪走之后，用例该跟着挪吗

**状态**：开放。这一页是 `fix/nest-quartic` 上量到的一件事：`nested.nest_quartic` 从 **PASS**
变成 **FAIL**，而它不是新错——它钉的那句拒句，正是 `2a2cf31a8`（`lane-split-type-tangent`）要拆掉
的那道墙。拆掉之后墙往后挪了一步，落到 `transpose` 上，于是钉旧句子的一批用例一起变红：**15 条
「墙挪了」+ 2 条内部错误**。这一页要的裁定只有一件：**这批红是重钉回绿，还是按红记账。**

## 1 读数：`nested.nest_quartic.nvm` 现在逐字（`2551155bb`，Release，`std::nvm`）

```
[FAIL] nested.nest_quartic.nvm
    [INFO] output does not contain 'split does not build this function (the value half
           reads the tangent of parameter 0, which the forward half does not take)'
    [stdout] (split_of_jvp_of___g_361____first_with_as_param_forward):?:?: [Error]: An error
             has occurred: transforms: transpose has no transpose for this function: the
             callee's transpose was built, but the parameter at position 3 reads a value
             this call does not carry.
    [stderr] TRANSPOSE function=jvp_of_autodiff::mul_tangent<float> graph=433
             type=(float, float, float, float, (float, float)) => (float, float, float, float)
             nodes=16 arms=0 rhs=add_t,mul_t
    [stderr] TRANSPOSE function=jvp_of_split_of_jvp_of_split_of_jvp_of_quartic__first_with_as_param_forward_backward
             skip=call-value-missing param=2 node=12 arg=10
```

**它是拒句**，不是编译错、不是运行错、不是数值不符：`expect_exit = 1` 仍然满足（`[INFO]` 里没有
「expected exit 1, got …」这一行），程序仍然一个数都不打印。变的只有那句话。

## 2 它当初为什么 PASS：它的绿是那句拒句给的

- `6a21597db` 上 `split` 拒：`the value half reads the tangent of parameter 0, which the forward
  half does not take`；`nested.nest_quartic` 是 `kind = "negative"`，`expect_output_contains`
  钉的就是这一句 ⇒ PASS。
- `2a2cf31a8` 起 `split` 从提升的类型读哪几段是切向（不再拒），这句从此不出现；墙往后一步，
  落到 `transpose` 的 `participates` 读法上。
- 所以这条的 PASS 从来不是「二阶梯度对了」，而是「它停在这句话上」。墙挪走，绿就没了，
  **而程序没有变坏**。

## 3 同一堵墙、三种标签：`nested` 的三条差在哪

`feat.transforms.nested` 11 条，`2551155bb` 上 **3 pass / 3 fail / 4 known**。三条红打的是同一张
骨架 `split_of_jvp_of_split_of_jvp_of_<f>__first_with_as_param_forward_backward`、同一句
`transpose` 拒句，差别**只在标签**：

| 条目 | 标签 | 输出的那句话 |
| --- | --- | --- |
| `nested.nest_square` | `known_fail_msg` | `known failure changed its mind: expected "split does not build …", got …` |
| `nested.nest_cube` | `known_fail_msg` | 同上 |
| `nested.nest_quartic` | `negative` | `output does not contain 'split does not build …'` |

三者的 `skip=call-value-missing` 分别是 `node=4 arg=0`、`node=8 arg=6`、`node=12 arg=10`——同一张图
多一次乘法。**`quartic` 并没有走到别人没走过的路上；它只是唯一一条在墙挪之前被记成 `pass` 的。**

## 4 价钱是 17 条，不是 11 条

`2551155bb` 上按名字数（`nested` 见上节）：

- **同一句 `transpose` 拒句，15 条**：`nested.nest_{square,cube,quartic}` 3 条；`compositions.nested_grad_min.{nvm,fvm,jit,prl}`
  4 条、`compositions.nested_grad_through_a_binding.*` 4 条、`compositions.hessian_of_a_quadratic.*` 4 条
  （后 12 条钉的都是更早那句 `jvp has no forward transform for this function`，现在读到的是这句）。
- **不是这一类，2 条**：`compositions.jvp_vjp_f.{nvm,fvm}` 读的是
  `Pass 'std::nvm' failed with an internal error: Runtime graph 'jvp_of___g_359__' breaks its edge
  invariants (1 violation(s))`——一个内部错误，不是拒句。按 1.0 的红线第 1 条（程序造不出结构上坏的
  图），它和上面 15 条不是同一笔账，不该一起记账。
- **其它计划不动**（同一次量，同一棵树）：`feat.compile.transforms` 211 pass + 5 known / 0 fail；
  `feat.transforms.split` 30/30；`feat.showcase_s2` 10/10；`feat.compile` + `feat.opt`
  4286 total / 4278 pass / **0 fail** / 8 known。

## 5 三条路，默认 B

- **A 把这 15 条重钉到新的 `transpose` 拒句。** 代价：15 处一行一条，读数立刻全绿，**什么都没修好**；
  而 `transpose` 一落地还得再钉回来一次，等于每次墙动都付一遍。它唯一的价值是把读数与实现对齐——
  而那正是「改期望让红变绿」的经典形状，`known_fail_msg` 尤其危险：它会把「墙挪了」写成「一直是这道墙」。
- **B（默认）保持现状、等裁定：一条都不重钉，让它们按红记账。** 这一页就是那 15 条的名字。
  理由：它们钉的是实现里的一句话，不是语言的一条性质；实现正在被正确地改，句子还会再动一次
  （`docs/handover/questions/2026-11-20-what-transpose-reads-a-lifted-kernel-call-by.cn.md`，默认 B 就是
  `transpose` 换读法）。代价：`develop` 上常驻 15 条红，任何「new red = 0」的读数必须先把这批名字减掉；
  以及 `nested` 计划的 `pass` 会一直停在 3。
- **C 把钉的对象从「那句话」换成「哪一层拒的」。** 能挺过墙挪，代价是把「拒得对」降级成「拒了」：
  `nested.nest_quartic` 这种只钉一句词的用例就失去意义（它本来就是靠那句话才成立的 negative）。
  与 B 不冲突，可以作为 B 裁定之后的下一次改动。

**先量什么再选**：`compositions.jvp_vjp_f` 那 2 条内部错误要在 `6a21597db` 上量一次（它在那时是
一句拒句还是这个内部错误）。若那时就是内部错误，`2a2cf31a8` 无罪；若那时是拒句，这是它带进来的一笔
新账，要单独一格，并且按红线第 1 条优先于本页的记账问题。

## 6 这一轮被量到、也值得逐字留着的事

- **`nested.nest_quartic` 的用例注释还停在两堵墙之前**：`test/cases/transforms/nested/nest_quartic.cml`
  里的逐字块写的是 `transforms: jvp has no forward transform for this function`，并给出「它的结果
  类型没有切空间」的解释——那个解释正是 `2da9735da` 改掉的。计划条目（`desc`）已经按日期改过两次，
  用例注释一次都没跟上。它不是期望，不影响读数，但读者会先读到它。