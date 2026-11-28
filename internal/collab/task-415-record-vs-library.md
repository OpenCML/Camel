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

两笔提交 rebase 到 `origin/integration@6304e319e3` 之后重取（`dirty=0`，`BUILD_EXIT=0`）：

| 读数 | 原文 |
|---|---|
| registry（终读） | `[provenance] tip 811c48f2cfdfff26413db6c9f27e4a1460508db8 … tier fast`；`[SUMMARY] total: 14, pass: 14, fail: 0` |
| ops-table（同一树） | `operator-uses: 825 operator row(s) from test/cases/modules/stdlib/registry_all.cml`；12 行 `signature`/`purity`/`may_fault`/`clause_pure` **12/12 未变**；4 条新语义 key `tensor:{tanh,erf,erfc,gelu}@1.0`，`numeric: bounded`；其余 8 行语义不变 |
| 75 表 | `internal/reports/2026-11-28-promotion-75-reds.tsv` 141 行；本轮无新红（甲 ∅ / 表外红 0） |

提交（rebase 后）：`e6b9f187fc`（semantics 32 → 36 条，`+21` 行，只此一文件）、`811c48f2cf`（本页）。

## 十、第二阶段的可执行改法清单（清单，不是改动）

**结论先说：不需要动检查表。** `EXCEPTIONS` 一条不加、一条不删、也不新增类；要动的是
**1 行记录 + 2 行参考声明 + 1 次文档重生成**。`__neg__` 只能改记录（它没有、也不可能有声明，见 10.4）。

### 10.1 记录（一行）

`src/stdlib/tensor/ops/elementwise_ops.cpp:250`：

```cpp
        .resultDoc = k::unaryProducesFloat(Op) ? "float[...]" : "T[...]",
```

依据就在下一行：L251 的 `.infer = unaryInfer(k::unaryProducesFloat(Op))` 用的是**同一个谓词**，
`unaryProducesFloat`（`src/stdlib/tensor/kernels/elementwise.cpp:617-620`）对 `Neg`/`Abs`/`Relu`
返 false（元素类型保留），其余返 true（产浮点）。改一行，12 条记录里**恰好 3 条**（`__neg__`/`abs`/`relu`）
的 `signature` 变 `(t: Tensor) => T[...]`，其余 9 条不动。

### 10.2 参考声明（两行）

`src/stdlib/tensor/signatures/tensor.cml`：

| 行 | 现在 | 改成 |
|---|---|---|
| L61 | `export func abs(t: float[...]) => float[...]` | `export func abs::<T>(t: T[...]) => T[...] when is_numeric::<T>()` |
| L97 | `export func relu(t: float[...]) => float[...]` | `export func relu::<T>(t: T[...]) => T[...] when is_numeric::<T>()` |

同文件的先例（都是 `::<T>` + `T[...]`，均绿）：L20/26/36 `zeros`/`ones`/`eye`、L181/204 `sum`/`mean`、
L246/257 `max`/`min`、L296 `mean_axis_stated`、L432 `load_npy`、L449 `from_bytes`；它们的记录同样写
`.resultDoc = "T[...]"`（`ops/creation_ops.cpp:120/129/146`、`ops/reduction_ops.cpp:828/838/919`、
`ops/layout_ops.cpp:849`）。

### 10.3 判据为什么不用动（读原文的结论）

判据文件 `scripts/stdlib-reference.mjs`：

- **结果类型两侧同写 `T[...]` ⇒ 直接"比较过且相符"**：`canonicalResult('T[...]')` 走 opaque 分支
  （`:2449-2486`；`T` 不在 `RESULT_SCALARS`（`:2327-2349`）里 ⇒ `:2474-2478`）返回
  `{text: 'T[...]', opaque: true, family: false}`；判相符用的是**文本相等**（`:3714-3716` 的 `agreed`），
  **不看 opaque** ⇒ `resultsCompared++`，不进 `unchecked`。
- 豁免类 `declaration-names-an-element-the-rule-takes-from-its-argument`（label 在 **`:3755`**，
  路径 `:3753-3768`）**确实覆盖结果类型**，但它只在"两侧文本不同 + 声明写的是浮点族之一
  （`FLOAT_ELEMENTS` `:2354`：`float`/`f32`/`bf16`/`f16`）+ 记录写 `float[...]`（`family`，`:2485`）"时触发；
  我们两侧同字，**用不到它**。该 label 目前在 `EXCEPTIONS` 里**没有条目**（全文件只 `:3755` 一处出现），
  即当下没有任何条目落在这一类，我们也不会让它落进去。
- **参数类型 `t: T[...]` vs 记录的 `t: Tensor`**：`declaredNeed('T[...]')` 返 `'tensor'`
  （`:3396-3416`，注释原话 "including a type variable applied to dimensions"），
  `operatorAccepts('Tensor')` = `{kinds:['tensor']}`（`KIND_SETS` `:2829`），`kindCovers` 相等 ⇒ `'fit'`
  ⇒ `paramsCompared` 不变、不进 `paramsUnchecked`。`EXCEPTIONS.parameter` 里现在**没有**这 12 条
  （grep `(tensor:(abs|relu|exp|…))#parameter` = 0），改完也不会有。
