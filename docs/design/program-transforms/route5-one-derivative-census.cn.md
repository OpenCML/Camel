# 路线五：把求导收成一套，还差什么（2026-11-25 读数）

- 状态：**只读盘点**。本页不改语义、不改代码，只数。规范仍是
  [`docs/design-principles.cn.md`](../../design-principles.cn.md)；排期是
  [`docs/roadmap.cn.md`](../../roadmap.cn.md) §0 第 1 条；完成标志是
  [`docs/design/architecture/README.cn.md`](../architecture/README.cn.md) §5 路线五的「求导只剩一套」。
- 树：`D:\Projects\Camel-wt\refgen`，分支 `lane/route5-one-autodiff`，基线 `d6503a197c`（= `develop`）。
- 先说一件会误导人的事：排期与架构文件里写的 `modules/autodiff`、`stdlib/` **在这个树里都不存在**
  （`git ls-files 'modules/*'` 与 `'stdlib/*'` 各 0 个文件）。求导引擎现在住在 `src/stdlib/autodiff/`，
  库的求导住在 `src/stdlib/vjp.cml` + `src/stdlib/rulesets/`。本页一律用**实际路径**，本文里的
  "`modules/autodiff`" 一律读作 "`src/stdlib/autodiff/`"。这一点本身是一条待修的文档偏差，不是我的措辞。

## 0. 五个读数，一句话一个

| 读数 | 值 | 出处 |
|---|---|---|
| 两套的实现面 | 库侧 5 个文件 / **3138 行**；引擎侧 10 个文件 / **6178 行**（含签名与 Camel 面） | §1 |
| 库内调用 `grad` 的点 | **0 处**（`src/` 里 5 行命中全是诊断字符串，不是调用） | §2 a |
| 测试调用 `grad`/`vjp` 的行 / 文件 | **897 行 / 414 个文件**（含注释共 2461 行 / 489 个文件） | §2 b |
| 求导家族的用例条目 | **517 条**：引擎入口 246、库入口 220、两条腿 8、其它 43 | §5 |
| 两份求导表的差 | **58 键两边都答，6 键只有库，46 键只有引擎**（全部已登记豁免，0 欠账） | §4、§5 |

最后一行是这一项的**关键读数**：引擎那 46 个键里，**31 个是机器算子**（`:tangent/*`、`:struct/*`、
比较、离散转换、`access`、`cast`——程序写不出来，也不该由规则集回答），**15 个是真的要补的律**
（`nn` 7 个、`math:erf/erfc` 4 个、`pow`、`slice_sub`、`sqrt_d`、`idx_take`）。
反方向那 6 个"库有引擎无"不在这笔账里：4 个是 `*_stated` 轴家族（引擎走普通算子），2 个是
`log_d`/`pow_d`（律只写了反向那一半）。

```powershell
$rows = Get-Content scripts/vjp-parity-exemptions.tsv | Where-Object { $_ -and -not $_.StartsWith('#') }
$rows | ForEach-Object { ($_ -split "`t")[1] } | Group-Object   # engine-autodiff 31 / lane-rulesets 15
                                                                # lane-axistated 4 / lane-missinglaws 2 (=6 行库独有)
```

---

## 1. 两套各自的文件清单

命令（行数用物理行，`(Get-Content 文件).Count`）：

```powershell
$files = @('src/stdlib/vjp.cml','src/stdlib/rulesets/jvp.cml','src/stdlib/rulesets/transpose.cml',
  'src/stdlib/autodiff.cml','src/stdlib/autodiff/signatures/autodiff.cml',
  'src/stdlib/autodiff/module.cpp','src/stdlib/autodiff/module.h',
  'src/stdlib/autodiff/operators.cpp','src/stdlib/autodiff/operators.h',
  'src/stdlib/autodiff/pullback.cpp','src/stdlib/autodiff/pullback.h',
  'src/stdlib/autodiff/rules.cpp','src/stdlib/autodiff/rules.h','src/stdlib/autodiff/CMakeLists.txt')
