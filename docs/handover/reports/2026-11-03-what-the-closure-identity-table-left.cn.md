# 闭包身份表这一笔留下了什么：四条给合并车道

**分支** `lane-closurewk`，**提交** `34788b554`（身份表、`split`/`transpose` 返工、删 `kGraphFlagClosureCarriesParameters`）
之后又合了两次 develop（`fbee6741b`、`8c4401e66`，后者到 `5491d4bf6`）。这一页只说合并前后要有人知道、
且看代码看不出来的四件事。读数都在远程 88zjwei 上取，`-Tier fast -Jobs 4`。

## 1. 七条 `grad_` 绿，数值逐字未变

`grad_array_{no_int,with_int,one_element,two_elements_first,sum,product}`、`grad_of_an_array_parameter`、
`grad_where_the_tangent_ignores_the_operands`、`grad_constant_beside_a_value`、`grad_of_a_division`——
全 PASS，且与 `0c7e5f20a` 上逐字相同（`[1, 1]`、`[1]`、`[1, 0]`、`[2, 3]`、`−1.0`、`6.0`）。
**这八条"墙"用例在我这一笔之前就是绿的**，它们是端口次序那一笔（`e7d73bf00`）才转红的，不是我修好的。

## 2. `h` 经 `vjp` 仍答不出 `(0.0,)`

作者的判据是"`h` 经 `vjp` 答 `(0.0,)`"，`h` 的定义在
`docs/handover/reports/2026-11-03-a-member-port-slot-nobody-fills.cn.md`（第 1 节）：
`h(x) = loop s = 0.0 for i in [1,2,3,4] do s + 2.0`，`h' = 0`。今天：

```
vjp(h)(1.0) → rc=1
  TRANSPOSE function=split_of_jvp_of_h_backward skip=state-unread node=2 origin=0
  REINTERP split function=jvp_of_h ... residuals=1 loop=1 tape=1 invariants=0
对照 k(x) = loop s = 0.0 for i in [1,2,3] do s + 2.0 直接 transpose(k):
  k(1.0)= 6.0   transpose(k)(1.0)= (0.0)   transpose(k)(2.0)= (0.0)   rc=0
```

**直转已经答 `(0.0)`，经 `vjp` 不答。** 差别是零答案要让"零经过所携带的值"，而后半携带的捕获是 tape
（`(float,)[]`），裸常量零会让捕获变成没人填的端口槽——那页第二节量过。

## 3. 手写闭包的 `transpose` 与 `split` 的两半一致

`residuals_in_the_closure.nvm` PASS，断言 `value= 18.0`、`back dx= 12.0  back dy= 9.0`、
`TRANSPOSE function=split_of_jvp_of_f_backward graph=`。即：把两个前导实参故意传 `0.0`、残差从闭包读，
答案仍是 `(12, 9)`。**参数原值当残差这件事，在图上落在不变量表多出的那一格**（`invariants` 1→2），
`residuals=` 的数值前后都是 3，没有变。

## 4. 端口次序必须排在闭包之后，且还欠一处调用点计数

把 `e7d73bf00`（抬升列表 `(values..., tangents...)`）cherry-pick 到我这一笔之上：零冲突（`jvp.cpp` 39/35、
`split.cpp` 78/36、`transpose.cpp` 26/6），但六条 `grad_array_*` 与 `grad_of_an_array_parameter` 复红——
**停点不是 `TRANSPOSE-NEEDS param=0`，而是 `modules/transforms/transpose.cpp` 里那个计数闸**：

```
TRANSPOSE function=split_of_jvp_of_g_backward skip=call-no-transpose node=4 values=2 member=1
```

⇒ 两笔的顺序是**先闭包、后端口次序**；端口次序那一笔还要补"调用点按身份给/省一个 own value"，
否则 `arguments.size()` 与 `memberValues` 对不上。回退用 `git checkout -- .` 干净。

## 附：这一笔自己带来的三条账红，已修

`runtime.graph_draft_ledger.never_rises`（三处注释把类型名拼了出来，账本数字只许降）、
`docs.links.questions_index`（新页让生成表从 11 页变 12 页，用 `--questions-index --write` 重新生成）。
全量复测 `total: 8543, pass: 8521, fail: 3, skip: 8, known failures: 11`，killed 0；三条红是
`docs.links.citations`（develop 自带：`stdlib/rulesets/jvp.cml` 用行号引 `stdlib/tensor.cml`，
修在 `oprules` 的本地提交里，不在任何远端分支上）、
`transpose_loop_escape.two_values_of_two_kinds`、`transpose_loop_escape.second_state_of_the_same_loop`
（`TRANSPOSE op ... same=no` 之后 `Runtime indirect call norm-arity mismatch`：转置成员长出捕获端口，
而调用点按类型只写了一个实参——端口次序/捕获那一笔的账）。