- **两侧的计数判据是地板/天花板，不是定值**：`registry.result_types_match` 断言
  `comparedCount > 400` 且 `excusedCount < 60`（`test/plans/feat/modules/stdlib_registry.plan.toml:118-138`），
  参数侧同（`:157-172`）⇒ 3 条从"只被比较、判定相符"……本就在"比较过"里，改后仍在，计数不跨阈值。

**实读（不是只读码）**：让 `--verify-ops` 的输出落进日志（探针用例故意让 expect 失败，跑完即删），
在本枝 tip 上得到的原文是：

```
the declared result type agrees with the operator's own for 548 of them; 46 overload(s) with types this checker cannot read are listed above
the exception lists are the ones EXCEPTIONS records: result 46 in 10 class(es), parameter 27 in 6 class(es)
the declarations agree with the 594 registered operator overloads
```

随后打印的 46 个豁免成员里**没有** `abs`/`relu`/`exp` 这一族（只有 `attempt`、`tree_leaves`、
`transforms` 的 `jvp`/`split`/`transpose`/`vmap`/`remat`、`from_bytes`×5、`load_npy`×5 与 nn 的聚合类型），
10 个类里也**没有** `declaration-names-an-element-the-rule-takes-from-its-argument`
（类清单：generic application 1 / type variable 13 / function type 3 / generic application 1 /
gradient-of 1 / named type 17 / record 1 / tuple 7 / type variable 1 / no result 1）
⇒ 今天的 12 条确实在"比较过且相符"的 548 里，`EXCEPTIONS` 里既没有它们、也没有那个类。
`from_bytes` 那 5 条之所以在豁免里，是因为它**每个元素类型注册一次**（声明 `T[...]` vs 算子 `bfloat16[...]`）；
我们的 12 条是**一条一个名字**、结果文本两侧可以写成同一个 `T[...]`，所以不会落到那一类。

### 10.4 `__neg__`：只能改记录，加不了声明

`tensor:neg` 这一条在 `inspect --ops` 里是 `name = __neg__`、`uri = tensor:neg`。
`--verify-ops` 在 `:3494` 把所有 `name.startsWith('__')` 的算子从 `allOps` 里**排除**，
于是任何名为 `neg` / `__neg__` 的声明条目都会撞上 `:3805` 的
"declared but no such operator is registered" ⇒ 失败。**结论：`__neg__` 不能声明，判据也看不见它**，
它这一格只能靠 `ops-table` 那 12 行读数作证（本页第二节）。它的入口在语言里也到不了（本页第四节）。

### 10.5 文档重生成

- 命令：`node scripts/stdlib-reference.mjs --write`
- 判据：`registry.reference_is_generated` = `--verify-doc`，必须打印
  `docs/stdlib-reference.md is up to date`（plan 文件 `:426-435`）
- 预期 diff **只有两行签名行**：`docs/stdlib-reference.md` **L2156**
  （`abs: (t: float[...]) => float[...]` → `abs: (t: T[...]) => T[...] when is_numeric::<T>()`）
  与 **L2234**（`relu: …` 同形）；两节里的描述、`- Pure.`、`- Example:` 三行不变
  （`abs` 节 L2153-2162，`relu` 节 L2231-2240）。`neg` 不新增小节（无声明）。
  渲染形状的现成样子见 L2071：`zeros: (shape: int[]) => T[...] when is_numeric::<T>()`。

### 10.6 落地后应取的读数

1. `-Check ops-table`：12 行里**恰好 3 行变**（`abs`/`relu`/`__neg__` 的 `signature`：`float[...]` → `T[...]`），
   其余 9 行逐字节不变；总行数仍 825。
2. `feat/modules/stdlib_registry` 仍 `14/14`（含 `--verify-ops` 与 `--verify-doc`）。
3. `dirty=0` 后取，贴 `[provenance]` 与 `BUILD_EXIT=0`。
4. 75 表（141 名）比对：无新红（甲 ∅ / 表外红 0）。

### 10.7 不属于这一笔的一半：参数的**宽度**

记录写 `Tensor`（`kindText` `op_def.cpp:99-100`），声明写 `t: T[...]`——两者都只说"张量"。
A 类清单的原话是"张量或嵌套数值数组"（`TensorLike`）。若要让两侧都说宽度，可**同时**改成联合：
记录 `ParamKind::TensorLike`（`op_def.cpp:101-102` 的拼写就是 `float[...] | number[]`）、
声明同写 `float[...] | number[]` ⇒ `KIND_SETS` 里本来就有这一对（`:2830`），`declaredNeeds` = `['tensor','array']`
被 `operatorAccepts` 的 `['tensor','array']` 覆盖 ⇒ 仍 `'fit'`，**判据也不用动**。
但它改的是"`abs` 接不接受 `number[]`"这个**语言面**的事（真正放开要动 `unaryKernel` 的输入转换），
与"结果元素类型"是两笔，按"一格一件"单独排，本轮不做。