$files | ForEach-Object { "{0,-46} {1,6}" -f $_, (Get-Content $_).Count }
```

**Camel 版（库的 `grad`，`src/stdlib/vjp.cml` + 它的规则集）**

| 路径 | 行数 | 归属 | 管什么 |
|---|---:|---|---|
| `src/stdlib/vjp.cml` | 245 | 库（门面） | `vjp`、`grad`、`has_aux` 三个 `#macro`，全是组合：`split(jvp(first_with_as_param(f)))` 然后 `transpose` |
| `src/stdlib/rulesets/jvp.cml` | 1568 | 库（规则表·前向） | 75 条 `export rule` + 14 个导出函数（lift、zero_tangent 等） |
| `src/stdlib/rulesets/transpose.cml` | 1276 | 库（规则表·反向） | 48 条 `export rule`（每条答一个名字的余切） |
| `src/stdlib/rulesets/probe_batch.cml` | 26 | 库（探针） | `vmap` 探针用的最小规则集 |
| `src/stdlib/rulesets/probe_missing.cml` | 23 | 库（探针） | 缺规则时的读数探针 |
| 小计 | **3138** | | |

**C++ 版（引擎，`src/stdlib/autodiff/`）**

| 路径 | 行数 | 归属 | 管什么 |
|---|---:|---|---|
| `src/stdlib/autodiff/pullback.cpp` | 5326 | 引擎（主体） | 前向改写、反向发射、循环磁带、`findRule` 按 URI 查规则；16 处 `failAt`/`throwRuntimeFault` |
| `src/stdlib/autodiff/operators.cpp` | 290 | 引擎（算子） | 注册 `autodiff:grad`、`grad_aux`、`with_grad`、`with_grad_aux`、`vjp` 标签读取；`GradResolver` 算签名；`NotDifferentiable` 由这里发出 |
| `src/stdlib/autodiff/operators.h` | 37 | 引擎 | |
| `src/stdlib/autodiff/pullback.h` | 84 | 引擎 | |
| `src/stdlib/autodiff/module.cpp` / `.h` | 66 / 19 | 引擎（登记） | 模块与算子表的入口 |
| `src/stdlib/autodiff/rules.cpp` / `.h` | 42 / 40 | 引擎（`#vjp(rule)` 读者） | 用户自定义导数：读 `graph->ruleGraphs()` |
| `src/stdlib/autodiff/CMakeLists.txt` | 9 | 引擎（构建） | |
| `src/stdlib/autodiff/signatures/autodiff.cml` | 75 | 引擎（声明） | 不编译，供解析器对照与 `docs/stdlib-reference.md` 生成：`Gradient<F>` / `GradientAndAux<F>` / `ValueAndGradient<F>` / `ValueAndGradientAndAux<F>` |
| `src/stdlib/autodiff.cml` | 190 | 引擎（Camel 面，**但库要用**） | `stop_gradient`，以及 `add_tangent`/`mul_cotangent` 这一批"局部律的数学"。`rulesets/jvp.cml`、`rulesets/transpose.cml` 各 `import ... from autodiff` 拿它们 |
| 小计 | **6178** | | |

**C++ 规则表本身不在 `autodiff/` 里。** 它是 `DerivativeRegistry` 的注册，散在 10 个文件里
（`setVjp(defs, "<name>", &fn)` 72 处 + `setRule(uri, fn)` 的调用点）：

```powershell
git grep -c 'setVjp(' -- 'src/*'   # 72 处注册（含 vjp.h 的定义行）
git grep -n 'setRule(' -- 'src/*'  # derivative.cpp 27 / math/module.cpp 4 / autodiff/operators.cpp 1
node scripts/vjp-parity.mjs        # 权威读数：122 个注册，压成 104 个键
```

