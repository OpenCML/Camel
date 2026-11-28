# task-415 A 类：12 条一元签名，「记录 vs 真库」的账

这一页是 `task-415` A 类（12 条一元算子：`abs exp log sqrt rsqrt sigmoid tanh relu gelu erf erfc neg`）
**记录与真库对不对得上**的唯一账。量具是 `camel-remote -Check ops-table`
（`scripts/check-operator-uses.mjs --dump`，走 `camel inspect --ops`），读数树 `dfdeff1a9e`。

## 一、量具与两读

```
operator-uses: 825 operator row(s) from test/cases/modules/stdlib/registry_all.cml
```

12 行在补 `semantics` 前后**逐行比**：`signature` / `purity` / `may_fault` / `clause_pure`
**全 12 行未变**，只有 `tanh` / `erf` / `erfc` / `gelu` 从"没有 `semantics`"变成有（见第七节）。

## 二、12 条逐条

「记录」列 = ops-table 的 `signature`（`OpDef::signature()` 由 `ParamSpec.kind` 与 `resultDoc` 拼成，
`src/stdlib/tensor/ops/op_def.cpp:97-140`；12 条都来自同一个 `unaryDef`，`kUnaryParams = {{"t", ParamKind::Tensor}}`，
`src/stdlib/tensor/ops/elementwise_ops.cpp:211,244-255,755-766`）。
「参考声明」列 = `src/stdlib/tensor/signatures/tensor.cml`（文件头自述：**for reference only**，
"C++ operators in `lib/tensor` are the one source of these types"，L4-11）。
「内核真值」列 = `src/stdlib/tensor/kernels/elementwise.cpp:617-620` 的 `unaryProducesFloat`
与 `src/stdlib/tensor/ops/elementwise_ops.cpp:188-201` 的 `unaryInfer`。

| # | 算子 | 记录（ops-table） | 参考声明 | 内核真值 | 判 |
|---|---|---|---|---|---|
| 1 | `abs` | `(t: Tensor) => float[...]` | `(t: float[...]) => float[...]` L61 | **元素类型保留**（`Abs` 在 `unaryProducesFloat` 里返 false） | **对不上** |
| 2 | `__neg__` | `(t: Tensor) => float[...]` | **该文件无条目** | 元素类型保留；且前端对 `int[]` 的 `-` 不接受 | **对不上（两处）** |
| 3 | `relu` | `(t: Tensor) => float[...]` | L97 同形 | **元素类型保留**（`Relu` 同为 false） | **对不上** |
| 4 | `exp` | `(t: Tensor) => float[...]` | L66 | 产出浮点：f64 进 f64 出，其余 f32 | 一致 |
| 5 | `log` | 同上 | L72 | 同 4 | 一致 |
| 6 | `sqrt` | 同上 | L77 | 同 4 | 一致 |
| 7 | `rsqrt` | 同上 | L82 | 同 4 | 一致 |
| 8 | `sigmoid` | 同上 | L87 | 同 4 | 一致 |
| 9 | `tanh` | 同上 | L92 | 同 4 | 一致 |
| 10 | `gelu` | 同上 | L102 | 同 4 | 一致 |
| 11 | `erf` | 同上 | L107 | 同 4 | 一致 |
| 12 | `erfc` | 同上 | L112 | 同 4 | 一致 |

**参数那一列（12 条共同）**：记录写 `Tensor`（`kindText` 的 `ParamKind::Tensor` → `"Tensor"`，
`op_def.cpp:99-100`），参考声明写 `float[...]`；但前端**接受** `int[]`（见第三节）⇒ 记录对、参考声明窄。
这正是 `task-415` 方案页里 A 类那句"不是形状问题，是参数类型"（`internal/collab/task-415-computed-signatures.md:46`）。

## 三、实测（不是只读码）

用类型绑定把编译器的结论逼出来（探针用例，`std::nvm`，`[PASS]`，跑完即删、树里不留）：

```camel
let xs: int[] = [-3, 4]
let ai: int[] = abs(xs)      // 编过 ⇒ abs(int[]) : int[]
let af: f32[] = exp(xs)      // 编过 ⇒ exp(int[]) : f32[]
```

⇒ `abs(int[])` 的结果是 **`int[]`**，不是记录与参考声明都写的 `float[...]`（`relu` 同一条
`unaryProducesFloat` 分支，未单测）；`exp(int[])` 是 `f32[]`，与 `float[...]` 相容。
两行同时说明：**参数是 `Tensor`（任何数值数组）这一点，记录比参考声明准**。

## 四、`__neg__`：记录描述的入口在语言里到不了

同一探针里 `-xs`（`xs: int[]`）在第 9 行报
`NoMatchingFunction: <> (int[])`，列出的候选只有标量
`<:op/neg_i> (i32)=>(i32)`、`<:op/neg_l> (int)=>(int)`、`<:op/neg_f>`… 一族。
即 `tensor:__neg__` 记录里写的 `(t: Tensor)` 参数，前端**没有任何路径**走到；参考声明里也没有它的条目
（`git grep __neg__ -- src/stdlib/tensor/signatures` = 0 命中）。

## 五、为什么三条对不上、`--verify-ops` 还绿（重要）

`feat/modules/stdlib_registry` 的 `registry.signatures_match` / `registry.result_types_match` /
`registry.parameter_types_match` 三条跑的都是 `scripts/stdlib-reference.mjs --verify-ops`。
它对"读不懂的签名"有一层**豁免类**，命中的一侧不进失败、只进"读不懂"清单：

