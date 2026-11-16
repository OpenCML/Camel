# 案例一旦改用库的 `grad`，`break_rate` 那个重写该怎么钉？

> 一页一问。**默认建议写死在下面，本轮按默认开工，不另等。**

## 这一页要的答复

`examples/case-study/train.cml` 迁到 `stdlib/vjp.cml#grad`（`grad(loss)<y>(layer, X)`，张量写成开秩
`f32[?, ?]`，`a4e53b1dd`）之后，`examples/case-study/edits.cml#break_rate` 那条重写不再「拒绝提交」，
而是「拒绝选点」：程序照跑、exit 0，`examples.case_study.break_rate` 在四个后端上全部从「钉住 sec 4.2
第 2 条」退化成空转。**所以问题是：这条重写与库的 `grad` 同时在场时，用什么钉住「把模型放进学习率位置」
必须被 `commit` 拒绝？**

## 读数（为什么它变成一个问题）

`break_rate` 的前提是自己写着的：

```
let rate  = only(p, (n: Node): bool => constant_of(n, 'float'))
let wrong = only(p, (n: Node): bool => constant_of(n, 'Linear'))
```

`only`（`edits.cml#only`）在一个条件命中 **多于一个或零个** 节点时返回 `null`，重写于是原样返回程序。
这条设计是演示的要点本身：「按节点是什么来选，且计数必须为一」。迁移前，案例的程序里恰好只有一个
`float` 常量（`0.1`，学习率）和一个 `Linear` 常量（`w0`）。

库的 `grad` 把自己的 float 常量带进了从 `main` 可达的图里 —— `grad` 的正文是
`pullback(1.0)`（`stdlib/vjp.cml#grad`），于是一个 `float` 常量变成不止一个，`only` 返回 `null`。

实测（主机 `out/latest`，四个后端一致）：

```
examples.case_study.break_rate.nvm/fvm/jit/prl   expected exit 1, got exit 0
stdout: loss before: 3.7500 / loss after: 0.0000 / [[1.9999925], [-0.99998784]]
        （与未经重写的程序完全相同 ⇒ 重写没有改动任何东西）
stderr: 1988 行 TRANSPOSE/REINTERP 变换记录，golden 里没有这些行
```

本线因此把 `train.cml` 退回 develop 的版本。**代价也要记在账上：退回之后，fast 层没有任何用例对
带两个 `with` 值的函数求导** —— 而这正是 `GradientOf<F>`（`stdlib/types.cml`，`grad` 带整条 `with`
列表）那笔修复唯一的验收形状。退回不是「这条不做了」，是把验收挂在了别的用例上：目前没有别的用例。

## 三条路与各自的代价

1. **重钉 `break_rate` 的选点。** 换一个仍然是「按节点是什么来选」、但在这份程序里唯一的条件
   （例如「顶层图里那个 `float` 常量」，或「`sgd` 的 rate 参数实参」），交换与拒绝都不变。
   **代价**：演示里「程序恰好持有一个 float 常量」这句话要改写，而它是论文 sec 4.2 的读法之一；
   改完之后 `break_rate` 钉的仍是「类型不符在 commit 处被拒」，但选点理由弱了一档。
2. **让库的 `grad` 不写自己的 float 常量**（seed 由 `vjp` 的参数给，或用 `zero_tangent` 这样的类型构造）。
   **代价**：`stdlib/vjp.cml#grad` 的写法为一份演示让路，而「计数必须为一」变成库必须长期记住的一条约束。
3. **这一步不迁**（`train.cml` 保持 `autodiff` 的 `grad`；库的 `grad` 另立一条用例：两 `with` 值、
   一次求导、不做 200 步训练，放 `test/cases/compile/transforms/`）。
   **代价**：「只留一份 `grad`」（`4ae2c0179`）的迁移这一步仍未做，演示里「库的变换」那行仍是引擎算子。

## 默认建议（按此开工，不另等）

**本轮走 3，迁移那一步做的时候走 1。**

3 的读数是：一条 pinned 的验收（sec 4.2）不能为了迁就一次移植而换成更弱的期望，而移植本身不是裁决，
是「只留一份 `grad`」那一步的一部分 —— 那一步没到，案例先留在能钉住它的地方。1 的读数是：`break_rate`
要钉的是「类型不符被 `commit` 拒绝」，不是「程序里只有一个 float 常量」；换选点保住了前者，只在
「选点理由」上让一步，而 2 是让库去迁就演示，方向反了。

## 这一页不改变的事

`stdlib/vjp.cml#grad` 的 `with` 包、`GradientOf<F>` 的 rest 拼接、`dtod_t` 的各宽度转置与十二个泛型
余切辅助函数都留在分支上；本页只请求「验收挂在哪个用例上」的裁定，不请求任何一条回退。