| 注册点 | 干什么 |
|---|---|
| `src/stdlib/std/builtin/operators/derivative.cpp` | 核心算术、转换、数组写、`tangent` 算子（27 处 `setRule`） |
| `src/stdlib/tensor/ops/elementwise_ops.cpp` | 25 处 `setVjp`（`add`/`mul`/`exp`/`relu`/`gelu`…） |
| `src/stdlib/tensor/ops/reduction_ops.cpp` | 17 处 |
| `src/stdlib/tensor/ops/layout_ops.cpp` | 11 处 |
| `src/stdlib/tensor/ops/gradient_ops.cpp` | 9 处 |
| `src/stdlib/tensor/ops/linalg_ops.cpp` | 3 处 |
| `src/stdlib/tensor/ops/registry.cpp` | 把 `setVjp(defs, "<name>")` 变成 `tensor:<name>` |
| `src/stdlib/nn/tensor_ops.cpp` | 7 处（`conv2d`、`batch_norm_train`、两个 pool、`embedding`、两个 cross-entropy） |
| `src/stdlib/math/module.cpp` | 4 处（`erf`/`erfc` 的 `_d`/`_f`） |
| `src/stdlib/autodiff/operators.cpp` | 1 处 |

**对照脚本**

| 路径 | 行数 | 干什么 |
|---|---:|---|
| `scripts/vjp-parity.mjs` | 985 | 主判定：两份表各压成 `head@arity` 键，求差，逐条对清单；`--tables` / `--json` 出三张表 |
| `scripts/vjp-parity-exemptions.tsv` | 145（**52 条数据行**） | 每一处差一行：标记、主人、理由 |
| `test/tools/check-vjp-parity.mjs` | 206 | 判定的自检（三道读数：真树 0 new 且 stale=0；加一条没人答的律必须 exit 1；登记后 exit 0） |
| `test/tools/check-vjp-ruleset.mjs` | 204 | 规则集自身的自检 |
| `scripts/autodiff-migration-ledger.mjs` | 574 | 逐案把 `from autodiff` 里的 `vjp`/`grad`/`with_grad` 改到 `from vjp`，按各案计划期望的退出码判"这个案今天能不能走库" |
| `scripts/law-cotangent-ports.mjs` | 1024 | 律的端口（读哪几个值）与两侧写法的一致性 |
| `benchmarks/tools/grad_referee.py` | 729 | 基准上的数值裁判 |

---

## 2. `grad` 的用户：三类，不许混

### a) 库内调用（`src/`、`modules/`、`stdlib/`）：**0 处**

```powershell
git grep -n -E '\b(grad|with_grad|vjp)\s*\(' -- 'src/*' |
  Where-Object { $t=($_ -split ':',3)[2]; $t=$t.TrimStart();
                 -not ($t.StartsWith('//')-or $t.StartsWith('*')-or $t.StartsWith('/*')) }
# 5 行命中 / 4 个文件：runtime.cpp:261、registry.tsv:516、operators.cpp:84,85、pullback.cpp:5101
# 这 5 行全在诊断字符串里（"write `grad(f)` …"），没有一个是真的调用点
```

所以「剩下的引擎 `grad` 用户先迁」这句话，**在库内没有对象**：库自己（`nn`、`optim`、`tile`…）
一次也没调用 `grad`/`with_grad`/`vjp`。这让路线五的难点整体从"库内迁移"挪到了"测试与示例"。

但有两条**依赖边**，删引擎时会一起断（见 §4 第 1 步）：

```powershell
git grep -n 'from autodiff' -- 'src/*'
# src/stdlib/rulesets/jvp.cml:22      import { add_tangent, div_tangent, mul_tangent, neg_tangent, stop_gradient, sub_tangent } from autodiff
# src/stdlib/rulesets/transpose.cml:40 ... } from autodiff
```

即：库的数学律（`add_tangent` 这一批，190 行的 `src/stdlib/autodiff.cml`）今天是**挂在引擎文件上的**，
而库的规则集是它唯一的读者。删 `src/stdlib/autodiff/` 之前必须先把它搬出引擎的归属。

### b) 测试自己在用（`test/`）：**897 行 / 414 个文件**