- 结果类型那一半：`scripts/stdlib-reference.mjs:4182-4188`，类名在 **`:4184`**
  —— `a type variable, a named type or a sentence on one side, or an element type the operator takes from its argument`；
- 参数类型那一半：`scripts/stdlib-reference.mjs:4189-4195`，类名在 **`:4191`**
  —— `a parameter that is a function type, a module's own type, a nameless pack or a bare type variable on one side`；
- 两侧的 `EXCEPTIONS` 表在 **`:2876`**，注释里点明 `T[...]` 这种"取自实参的元素类型"属于此类；
- 清单是**两向比**的（`exceptionProblems(found)` 在 **`:4200`**；注释 `:4196-4199`）：
  某条**新进**某个豁免类、或**离开**，都会失败 —— 所以"把三条挪进豁免类"必须同时改表，不能只在声明侧改字。
- 绿的那三行文本在 `:4241`（`the declarations agree with the N registered operator overloads`）、
  `:4244`（`the declared result type agrees ... for M of them`）、`:4245`（`the declared parameter types fit ... for P of them`）；
  `EXCEPTIONS_SHOWN`（`12`）在 `:2357`。

**为什么这不矛盾**：`float[...]` 在记录与参考声明**两边都写着**，所以 `result_types_match`
（它比的是"声明的结果类型"与"算子自己的"）看到的是两边相等，判据当然绿；错的是两边**共同的断言**
（内核按元素类型保留），而不是任一侧的单方面笔误。⇒ 这条不是"量具坏了"，是"量具只比两侧相不相等"。

## 六、为什么不改（决定的边界）

要让它对上，必须**三处一起改**：
1. 记录 `resultDoc`（`unaryDef` 里所有 12 条共用一个 `"float[...]"`，`elementwise_ops.cpp:250`）；
2. 参考声明（`signatures/tensor.cml` L61/L97 及 `__neg__` 的缺失条目）——写成
   `abs::<T>(t: T[...]) => T[...] when is_numeric::<T>()` 这类，才是"能从实参读出元素类型"的写法；
3. `stdlib-reference.mjs#EXCEPTIONS`（把这三条登记进"元素类型取自实参"类，否则两向比会失败），
   并且 `docs/stdlib-reference.md` 要重生成（`registry.reference_is_generated` 钉着）。

第 3 处是**检查表/判据**，按铁律 18 归决策者，不由车道改。故本轮**不动这三条**，
登记进 `task-415` 第二阶段的类型函数（元素类型由 `T` 算出）时一起做。

## 七、本轮落地的那一笔（4 条 `semantics`）

`erf`/`erfc`/`gelu`/`tanh` 原来**没有** semantics 记录（12 行里唯四），而参考声明给它们写了算法说明。
补在 `src/stdlib/tensor/ops/semantics.cpp`（带字面名字的条目 **32 → 36**，只加 4 条、不动既有条目）：
`bounded(4.0f, …)` —— 4 ulp 是 `docs/design/op-semantics` 给这一族的目标界，也与
`src/stdlib/math/semantics.cpp:194-196` 给 `math:tanh`/`math:erf`/`math:erfc` 的界一致；
`gelu` 的算法按内核实际算式写（tanh 形式，`kernels/elementwise.cpp:555-558`），
不写成参考声明里那句"its normative definition in Camel"（见第八节的观察）。

## 八、两条留给决策者的观察（不在本轮改动范围）

1. `gelu` 的参考声明（`signatures/tensor.cml:99-102`）写"computed as its normative definition in Camel"，
   而内核是 **tanh 近似**（`0.5·x·(1+tanh(√(2/π)·(x+0.044715x³)))`，`kernels/elementwise.cpp:555-558`，
   f64 路径同式 `:555-559`）。参考声明那句话与内核不一致，属 A 类之外的一处文档缺陷。
2. `sigmoid` 的 `semantics` 是**只有 definition 的那种**（`numeric: "opaque"`, `algorithm: ""`,
   `definition: tensor_definitions::sigmoid(norm 0)`，来自 `src/stdlib/tensor/ops/catalog.cpp:80`
   的 `declareDefinition`），不是数值语义；补数值语义是另一条（若要做，与第七节同一张表再加一条）。

## 九、读数

两读都在**提交后的干净树**上取（`dirty=0`，`BUILD_EXIT=0`）。读数是在 `34fd6a5b76` 上取的
（`c370607b4a` 之后、本页定稿之前；与包含本页的提交**只差本页文字，源码与构建输入相同**）：

| 读数 | 原文 |
|---|---|
| registry（终读） | `[provenance] tip 34fd6a5b7691b77741490323af87b3d5032d39a6 … tier fast`；`[SUMMARY] total: 14, pass: 14, fail: 0` |
| ops-table（同一树） | `operator-uses: 825 operator row(s) from test/cases/modules/stdlib/registry_all.cml`；12 行 `signature`/`purity`/`may_fault`/`clause_pure` **12/12 未变**；4 条新语义 key `tensor:{tanh,erf,erfc,gelu}@1.0`，`numeric: bounded`；其余 8 行语义不变 |
| 75 表 | `internal/reports/2026-11-28-promotion-75-reds.tsv` 141 行；本轮无新红（甲 ∅ / 表外红 0） |

提交：`c370607b4a`（semantics 32 → 36 条，`+21` 行，只此一文件）；本页紧随其后一笔。