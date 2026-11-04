# 同一个原因（声明未陈述形状），该用哪个词？

2026-11-04 夜，`lane-vectorlaw`。两句话是同一件事的两种说法，而被读成了两件事：

```
// 直接 transpose(f)，f 的参数是 float[...]：
TRANSPOSE function=loss_mean skip=parameter-shape uri=tensor:sum node=6 param=0 param-type=float[...]
RuntimeDiag::TransposeNeedsParameterShape
  The transpose of 'loss_mean' needs the shape of the parameter 'x', and the type 'float[...]' does not
  state it.  Write the extents the parameter's value has in its declaration ...

// 同一个原因，走 vjp（split 的 backward 那一半）：
TRANSPOSE function=split_of_jvp_of_sum_of_product_backward skip=cotangent-needs-a-value
  uri=tensor:matmul position=0 reason=the rule's right side reads a value the caller does not carry
```

**原因一个**：律读的那个操作数，形状在程序里没有写下来（参数声明陈述 rank、不陈述长度，转置后的函数
又只被交 cotangent、被求导的参数没有端口）。**词两个**，而 `cotangent-needs-a-value` 把「一个形状没地方放」
说成了「一个值拿不到」——今晚这一族的两次误读（`docs/handover/reports/2026-11-04-b3-reverted-and-the-ten-it-does-not-explain.cn.md`
的十连红归类、`docs/handover/reports/2026-11-04-the-seven-reds-classified-by-what-the-port-states.cn.md` §3.1 的
「`extents=stated` 所以类型答不出」）都从这句开始，第三次是本轮的 `float[]` → `float[2]`
（`docs/handover/reports/2026-11-04-the-two-vector-laws-ask-for-a-shape-the-declaration-does-not-write.cn.md` §五）。

## 1. 两个词今天在哪（`git grep -n`，tip `ca464bca2` 之上）

| 词 | 行数 | 文件数 | 其中 |
| --- | --- | --- | --- |
| `cotangent-needs-a-value`（日志里的词） | **110** | **39** | `test/plans` **33 行 / 3 个 plan**（`compile/transforms` 28、`compile/transpose_array_product` 4、`transforms/split` 1），其中 **18 行是断言**（`expect_*_contains` 或 `known_fail_msg`）、15 行出现在 `desc`/注释里；`docs/` 25 个文件；代码只有 `modules/transforms/transpose.cpp` 的一张词表 |
| `CotangentNeedsAValue`（枚举） | 8 | 4 | `modules/transforms/transpose.{h,cpp}`、`transpose_operator.cpp`，外加 1 个 docs 页 |
| `TransposeNeedsParameterShape` | 25 | 18 | `test/plans` 2 行（`compile/rules_angle_args` 的一条负例）；其余在 `modules/transforms/`、`docs/` |
| `parameter-shape`（同一原因的日志词） | 12 | 8 | 负例的 stderr pin 与文档 |

**18 条断言押在 `cotangent-needs-a-value` 上**，这就是这件事不能顺手改的原因，也是它值得单独一页的原因。

## 2. 三条路，各自要付什么

- **甲：两词都留，只把入口补齐。** 让 `vjp`/`split` 那一支在「律读的是形状、声明没写」时也报
  `parameter-shape`，即两处拒绝点（`modules/transforms/transpose.cpp` 的 `ruleCotangent` 调用者）先问
  `state.shapeWanted`。**改动最小、18 条 pin 一条不动**；代价是**两个词都还在**，「要一个值」仍会被读成
  另一件事，这类误读只是少一个入口。
- **乙：一个类一个词。** `TransposeNeedsParameterShape` / `parameter-shape` 是这个类的词；
  `cotangent-needs-a-value` 只留「律读的是**元素**」那一支（`relu_t` 的 `x > 0.0`、`mul_tensor_t` 的 `ct * w`），
  并**改名成它真正说的那件事**（如 `law-reads-an-element`）。代价：18 条断言逐条过、3 个 plan 改词、
  `TransposeRefusalCause` 表与 `test/tools/transpose_refusal/check.py` 同步、`docs/` 25 个文件里的说法跟着改。
- **丙：什么都不改，只加注释。** 代价已经付过三次（见上），第四次只是时间问题。

## 3. 默认（下一轮照它做，不再问）

**乙。** 理由只有一句：**一个类只能有一个词**，而「要一个值」把形状说成了值——**今晚所有误读都从这句开始**。
`TransposeNeedsParameterShape` 是那个词（它点名参数、说清「声明没写」、给出改法）；`cotangent-needs-a-value`
要么改名成「律读的是元素」，要么在「声明不写形状」这一类上消失。

**本轮按默认做到哪一步**：只做清单与两处入口的判定（上表 + 第 2 节甲/乙的代价），**不重命名**——
18 条断言属全树，要一次做完，留给拿到这条裁定的一轮。落地时的验收判据写死在这里：
**同一个程序，`transpose(pick)` 与 `vjp(pick)` 遇到同一个原因必须打印同一个词**，
一条负例（两份 stderr 逐字相同的那一段）就是它的读数。