```powershell
git grep -n -E '\b(grad|with_grad|vjp)\s*\(' -- 'test/*' |
  Where-Object { $t=($_ -split ':',3)[2]; $t=$t.TrimStart();
                 -not ($t.StartsWith('//')-or $t.StartsWith('*')-or $t.StartsWith('/*')) }
# 897 行 / 414 文件
git grep -n -E '\b(grad|with_grad|vjp)\b' -- 'test/*' | Measure-Object   # 含注释 2461 行 / 489 文件
```

这就是「若不迁，两套都得留」的那一堆。落到**求导家族的 421 个用例文件**上（§5 的口径）：
**127 个只走引擎入口**（`test/cases/modules/autodiff/` 145 个文件里除库腿之外的，加
`test/cases/compile/transforms/` 里的引擎腿）、**236 个只走库入口**、7 个两条腿都走、
51 个只有标签/辅助（`#vjp(rule)` 参数用例、探针）。按计划条目数则是 §5 那张表（517 条）。

### c) 只是文档/注释提到

```powershell
git grep -n -E '\b(grad|with_grad|vjp|autodiff)\b' -- '*.md'   # 2812 行 / 417 文件
#   其中 docs/   471 行 / 66 文件
#        internal/ 2209 行（协调者的账，不算源码改动面）
git grep -n -E '\b(grad|with_grad|vjp)\b' -- 'src/*'          # 328 行，其中注释 239 行
git grep -n -E '\b(grad|with_grad|vjp)\b' -- 'test/*'         # 2461 行，其中注释 404 行
```

**另有一类必须单列，不能算进 a**：仓库里的**程序**（不是库、不是测试）用 `grad` 的还有

```powershell
foreach($d in 'examples','showcase','benchmarks'){
  git grep -n -E '\b(grad|with_grad|vjp)\s*\(' -- "$d/*" } # examples 8 行/8 文件、showcase 65/40、benchmarks 81/37
```

合计 154 行 / 85 个文件。这些是"用户"，删引擎后它们要么改写成库的写法，要么被明确接受为历史件。

---

## 3. 两套的接口差：11 处，逐处列

1. **名字的位数不同。** 引擎一个模块 `autodiff` 出四个算子：`grad`、`grad(…, has_aux: true)`、
   `with_grad`、`with_grad(…, has_aux: true)`（算子名 `autodiff:grad`、`autodiff:grad_aux`、
   `autodiff:with_grad`、`autodiff:with_grad_aux`，登记在
   `src/stdlib/autodiff/operators.cpp#gradKernel` 与同文件的算子名表）。库一个模块 `vjp` 出三个名字：
   `vjp`、`grad`、`has_aux`（`src/stdlib/vjp.cml` 的三个 `export func`）。**库没有 `with_grad` 这个名字**。
2. **同一件事的返回形状不同。** 引擎 `with_grad(f)` 答 `(value, gradient)`；库要用 `vjp(f)` 拿
   `(value, pullback)` 再 `pullback(1.0).0` 才得到 gradient，或者用 `has_aux(f)` 拿
   `((value, aux), gradient)`。三种写法、三种元数。
3. **`has_aux` 的位置不同。** 引擎是 `grad`/`with_grad` 的**第二个参数**（`has_aux: bool`，只许常量
   `true`，`false` 被拒并告诉你改写 `grad(f)`，见 `src/stdlib/autodiff/operators.cpp#GradResolver`
   的 `explainRejection`）。库是**另一个名字** `has_aux(f)`，而且只声明了一种形状
   `(w: float) => (float, float)`（`src/stdlib/vjp.cml#has_aux` 的注释自己写明了"库还没有
   参数列表是 pack 的声明"）。
