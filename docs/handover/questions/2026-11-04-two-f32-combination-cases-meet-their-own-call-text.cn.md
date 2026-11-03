# 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字）

## 问什么

`test/cases/transforms/compositions/` 里 `jvp_vjp_f` 与 `grad_vjp_g_zero` 写着 `vjp(f)(x, ct)`——把余切交给
**前向那一次调用**。`stdlib/vjp.cml#vjp` 声明的面是 `Vjp<F> = <..WithOf<F>> (..ParamsOf<F>) =>
(ResultOf<F>, Pullback<F>)`：余切是 **pullback 的参数**（`vjp(f)(x).1(ct)`），这是作者 2026-11-01 的裁定，
JAX 的面。用例自己的头注释又把调用拼写标成"settled with M4"、把数字标成契约。

**问句**：这两条用例的**调用拼写**与 `grad_vjp_g_zero` 的**四个 0**，哪一样算契约？（拼写改动只会改变"第一条
拒绝"，数字改动会改变 M6 的判据。）

## 读数（2026-11-04，实测）

- `transpose` 的 `f32` 出口墙已经拆了（本车道）：`vjp` over `(x: f32) => f32` 现在能定型、能跑，pullback 读
  `(ct: f32) => (f32,)`。同一个程序两种宽度并列：`vjp` of `x*x*x` at 3 在 `f32` 与 `float` 都是值 27.0、余切
  27.0；`x*3` 的余切在 `f32` 是 3.0，`x+1.5` 是 1.0。
- 这两条用例现在的第一条拒绝，来自它们自己的写法：
  `Function of type '(__pack_0_0: f32) => (f32, (ct: f32) => (f32,))' cannot be called with argument types: <> (f32, f32)`
  （另一条同形，`f32[4]` 对 `(f32[4], f32)`）。
- 写法之后还有两道墙，**都在 `float` 上同样成立**（同日用等价的 `float` 探针量），所以都不是 `f32` 的事：
  - `jvp` 穿 pullback：`jvp has no forward transform for this function`；
  - `grad` 穿 pullback：`cannot differentiate through this call of a function value: … the function is the result
    of 'transforms:split'`（`stdlib/vjp.cml#vjp`）——这就是 `grad_vmap_f` 卡的那道墙；
  - 把批轴放到外面（`jvp(x => vmap(pull)(x))(xs, dx)`）则撞 `vmap has no rule for 'a constant'`——`vmap_grad_f`
    的墙。
- `grad_vjp_g_zero` 还多一处**数字**问题：它的 body 可达的读法是 `sum(vjp(g)(w).1(1.0).0)` = `sum(2w)`，对 `w`
  的梯度是每项 2；页面
  （`docs/design/program-transforms/m6-combination-numbers.cn.md`）说的"vjp 对 M 线性、二阶导为 0"说的是
  **二阶**导，而这个程序只求了一阶。四个 0 与这行正文对不上。

## 三条路，各带代价

**A. 只改拼写，数字不动**（`vjp(sq)(x).1(v)`；`grad_vjp_g_zero` 给函数一个 `with <w: f32[4]>`）
- 代价：改的是用例正文；`vmap(jvp(pull))` 还要重排成 `jvp(x => vmap(pull)(x))` 才类型正确（`vmap` 只收一元
  函数），而重排后撞 `vmap` 的常量墙——于是 `jvp_vjp_f` 的第一条拒绝换成 `grad_vmap_f` 那一族。
- 好处：两条用例从此指着**真正的墙**，墙一倒就能转绿；`grad_vjp_g_zero` 除外（见上，数字仍对不上）。

**B. 正文与数字都不动，只把 `known_fail_msg` 钉在今天实测的那句上**（本车道这一轮的做法）
- 代价：XFAIL 的理由暂时是"用例自己的写法"，不是编译器缺口；读者要读 note 才知道墙在哪。
- 好处：一行正文都没动，四个数字原样；`feat/transforms/compositions` 不新增红；拼写与数字两件事都摆到这一页
  等一句裁定。

**C. 让 `vjp` 再接一种面**（`vjp(f)(x, ct)` 直接给余切）
- 代价：动的是作者 2026-11-01 的裁定，`stdlib/vjp.cml`、`stdlib/types.cml#Vjp`、以及所有 `vjp` 的用法一起动；
  两条用例仍各自卡在 jvp/grad 穿函数值那两道墙上，所以 C 也不换来绿。
- 好处：两条用例的正文不用改。

## 默认（本轮按这个开工，不等回复）

**B**。理由是 A 与 C 都换不来这 4 条变绿（墙在别处），而 A 会顺手把"数字对不上"这条发现藏进一条新的 XFAIL
理由里；B 把两件事都留在页面上，且一行用例正文、一个期望数字都没动。作者的裁定若是 A 或 C，只在 `test/` 与
`stdlib/` 那一层回退，`modules/transforms` 的 `f32` 改动（`transposeSignature`、`addUriFor`、`zeroIn`、
`ruleCotangent` 的实例类型）与裁定无关，保留。