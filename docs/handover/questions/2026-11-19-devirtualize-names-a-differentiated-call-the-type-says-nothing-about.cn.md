# `devirtualize` 之后那两条梯度调用没有序——因为 `encode.pb` 的类型里一个子句都没有

2026-11-19 ｜ 来自 lane `lane-xdev-devirt`（`origin/develop` = `91f708389`）
｜ 对象：`test/plans/feat/modules/nn.plan.toml#siamese_shared_encoder.devirtualize_inline.nvm`、
`src/runtime/draft_inline.cpp#devirtualizeClosureCallInDraft`、`src/runtime/draft_inline.cpp#keepCalleeOrdering`、
`src/runtime/resources.cpp#Resolver::call`、`modules/autodiff`（未动）
｜ 读数：`-Plan feat/modules` 与单条探针，Release，逐字见下方

## ① 红是什么（逐字）

```
():?:?: [Error]: 'std::cxx::inline' committed a graph whose conflicting effects are not ordered:
node 32 of graph 'train' (a call of 'encode.pb$lift1'), in one region of graph 'train',
has no order against node 36 of graph 'train' (a call of 'encode.pb$lift1'), …
```

`RuntimeDiag::PassEffectOrder`，出在 `std::cxx::inline` 的 commit 上，不是崩溃、不是数值不等。
三条管线单独量（同一份 `siamese_shared_encoder.cml`）：

| 管线 | 读数 |
|---|---|
| `std::macro std::nvm` | 绿 |
| `std::macro std::cxx::devirtualize std::nvm` | **绿** |
| `std::macro std::cxx::inline std::nvm` | **绿** |
| `std::macro std::cxx::devirtualize std::cxx::inline std::nvm` | **红**（上面那条） |

⇒ **不是 `devirtualize` 一条、也不是 `inline` 一条能单独踩响的**，要两条连着。

## ② 这一对是谁、序从哪里来（实测）

在 `keepCalleeOrdering` 里加一行临时日志（`CAMEL_LOG_WARN_S`），跑红的那条管线：

```
<XDEVPROBE> keepCalleeOrdering func=29  callee=27 kind=5 waits=6
<XDEVPROBE> keepCalleeOrdering func=32  callee=24 kind=5 waits=0
<XDEVPROBE> keepCalleeOrdering func=36  callee=26 kind=5 waits=0
<XDEVPROBE> keepCalleeOrdering func=94  callee=92 kind=5 waits=6
<XDEVPROBE> keepCalleeOrdering func=97  callee=88 kind=5 waits=0
<XDEVPROBE> keepCalleeOrdering func=101 callee=90 kind=5 waits=0
```

**`func=32` 与 `func=36` 正是诊断里的 node 32 / node 36**，两者都是 `kind=5`（`Fill`，闭包）且 `waits=0`。
`devirtualizeClosureCallInDraft` 把这两个 CALL 改成直接 FUNC 之后走 `keepCalleeOrdering`，那里
`if (!header || draft.waitInputsOf(callee).empty()) return;` ——FILL 自己的等待边是空的，于是**没有序要保**，
那一句也就不写。相邻的 `func=29`/`94` 的 FILL 有 6 条等待边，序保住了。

所以这一段里**能确定的是**：改名没有拆掉一条序——那两句的 `waits=0`，`keepCalleeOrdering` 按它自己的
规则没事可做，而 commit 的判词本身（`has no order against`）也说明两条之间没有任何一条边（值边、
控制边、序边都算）能连起来。这一对在改名之后无彼此。它们之前是否靠某条值边被排过，本页没有量到，
不给结论；要判这一点，得把 `orderIncomplete` 的判词在两条管线上各取一次。

## ③ 那它为什么以前不被拒（判据在读数，不在序）

`orderIncomplete` 只在两条访问**冲突**时才问序。两条都读不出任何访问（`pure`）就没有冲突，也就没有这条红。
实测这两条调用的读法是「写一切」，两个形态都一样：

`std::macro std::jgir` 里每个图打印的是它的 `funcType()->toString()`（effects plan 2.2 的口径），
`std::cxx::devirtualize` 之后逐字：

```
encode.pb :: (f32[...]) => ({ weight: f32[...], bias: f32[...] }, f32[...])
linear.pb :: (f32[...]) => ({ weight: f32[...], bias: f32[...] }, f32[...])
pair_loss.pb :: (float) => (f32[...], f32[...])
encode.fwd :: <Encoder> (f32[...]) => (f32[...], (f32[...]) => ({ weight: f32[...], bias: f32[...] }, f32[...]))
grad(siamese_loss) :: <Siamese> (f32[...], f32[...]) => … pure
encode :: <encoder: Encoder> (x: f32[...]) => f32[...] pure
```