4. **结果类型名不同，但是同一个类型。** 引擎的声明写 `Gradient<F>`（`src/stdlib/autodiff/signatures/autodiff.cml#grad`），
   库的声明写 `GradientOf<F>`（`src/stdlib/vjp.cml#grad`），两者在 `src/stdlib/types.cml#Gradient` 与
   `src/stdlib/types.cml#GradientOf` 里是**逐字相同的定义**
   （`<...WithOf<F>>(...ParamsOf<F>) => Tangent<FirstOf<WithOf<F>>>`）。文件里自己承认"两个名字一个类型
   是读者要学的一种拼法，合并没在这里做"。
5. **`vjp` 这个词在两套里不是一个东西。** 引擎侧 `vjp` 是**标签** `#vjp(rule)`（`src/stdlib/prelude.cml#vjp`，
   由 `src/stdlib/autodiff/rules.cpp#ruleOf` 读 `graph->ruleGraphs()`）；库侧 `vjp` 是**函数**
   （`src/stdlib/vjp.cml#vjp`）。同名的两种意义共存，靠的是"标签用 `#` 写"这条规则。
6. **`vjp` 微分的是谁不同。** 库的 `vjp(f)` 微分的是一组**普通参数**（`src/stdlib/types.cml#Pullback`
   一个参数一个余切）；引擎的 `grad`/`with_grad` 微分的是**第一个 `with` 参数**（原则 9.9）。
   库的 `grad` 靠 `first_with_as_param` 把 weight 搬过去再搬回来，才和引擎"叫法一样"。
7. **诊断文本与诊断 id 不同。** 引擎发出的拒绝是**注册过的语义诊断**
   `SemanticDiag::NotDifferentiable`（`0x000150`，`src/kernel/diagnostics/registry.tsv#NotDifferentiable`，
   报文是一整段"怎么改写"的建议），由 `src/stdlib/autodiff/operators.cpp#GradResolver` 抛出；
   此外 `src/stdlib/autodiff/pullback.cpp` 里另有 16 处 `failAt`/`throwRuntimeFault`。
   库路线没有这个 id：它的拒绝来自 `transforms` 模块，**至少 20 处**不同文本，例如
   `transforms: first_with_as_param needs a function with a first `with` parameter that …`
   （`src/stdlib/transforms/first_with_as_param_operator.cpp#firstWithAsParamSignature`）、
   `transforms: jvp has no forward rule for `{}` ({})`（`src/stdlib/transforms/jvp_operator.cpp#JvpResolver`）、
   `transforms: split has no rule for this function`（`src/stdlib/transforms/split_operator.cpp#splitKernel`）、
   `transforms: transpose has no transpose for this function: …`
   （`src/stdlib/transforms/transpose_operator.cpp#refusalSentence`）。同一个程序写错，
   两套给的是**两句不同的英文和一个有的 id**。
8. **谁能读到用户的自定义律不同。** `#vjp(rule)` 只有引擎读（`src/stdlib/autodiff/rules.cpp#ruleOf`）。
   `src/stdlib/transforms/` 里没有任何 `ruleGraphs`/`rule_of`/`kGraphRefRule` 的读取
   （`git grep -nE 'rule_of|ruleGraphs|kGraphRefRule' -- 'src/stdlib/transforms/*'` 只命中 `vmap.cpp` 的
   `ruleOfUri`，那是按 URI 查规则集，不是读标签）。库路线的标签是 `#transpose(rule)`/`#jvp(rule)`。
9. **二阶。** 引擎侧 `second_order*.cml` 一排（`second_order`、`second_order_nest`、
   `second_order_array_*`…）；库侧 `grad(grad(f))` 能建起来但**答不出 2.0**，
   `src/stdlib/vjp.cml#grad` 自己写着停在哪一句（`transforms: jvp has no forward transform for this function`）。
10. **运行时 vs 宏时。** 引擎两种都有测试（`scalar_grad.runtime.nvm` 与 `scalar_grad.macro.nvm`
    成对出现）；库的 `vjp`/`grad` 是 `#macro`，而 `feat/compile/transforms` 计划里带 `vjp` 的 **91 个条目**
    的 `args` 里**没有** `std::macro` ——它们靠 `${run_nvm}` 之类的变量跑。也就是说库这条路"不写宏展开时
    发生什么"没有一条用例逐字钉住；本轮 **没有**构造探针刺它，见 §6。
