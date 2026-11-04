# 两条 f32 组合用例撞上的是自己的调用写法（还有一处数字）

> 2026-11-04 第二次读：本轮已按 **A+B 的并集**落地——旧的四条 XFAIL 条目原样留着，另加了两条用**正确调用
> 写法**写的新条目（读数见下）。这一页因此只剩两件要裁的事，都在文末。

## 问什么

`test/cases/transforms/compositions/` 里 `jvp_vjp_f` 与 `grad_vjp_g_zero` 写着 `vjp(f)(x, ct)`——把余切交给
**前向那一次调用**。`stdlib/vjp.cml#vjp` 声明的面是 `Vjp<F> = <..WithOf<F>> (..ParamsOf<F>) =>
(ResultOf<F>, Pullback<F>)`：余切是 **pullback 的参数**（`vjp(f)(x).1(ct)`），这是作者 2026-11-01 的裁定，
JAX 的面。用例自己的头注释又把调用拼写标成"settled with M4"、把数字标成契约。

**还剩两个问句**：

1. 这两条用例的**调用拼写**要不要改成今天的面？（改，就是换掉它们现在钉的那句；不改，它们永远钉的是自己
   的写法。）
2. `grad_vjp_g_zero` 的**四个 0** 想钉的是**几阶**导？（一阶可达的读数是每项 2；二阶在本树求不出来，停在哪
   句见下。）

## 读数（2026-11-04，实测）

- `transpose` 的 `f32` 出口墙已经拆了（本车道）：`vjp` over `(x: f32) => f32` 现在能定型、能跑，pullback 读
  `(ct: f32) => (f32,)`。同一个程序两种宽度并列：`vjp` of `x*x*x` at 3 在 `f32` 与 `float` 都是值 27.0、余切
  27.0；`x*3` 的余切在 `f32` 是 3.0，`x+1.5` 是 1.0。
- **新加的两条条目**（`compositions.pullback_read_at_a_cotangent`、
  `compositions.pullback_of_a_sum_of_squares`，各 nvm/fvm）：在未改的树上 4 条全红（`transpose` 还在拒 `f32`
  出口，程序根本跑不起来），在本树 4 条全绿，读数逐位一致——
  `f(x)=x*x`：`(x, ct) = (3,1), (3,2), (-1.5,0.5)` 分别 `6 / 6`、`12 / 12`、`-1.5 / -1.5`（`f32` 与 `float`
  各一列）；`g(v)=sum(v*v)` 在 `v=[1,2,3,4]`：`ct=1` 和为 `20 / 20`（手加逐项同值），`ct=0.5` 和为 `10 / 10`。
- 旧的两条 `f32` 条目现在的第一条拒绝，来自它们自己的写法：
  `Function of type '(__pack_0_0: f32) => (f32, (ct: f32) => (f32,))' cannot be called with argument types: <> (f32, f32)`
  （另一条同形，`f32[4]` 对 `(f32[4], f32)`）。
- 写法之后还有两道墙，**都在 `float` 上同样成立**（同日用等价的 `float` 探针量），所以都不是 `f32` 的事：
  - `jvp` 穿 pullback：`jvp has no forward transform for this function`；
  - `grad` 穿 pullback：`cannot differentiate through this call of a function value: … the function is the result
    of 'transforms:split'`（`stdlib/vjp.cml#vjp`）——这就是 `grad_vmap_f` 卡的那道墙，`f32` 与 `float` 逐字
    相同；
  - 把批轴放到外面（`jvp(x => vmap(pull)(x))(xs, dx)`）则撞 `vmap has no rule for 'a constant'`——`vmap_grad_f`
    的墙。
- **二阶到底求没求出来**：没求出来，停在这一句（`f32` 与 `float` 同一句）：

  ```
  autodiff: cannot differentiate through this call of a function value: which function it calls is not evident
  where it is called: the function is the result of 'transforms:split'.
  ```

  第一阶没落地，第二阶就无从谈起。同时量到一件相关的事：**套两层 `grad` 本身是通的**
  （`with <x: float> func d1(): float { return grad(sqw)<x>() }`，`grad(d1)<2.0>()` 读 `2.0`，即 `x*x` 的
  二阶导），所以挡住二阶的不是"阶数"，就是"穿一次函数值"。
- 因此 `grad_vjp_g_zero` 的数字含混有了具体形状：它的 body 可达的读法是 `sum(vjp(g)(w).1(1.0).0)` = `sum(2w)`，
  对 `w` 的梯度是**每项 2**；四个 0 属于"再对那个常数求一次梯"。**本轮没有用一阶的数冒充二阶**，也没有新增
  一条把 0 钉住的条目（那会把已知失败数从 8 抬到 9）。

## 三条路，各带代价

**A. 只改拼写，数字不动**（`vjp(sq)(x).1(v)`；`grad_vjp_g_zero` 给函数一个 `with <w: f32[4]>`）
- 代价：改的是用例正文；`vmap(jvp(pull))` 还要重排成 `jvp(x => vmap(pull)(x))` 才类型正确（`vmap` 只收一元
  函数），而重排后撞 `vmap` 的常量墙——于是 `jvp_vjp_f` 的第一条拒绝换成 `grad_vmap_f` 那一族。
- 好处：两条用例从此指着**真正的墙**；`grad_vjp_g_zero` 除外（数字仍对不上，见上）。

**B. 正文与数字都不动，另加用正确写法的条目（本轮做的）**
- 代价：一个计划里同时有"钉墙的旧条目"和"钉可达读数的新条目"，读者要读 note 才知道哪条是哪条。
- 好处：一行用例正文、一个期望数字都没动；`feat/transforms/compositions` 从 `10 / 2 / 0 / 8` 变
  `14 / 6 / 0 / 8`——绿的多了 4 条，已知失败仍是 8 条。

**C. 让 `vjp` 再接一种面**（`vjp(f)(x, ct)` 直接给余切）
- 代价：动的是作者 2026-11-01 的裁定，`stdlib/vjp.cml`、`stdlib/types.cml#Vjp`、以及所有 `vjp` 的用法一起动；
  两条用例仍各自卡在 jvp/grad 穿函数值那两道墙上，所以 C 也不换来绿。
- 好处：两条用例的正文不用改。

## 默认（本轮按这个开工，不等回复）

**A+B 并集已经落地**：旧四条条目的 `known_fail` 标记原样保留（其中两条 `f32` 的 `known_fail_msg` 换成了今天
实测的那句——runner 对不上钉的句子会直接判红，那是它自己的规矩，不是本轮的取舍），新加两条正确写法的条目并
已验过先红后绿。

剩下两件等作者一句话：**（1）** 那两条用例正文的调用写法改不改（选 A 就改，选 C 则连 `vjp` 的面一起动）；
**（2）** `grad_vjp_g_zero` 的四个 0 是几阶导——若确认是二阶，就先关掉"穿函数值"那道墙，本轮已把"停在哪一句"
量准；若确认是一阶，数字应当是每项 2，那时再改期望。作者的裁定若是 A 或 C，只在 `test/` 与 `stdlib/` 那一层
回退，`modules/transforms` 的 `f32` 改动（`transposeSignature`、`addUriFor`、`zeroIn`、`lawInstanceTypes`）
与裁定无关，保留。