对照同一份产物里的 `encode`／`siamese_loss`／`train`／`grad(siamese_loss)`：它们都打印 `pure`，而
**`encode.pb`、`linear.pb`、`pair_loss.pb`、`encode.fwd`、`linear.fwd` 一个子句也没有**（`Generic`）。
按 effects plan 2.3 第 1 条，`Generic` 时取实际函数值的具体类型；那两步都取不到（我另加了日志确认
`liftClosureGraph` 拿到的 `published=false`，即闭包值与静态函数常量的类型**都没有子句**），
于是按 `anyOf` 全局处理 ⇒ `write unknown` ⇒ 与一切冲突 ⇒ 两条同区无序 ⇒ commit 拒。

**也就是说：这条红是「读法」的红，被 `inline` 的 splice 揭出来的。** 那一对节点是 splice 连同体
一起复制进 `train` 的，改名发生在 `std::cxx::inline` 自己的「把 splice 带进来的调用命名」那一轮，
不是 `std::cxx::devirtualize` 那一遍。`std::cxx::inline` 单独是绿的，是因为它不拼那个体（没有
`devirtualize` 先把调用改成可内联的直接调用，就没有 splice，也就没有 commit 问这一对）。

**这一遍的注释正好把这条红许诺掉了**（`src/passes/opt/inline/engine.cpp`，`devirtualizeStaticCallInDraft`
之前的说明）：

> … a differentiated body's two calls sit in one region and **neither writes**, so leaving them
> unnamed costs the graph the order completeness Analysis 4.1 asks for, **where naming them costs
> nothing**.

在这条用例上，命名恰好**不是**无代价：未命名时读的是值的类型（`Generic` ⇒ 保守 `unknown`），命名后
读的是被调用者函数体的足迹（同一个 `unknown`）——两边的 `unknown` 是同一种，但"neither writes"
这个前提在两边都不成立。也就是说，那句话里的"两个调用都不写"是这段代码**假定**的，而这条红说明
今天没有任何东西保证它：程序里没有一个类型说它们不写。

这条与 `2026-11-19-the-effect-of-a-call-through-a-function-value.cn.md` 是同一族：⑦ 那一笔只把
**被变换函数自己的**子句抄进了 `grad` 的结果类型（所以 `grad(siamese_loss)` 打印 `pure`），
**微分产生的每一张子图（`*.fwd` / `*.pb`）以及它们交出去的函数值，类型里仍然一个子句都没有**。
那张页 ④.2 列的第二个落点（"让值的类型在节点上取度量后的那一份"）在这条红上就是正解。

## ④ 我试过的、以及为什么不落

**试过（已回退，未提交）**：在 `liftClosureGraph` 里把被 lift 的图的子句带上
（`liftClosureGraph` 现在 `setFuncType(FunctionType::create(withTypes, normTypes, type->exitType()))`
把基图的子句整个丢掉，而同族的 `specializeGraphWithBindings` 会经 `setRenumberedClause` 把它按擦掉的
形参重编号保留）；再从闭包值／静态函数常量的类型取一个 `Concrete` 子句给它。

**读数：`published=false`——两个来源都是 `Generic`。** 程序里根本没有任何一个类型写着这两条调用是纯的，
所以"把类型里的子句搬到 lift 出来的图上"无处可搬。这一笔对读数无影响，已 `git checkout` 回退。

**没试、也不该试的**：

- 让 `std::cxx::inline` 不拼这个体、或让 `devirtualize` 不认这两条调用 —— 那是把红藏起来，红还在。
- 放宽 `commitViolation`：这一对是 splice **新造**的节点对（体被拼进 `train` 时连节点一起复制），
  `alreadyUnordered` 只对"图本来就有的节点对"成立，对复制品不成立，放宽它等于让 pass 交出它
  明知无序的图。

## ⑤ 缺口在哪（不在本 lane 能改的地方）

`modules/autodiff` 造 `encode.pb` / `encode.fwd` 这类图与函数值时，`FunctionType::create` 不带
`resources`；同族里 `modules/transforms/jvp.cpp#liftSignature` 是
`FunctionType::create(...)->withResources(function->resources())`。`encode` 自己是 `pure`，
它的前向／回传子图算的也是纯的梯度，所以正解是把被变换函数的子句抄进**每一张子图与每一个交出的
函数值**的类型（载 ⑦ 那一笔的做法扩到 `.fwd` / `.pb` 与闭包值），或者按 2.2 的口径让节点上的
"值的类型"取度量后的那一份。

**⇒ 这一条要动 `modules/autodiff`（当时是本轮红线），所以这一页先停在这里问；author 放行后的落地
见 ⑦。**

## ⑥ 本轮读数（`91f708389` + 本页，`-Plan feat/modules -Jobs 8`，`feat/trans` 单独一次）

- `feat/modules`：**2074 / pass 2057 / fail 3 / skip 7 / known 7**。三条红逐字：
  `siamese_shared_encoder.devirtualize_inline.nvm`（本页）、`array_sort_kernels.paths`（别人的）、
  `registry.reference_is_generated`。