11. **类型层的两个名字。** 除第 4 条外，`ValueAndGradient<F>` / `ValueAndGradientAndAux<F>` /
    `GradientAndAux<F>` 三个类型只有引擎的声明用，库里没有对应拼法（库的 `has_aux` 直接写死
    `(w: float) => ((float, float), float)`）。

（第 10 条的命令：`Select-String -Path test/plans/feat/compile/transforms.plan.toml -Pattern 'vjp' -AllMatches`
之后再筛 `args` 里没有 `std::macro` 的块，得 91。这是"块级"读数，不是逐字的，见 §6。）

---

## 4. 还缺哪几步：按依赖排，标出可并行

今天 `node scripts/vjp-parity.mjs` 的原样一行（在 `d6503a197c` 上实跑）：

```text
PASS the two derivative tables differ only where the list says so: 155 law(s) filed under 64 key(s),
122 engine registration(s) under 104 key(s), 58 key(s) both answer, 6 library-only and 46 engine-only
exempt, 0 new, stale=0
```

对照 2026-11-14 的基线（`vjp-parity-baseline.cn.md`：103 律 / 54 键 / 48 两边 / 58 差），
**律 +52、两边都答的键 +10、引擎独有的键 −6**。方向是对的，剩下的账是 46。

| # | 步骤 | 依赖 | 能不能并行 | 为什么在这一步 |
|---|---|---|---|---|
| 1 | 把 `src/stdlib/autodiff.cml`（`stop_gradient` + 局部律的数学，190 行）从引擎的归属里搬出来，放到库侧（`rulesets/` 旁边或自己的 `laws` 模块），改 `src/stdlib/rulesets/jvp.cml` 与 `src/stdlib/rulesets/transpose.cml` 里各自那条 `import … from autodiff` | 无 | **可**，与 2、3 并行 | 今天它是引擎目录里唯一被库 `import` 的文件；不搬，删引擎必删库的数学 |
| 2 | 合并 `Gradient<F>` / `GradientOf<F>` 两个名字（`types.cml`），以及决定 `ValueAndGradient*` 家族归谁 | 无 | **可**，与 1、3 并行 | 一个类型两个名字是读者要学的一种拼法；引擎一删，`Gradient<F>` 就没人写了 |
| 3 | 给 15 个"引擎有、库无"的律补 `jvp_*` / `*_t`：`nn` 7（`conv2d`、`batch_norm_train`、`avg_pool2d`、`max_pool2d`、`embedding`、`softmax_cross_entropy`、`softmax_cross_entropy_labels`）、`math` 4（`erf_d`/`erf_f`/`erfc_d`/`erfc_f`），以及 `pow`、`slice_sub`、`sqrt_d`、`idx_take` | 无（但 `nn` 那 7 个要求规则集 `import nn`，见豁免理由原文"the rulesets declare and import nothing from `nn`"） | **可**，与 1、2 并行；内部按模块再并行 | 这 15 个键是"删引擎就丢"的能力。另外 31 个（`:tangent/*`、`:struct/*`、比较、离散转换、`access`、`cast`）是机器算子，**不搬**，也不该搬 |
| 4 | 给 `#vjp(rule)` 在库路线找一个读者（或者明确宣布这条标签随引擎一起消失，并把 `custom_rule.cml`、`transform_rules.cml` 一排测试按新语义改写） | 1（律库要在库侧） | 与 3 并行 | 今天用户自定义导数只有引擎读得到；这是**语言能力**，不是实现细节，必须做决定 |
| 5 | 把家族用例文件里只走引擎入口的 127 个迁到库入口（先把 `scripts/autodiff-migration-ledger.mjs` 的读数往上推） | 3、4 | 与 3 的后半并行，但**必须在 3 之后** | 迁移的判据是那个脚本：现在 `test/cases/tools/autodiff_migration_ledger/baseline.json` 记的是 **23 个案**能在库上跑（145 个案里的 23） |
| 6 | 补齐库路线缺的形状：`has_aux` 的通用声明（pack 参数）、`with_grad` 的库拼法、二阶 | 5 | 与 4 之后可并行 | 这决定了"迁过去"是不是"降级" |
| 7 | 删 `src/stdlib/autodiff/`（10 个文件 6178 行）、`src/stdlib/autodiff/signatures/autodiff.cml`、引擎侧 10 个文件里的 122 个注册、以及 `vjp-parity` 那套对照脚本（清单、自检、迁移台账） | 5、6 **且** 3 清空 | 不可并行，这是最后一步 | 这就是路线五的"完成" |

