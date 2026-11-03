# 库层要不要也有 `has_aux`，还是"配对在哪写就在哪拆"就够了？

## 问什么

`has_aux` 今天只有**引擎一种写法**：`grad(f, has_aux: true)` / `with_grad(f, has_aux: true)`
（`stdlib/signatures/autodiff.cml` 的两条 `#op('autodiff:grad_aux')` / `#op('autodiff:with_grad_aux')`，
核在 `modules/autodiff/operators.cpp#GradResolver` 与 `modules/autodiff/pullback.cpp#Engine::gradient`）。

**库层（`stdlib/vjp.cml` 的 `vjp` / `grad`）没有 `has_aux`**，而且不是"忘了写"：`vjp` 的种子是
`float` 结果的余切，返回配对时那条 `transpose(lin)` 直接拒绝（读数见下）。三条路都自洽：
(a) 库层不实现，配对在哪写就在哪拆（今天的答案）；(b) 给 `vjp` 加一个 `has_aux` 形参，让它返回
`((value, aux), pullback)`；(c) 只在库层加 `grad` 的 `has_aux`，`vjp` 不动。

## 读数（可复核）

同一次探针，树 `D:\Projects\Camel-wt\hasaux` @ `545b24d92`，2026-11-04 03:5x（本地），主机
`88zjwei` load 15.7→59.6，二进制 `out/latest/bin/camel`：

- **引擎有 aux**：`f(w) = (w*w, w+10.0)` 在 3 → `grad(f, has_aux: true)` = `(6.0, 13.0)`，
  `with_grad(f, has_aux: true)` = `((9.0, 13.0), 6.0)`；`frozen(w) = (1.0, w*w)` → `(0.0, 9.0)`。
  值 9.0 / 附带 13.0 / 梯度 6.0 三个数互不相同，`frozen` 的梯度 0.0 说明附带值确实只被搬运。
- **库层有 aux（合成）**：`vjp(value_of)(3.0)` = `(9.0, pullback)`、`pullback(1.0) = (6.0)`、
  `aux_of(3.0) = 13.0`，与引擎的三个数在程序里**逐个 float 相等**（`same_value/same_aux/same_grad`
  全 `true`）。
- **`vjp` 命名形参**：`vjp(loss, has_aux: true)` →
  `SemanticDiag::ParamArgumentUnknown: 'has_aux' is not a parameter of ...`（按名字拒）。
- **`vjp` 收配对函数**：`vjp(loss)(3.0)` →
  `SemanticDiag::ArgumentsRejected: Invalid call to 'transpose' ... 'transpose' takes a function that
  returns a 'float'`，落在 `stdlib/vjp.cml` 的 `transpose(lin)` 那一行。
- 四条用例在 `std::nvm` / `std::fvm` / `std::jit` / `std::prl` 与 `std::macro` 前缀上逐字节相同
  （`test/plans/feat/modules/autodiff.plan.toml` 的 `has_aux.*`）。

所以"库层没有 `has_aux`"**不是功能缺口而是一种选择**：三个量今天都拿得到，且与引擎逐位相同；
分歧只在**写法**——JAX 的用户会写 `grad(f, has_aux=True)`，库层的用户要自己写两个投影函数。

## 三条路，各带代价

**A. 维持今天的答案（库层不加 `has_aux`，配对在哪写就在哪拆）**
- 一行代码不改；`has_aux.*` 的八条用例把今天的行为钉住（含两条按名拒绝）。
- 代价：库层用户写"值 + 附带 + 梯度"要自己写两个函数（`value_of` / `aux_of`），三个量靠约定对齐；
  JAX 的 `has_aux` 迁移过来时，库层那条腿要改写而不是改一个形参。
- 好处：`vjp` 的定义保持一条（种子是 `float` 的余切），不必为"配对"在库里再开一条形状规则；
  引擎已有 JAX 拼写，一个功能一个写法。
- 另有一条硬理由：库层的 `vjp` 是个 `.cml` 函数，加形参就要在**声明侧**决定"配对怎么拆"，
  而 `transpose` 只接受返回 `float` 的函数——那正是 `has_aux` 不能跟着值走的同一个理由。

**B. 给 `vjp` / `grad` 加 `has_aux`**
- `stdlib/vjp.cml`：`vjp(f, has_aux: bool)` 变成两形（同 `grad` 的两条规则），体内对
  `(..xs) => f<..ws>(..xs).0` 做 `split(jvp(...))`，于是 `transpose(lin)` 拿到的是 `float`。
- 代价：`vjp` 的签名从一条变两条，`Vjp<F>` / `ParamsOf<F>` 之外要再加 `ValueOf<F>`、`AuxOf<F>`
  一类类型函数（`stdlib/types.cml`）；`has_aux` 必须是常量 `true` 的判据要在 `.cml` 层重写一遍
  （引擎那份在 `operators.cpp#GradResolver::resolveWith`）；库里多一条与引擎**语义重复**的路径，
  将来两条必须一起改。
- 好处：JAX 的写法在两条腿上都成立；库层用户不必自己写两个投影函数。

**C. 只给库层 `grad` 加 `has_aux`，`vjp` 不动**
- `stdlib/vjp.cml#grad` 加一条 `grad(f, has_aux: bool)`，体内走 B 的投影 + `vjp`。
- 代价：`vjp` 仍然拒绝配对函数，而 `grad(f, has_aux: true)` 却能收——同一条腿两个形状规则，
  读者要记住"哪个名字能收配对"。好处：改动最小，`vjp` 的定义不动。

## 默认（本轮按这个开工，不等回复）

**走 A**。理由是可量的：三个量今天已经拿得到，且与引擎**逐位相同**（上面两条读数），所以 B/C
买到的是**写法**，付出的是**库里第二条形状规则**（C 还多一条"同名不同形状"的记忆负担）；
而 A 的代价只是库层用户多写两个小函数——那两个函数在 `has_aux.library.cml` 里各占三行，读者一眼
看得出配对是在哪拆的。留 B/C 的唯一强理由是把 JAX 的 `has_aux` 当成**契约**（迁移过来的程序要
原样编译），那不是这一条车道能替作者定的。

本轮按 A 落的：`has_aux.*` 八条用例（引擎/库层 × 有/无 aux 四形式 + 三条按名拒绝），
`docs/` 未改（无新诊断码）。作者若裁 B 或 C，改的是 `stdlib/vjp.cml` 与 `stdlib/types.cml`，
这八条用例里的 `has_aux.library` 与 `has_aux.vjp_named` / `has_aux.vjp_pair` 要按新形状重写
（前者从"投影 + 比较"变成"一个调用 + 比较"，后两条从"拒绝"翻成"通过"）。