- **`registry.reference_is_generated` 不是本 lane 带来的**：它读的是
  `docs/stdlib-reference.md` 与 `node scripts/stdlib-reference.mjs --write` 的产物是否一致，只需要
  `stdlib/signatures/*.cml` 与库源码、**不需要编译器**，所以本机就能量：
  `node scripts/stdlib-reference.mjs --verify-doc docs/stdlib-reference.md` 在 `xdev` 树上与
  主检出 `061011af6` 上**都**报 `is out of date`。它是 `origin/develop` 上已经存在的红
  （`docs/stdlib-reference.md` 最后一次由 `88f855ebb` 写过，之后 `7ba2a034b` 改了
  `stdlib/vjp.cml` 与 `stdlib/signatures/transforms.cml`）。
- `feat/trans`：**109 / pass 108 / fail 1**（`print_and_recompile`），与本 lane 开工前一行不差。

## ⑦ 已落（author 2026-11-19 答复本页后）：变换给每张图写出子句

author 的答复与本页的判定一致（"不是序被拆，是变换造出来的图类型里没有效应子句"），并给了三条细则与
一条界线："只抄被子句，不改任何判定规则"，"一处实现，不在读法里补"。落地：

- `modules/autodiff/pullback.cpp#makePlanTypes`：`*.pb` 与它交出的函数值的类型写 **`pure`**
  （`pureClause`）。依据 9.9：效应骨架整个进前向一半，反向一半只在正向记下的值上算转置规则，自己没有
  骨架。`plan.forwardValueType` 用的是同一个 `pullbackType`，所以 `(value, pullback)` 元组里那个
  函数值的类型也一并是 `pure`。
- `modules/autodiff/pullback.cpp#Engine::build`：`*.fwd` 图的类型原样带上被变换函数的子句——前向体
  就是函数自己的体多接一个聚合，形参表不变，不需要重编号。
- `PullbackPlan::sourceClause` 按**值**存（`std::optional`）：plan 活得比读它的 draft 久（第一版按
  指针存，`&clause` 指向 `planForClosure` 的局部量，量到 `std::bad_array_new_length` 与 OOM，已改）。
- `src/passes/opt/inline/engine.cpp`：那句「neither writes」改成引用这条规则（9.9 与
  `pullback.cpp#pureClause`），不再是假定；并写明它自己也有兜底——真有效应的反向体会在 commit 的
  `effectBeyondDeclaration` 那里被拒，而不是被这条注释放过。

读法一侧一行未动。逐字读数（同一次 `-Plan feat/modules -Jobs 8`）：

- `feat/modules`：**2074 / pass 2059 / fail 1 / skip 7 / known 7**，唯一的红是
  `array_sort_kernels.paths`（不是本 lane 的）。`siamese_shared_encoder.devirtualize_inline.nvm` 绿。
- 六计划与 `feat/trans` 不差：compile 2638/2622/0 · linear 869/864/0 · opt 1620/1620/0 ·
  parse 126/126/0 · para 104/104/0 · tools 70/70/0 · trans 109/108/1（`print_and_recompile`，开工前
  就红）。

**还没做的**：author 细则 ② 的"及转置产物"——`modules/transforms/split.cpp#split` 的
`halves.backward`、`transpose.cpp` 的转置产物、`vmap.cpp` 的函数值仍然不写字句（本 lane 的红不需要
它们，动了要另量 `feat/trans`、`feat/linear`）。这一条留给下一笔，写在这里免得丢。

## 附：复现

```
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <tree> -Plan feat/modules -Jobs 8
```

单条探针（本页读数用的形状，跑完即删）：`test/plans/feat/modules/` 下一份只含
`siamese_shared_encoder` 四条管线的 plan，`args` 分别为 `std::macro std::nvm`、
`… std::cxx::devirtualize std::nvm`、`… std::cxx::inline std::nvm`、
`… std::cxx::devirtualize std::cxx::inline std::nvm`。

## 默认建议（写死）

**默认建议**：按 ⑦ 已定的纪律**只抄被子句、不在读法里补**，把同一笔扩到 ⑦ 末尾点名的那三处 ——
`modules/transforms/split.cpp#split` 的 `halves.backward`、`modules/transforms/transpose.cpp` 的转置产物、
`modules/transforms/vmap.cpp` 交出的函数值，写法照 `modules/autodiff/pullback.cpp#makePlanTypes`
与 `#Engine::build`（`*.pb` 写 `pure`、`*.fwd` 原样带上被变换函数的子句）。
**判据**：`-Plan feat/trans,feat/linear -Jobs 8` 的 `total/pass/fail` 与改前逐条相同（`print_and_recompile`
之外零新红）、`-Plan feat/modules -Jobs 8` 的 2074 条不增红；并且 `git diff` 里**不出现**
`src/runtime/resources.cpp` 那一侧的任何一行 —— 读法一侧动了就是走了另一条路，不算这个默认。