**硬前置（不做完就不能删 C++ 版）是第 3 步。** 理由不是风格：46 个引擎独有的键里，15 个是程序写得出来
的算子（`conv2d`、`erf`、`pow`…）。引擎一删，这 15 个算子就再也导数不出来了，
而 §5 的 517 条用例里有一批正是钉在这些算子上的。第 3 步清零之后，`vjp-parity` 的
"engine-only" 一栏才可能只剩机器算子（那 31 个可以随引擎一起消失，它们本来就"只有规则建得出"）。

**第 5 步是量的门槛，第 3 步是质的门槛。** 两者都过才可以谈第 7 步。

---

## 5. 两个数

### 5.1 求导家族的用例：**517 条**，引擎入口 246 / 库入口 220

```powershell
# 家族 = 14 个计划（下面列出的这些）
$fam = @('test\plans\feat\modules\autodiff.plan.toml','...\autodiff_inspection','...\autodiff_value_with',
         '...\jax_alignment','...\remat','...\tools\autodiff_migration_ledger',
         '...\compile\transforms','...\compile\broadcast_product_gradient',
         '...\compile\transpose_array_product','...\compile\transpose_product_fixed_shape',
         '...\transforms\compositions','...\transforms\nested','...\transforms\split','...\transforms\floatgate')
Select-String -Path $fam -Pattern '^\s*\[\[tests\]\]' | Measure-Object -Line      # 517
```

逐计划的条目数：

| 计划 | 条目 | | 计划 | 条目 |
|---|---:|---|---|---:|
| `feat/modules/autodiff` | 234 | | `feat/compile/transforms` | 191 |
| `feat/modules/autodiff_inspection` | 14 | | `feat/transforms/split` | 23 |
| `feat/modules/autodiff_value_with` | 7 | | `feat/transforms/compositions` | 10 |
| `feat/modules/jax_alignment` | 14 | | `feat/transforms/nested` | 10 |
| `feat/modules/remat` | 3 | | `feat/transforms/floatgate` | 5 |
| `feat/tools/autodiff_migration_ledger` | 1 | | `feat/compile/broadcast_product_gradient` | 2 |
| `feat/compile/transpose_array_product` | 2 | | `feat/compile/transpose_product_fixed_shape` | 1 |

按**用例文件走哪个入口**分类（脚本见下）：**引擎入口 246 条、库入口 220 条、两条腿都走 8 条、
其余 43 条**（工具脚本 `.mjs`、`#vjp(rule)` 标签用例、以及 `case` 不是 `.cml` 的条目）。

分类规则（可复现，不靠名字猜）：
- **引擎入口**：用例正文的非注释行里有 `grad(` 或 `with_grad(`，且没有 `from vjp`；
- **库入口**：有 `vjp(` 或 `from vjp`，或有 `from transforms` 且调了 `transpose(`/`jvp(`/`split(`/`first_with_as_param(`；
- 两者都有 → 两条腿；都没有 → 其它。

```powershell
# 逐条：把每个 [[tests]] 块的 case 路径解析出来，读那个 .cml 的非注释行，按上面三条判
# （完整脚本见本次车道的提交说明；统计口径就是这三条正则）
```

按**文件**算是 421 个家族用例文件：引擎入口 127、库入口 236、两条腿 7、其它 51。
条目数与文件数不等，是因为一个用例文件常被多条条目跑（`nvm`/`fvm`/`macro`/`opt` 各一条），
这也是最容易把"用例数"数错的地方：**条目是 517，文件是 421。**

### 5.2 当前 `develop`（`d6503a197c`）上求导家族的已知红：**57 行**

```powershell
git -C . ls-files | Select-String 'promotion-4'      # 0 行：2026-11-24-promotion-4-reds.tsv 在这棵树上不存在
Get-Content internal/reports/2026-11-24-promotion-3-reds.tsv |
  Where-Object { ($_ -split "`t")[0] -match 'autodiff|transforms|jax|gradient|transpose' } | Measure-Object -Line
# 57
```

| 计划 id | 红行数 |
|---|---:|
| `transforms` | 35 |
| `modules.autodiff` | 15 |
| `modules.jax_alignment` | 6 |
| `tools.autodiff_migration_ledger` | 1 |
| 合计 | **57** |

三个必须说清的口径：
- 指定的 `2026-11-24-promotion-4-reds.tsv` **不存在**（`promotion-2`、`promotion-3` 在，`promotion-4` 不在）。
  上表用的是 `promotion-3`（1082 行），它测的是 promotion-3 那次的闸门，**不是 `d6503a197c` 本身**。
- 按 `internal/reports/2026-11-24-develop-reds.cn.md` 的口径，这些行是"最近一次记录判它红"，
  第三列"在这个提交上还成立吗"多数是"未知"。所以 57 是**已知红的行数**，不是"今天还红 57 条"。
- 我没有跑测试（本轮纪律：不跑整层，只在需要时跑 `-Plan smoke` 探针；本轮不改源码，故未跑）。
  要一个"今天的"数，得在构建好的树上跑 `internal\tools\camel-remote.ps1 -Plan smoke -Jobs 8 -NoGpuWatch`，
  那是另一次读数。

---

## 6. 数不出来的，和为什么

| 想数的 | 结果 | 为什么数不出来 |
|---|---|---|
| `modules/autodiff` 的文件清单 | 数不出来 | 这个路径在 `d6503a197c` 上不存在（`git ls-files 'modules/*'` = 0）。排期文件用的是旧路径；实际位置 `src/stdlib/autodiff/` 已在 §1 列出 |
| 顶层 `stdlib/` 里 `grad` 的定义与 reg 注册处 | 数不出来 | 顶层 `stdlib/` 不存在（`git ls-files 'stdlib/*'` = 0）。`grad` 的两个定义在 `src/stdlib/vjp.cml#grad` 与 `src/stdlib/autodiff/signatures/autodiff.cml#grad`，注册在 `src/stdlib/autodiff/operators.cpp#gradKernel` |
| "两套各被多少**程序**（用户）调用" | 数不出来 | 只有仓库内的程序（examples/showcase/benchmarks，154 行 / 85 文件）；仓库外的用户没有读数 |
| 库入口是否有"运行时"（不展开宏）的路径 | 数不出来（只能数到 91 条用例的 `args` 里没有 `std::macro`） | 这是块级正则，不是逐字的语义判断；要真答案得构造一个只调 `vjp` 不跑 `std::macro` 的探针，本轮没做 |
| `vjp-parity` 的 46 个引擎独有键里，哪些**真的**会在用户程序里出现 | 数不出来 | 需要把每个键反查有没有用例调它；`--tables` 给的是键与注册点，不是调用次数 |

## 7. 最该先做的一步

**先做第 1 步：把 `src/stdlib/autodiff.cml`（`stop_gradient` + 那批 tangent/cotangent 数学）搬出引擎的归属。**
它只有 190 行、两条 import、零语义变化，却是"删引擎必删库的数学"这条硬耦合的唯一钉子；
它不搬，后面 15 条律的补齐（第 3 步）都得写在别人的文件里。
搬完立刻可以做第 2 步（合并 `Gradient<F>` / `GradientOf<F>`）与第 3 步的一部分，三者互不阻塞。