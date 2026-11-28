# `tensor` 的"算出来的签名"改由 Camel 类型函数写（`pow` 第一例）

**阶段一：只出方案，不改代码。** 读数都在 `origin/integration`（尖端 `24cc401ac5`）；本文所有条数都是按源码数出来的，命令在文末。

## 0 一句话

`tensor` 里有一族算子的签名"元素类型或形状随实参走"，今天整条记录写在 C++（`OpDef`）里、只在
`src/stdlib/tensor/signatures/tensor.cml` 留一份参考。目标是把它们改成 **Camel 声明 + 类型函数**（原则 23
"类型函数是在编译期运行的普通函数"、663 "声明写不出的签名靠类型函数算出"），`pow` 先落地，其余分批跟上；
判据不是"跑通"，是 **`camel inspect --ops` 的逐条记录迁移前后不变**，不一致的逐条单列。

## 1 现状读数（这一族到底多大）

`src/stdlib/tensor/ops/*.cpp` 里共 **100 条** `OpDef` 记录（`.name =` / `binaryDef<>` / `unaryDef<>` /
`compareDef<>` / `reduceAxisDef<>` / `internal(...)` 六种登记形态，按名字去重）。分四块：

| 块 | 条数 | 是什么 | 例子 |
|---|---|---|---|
| **声明了的**（固定签名） | 26 | `src/stdlib/tensor.cml` 里有无体声明，`src/stdlib/tensor/module.cpp:91` 的 `kDeclaredOperators[]` 逐条列出 | `numel` `shape` `dim` `show` `sum_stated` `add_stated` `to_bytes` |
| **算出来的 + 有 Camel 名** | 47 | 签名由 `OpDef::infer` 算，模块名可调，记录在 `signatures/tensor.cml` | `zeros` `eye` `maximum` `where` `reshape` `softmax` `sum` `sum_axis` |
| **只有保留名（`__x__`）** | 20 | 只有运算符拼写，没有 Camel 名、没有声明、不在参考文件里 | **`pow`** `add` `subtract` `multiply` `divide` `mod` `neg` `not` `lt` `le` `gt` `ge` `strict_eq` `strict_neq` `idx` `idx2d` `idx_take` `row_add_at` `take_grad` `from_bytes_aggregate` |
| **内部助手**（不导出） | 7 | 只为 `#impl` 内核存在 | `cast_like` `expand_axis` `permute_inverse` `gelu_grad` `erf_grad` `matmul_grad_lhs` `matmul_grad_rhs` |

⇒ 这次要迁的"整族" = **47 条有名字的 + 20 条保留名的 = 67 条记录**（37 条是同一族的不同拼写/兄弟，见 §2）。
参考文件今天覆盖的是 47 条有名字的（**43 个名字 / 55 条 `export func`**，`sum`/`mean`/`max`/`min`/`argmax`/`argmin`
各两条：标量那条与 `*_axis` 那条同名，所以 6 条 `*_axis` 记录在文档里是那 6 个名字的第二条）。

**关键一条：`pow` 现在不在这 47 条里，在"只有保留名"那 20 条里** —— 它今天没有 Camel 名、没有声明、
参考文件里也没有（`git grep "\bpow\b" -- 'src/stdlib/**/*.cml'` 只命中 `math.cml` 的标量 `pow`）：

```
src/stdlib/tensor/ops/elementwise_ops.cpp:736   defs.push_back(binaryDef<k::BinaryOp::Pow>("pow", {"__pow__"}));
src/stdlib/tensor/ops/elementwise_ops.cpp:203   const std::vector<ParamSpec> kBinaryParams = {
src/stdlib/tensor/ops/elementwise_ops.cpp:204       {"lhs", ParamKind::TensorOrScalar},
src/stdlib/tensor/ops/elementwise_ops.cpp:205       {"rhs", ParamKind::TensorOrScalar},
src/stdlib/tensor/ops/elementwise_ops.cpp:218   template <k::BinaryOp Op> OpDef binaryDef(name, exports) {
... .params = kBinaryParams, .resultDoc = "float[...]", .infer = binaryInfer(k::binaryProducesFloat(Op)), .kernel = &binaryKernel<Op>, .traits = elementwiseTraits()
```
即 `tensor:pow` = `(lhs: TensorOrScalar, rhs: TensorOrScalar) => float[...]`，元素类型取自提升、
形状取自广播，**两件都随实参走**，所以声明写不出——这正是要由类型函数算的东西。

## 2 ① 整族清单：分六类，各自缺什么

| 类别 | 条数 | 例子 | 声明今天为什么写不出 |
|---|---|---|---|
| A 一元，元素类型随实参、形状不变 | 12 | `abs exp log sqrt rsqrt sigmoid tanh relu gelu erf erfc neg` | 不是"形状"问题，是**参数类型**：`TensorLike`（张量或嵌套数值数组）无写法 |
| B 二元逐元素，形状随广播、元素类型随提升 | 8 | **`pow`** `add subtract multiply divide mod maximum minimum` | 两个参数都是 `TensorOrScalar`，结果 = 提升后的元素类型 × 广播后的维；**这是 `pow` 那一类** |
| C 比较，结果 `bool[]` | 6 | `lt le gt ge strict_eq strict_neq` | 参数是 `TensorLikeOrScalar`（三态：张量 / 数组字面量 / 标量），结果元素类型是 `bool` |
| D 形状/秩随实参 | 8 | `reshape permute transpose unsqueeze concat take matmul one_hot` | 形状**由值参数定**（`reshape` 的目标形状是角度括号里的常量），类型函数要能读常量参数 |
| E 归约与规范化，形状随 axis | 12 | `sum mean max min argmax argmin` 各两条（标量那条 + `*_axis` 那条） | 同上：`axis` 是值，`keepdims` 是值 |
| F 可选参数 | 6 | `softmax log_softmax softmax_grad layer_norm layer_norm_grad linear` | 声明位置**没有可选参数语法**（参考文件里的 `axis?: int`、`eps?: number` 是参考写法，见 §6.3） |

外加"元素类型来自调用"的 5 条（`zeros ones eye load_npy from_bytes`，`elementFromCall = true`）——这一类
**已经声明得出来**（`zeros::<f32>([2,3])`，`tensor.cml` 里就是无体声明），只是它的结果元素类型来自调用位置，
不属于本次要迁的"算类型"部分，保留现状。

## 3 ② 类型函数与声明形态

**写在哪。** `src/stdlib/types.cml` 是类型函数的家（`ShapeOf`、`ElementOf`、`WithElement`、`RowsOf`、
`PayloadOf`/`KeyOf`/`ValueOf`）。现有两种形态，都已可用：

```camel
// 有右边：Camel 自己算（types.cml:70）
export type ShapeOf<T> = match type T {
    case E[...Dims] => int[Dims]
    case E[...] => int[]
    case _ when is_numeric::<T>() => int[0]
}
// 无右边：编译器给的原语（types.cml:39，663 允许的第三种）
export type PayloadOf<T>
```

**声明里怎么用**：已有先例，`src/stdlib/tensor.cml:66` 就是 `export func shape::<T>(t: T) => ShapeOf<T>`。
即结果位置可以直接写一个类型函数应用；参数位置可以直接写裸类型变量（`t: T`），再用 `when` 加谓词。

**`pow` 落地需要的三个类型函数**（都放 `types.cml`，都有文档与 `is_same` 例）：

| 类型函数 | 签名 | 干什么 | 现状 |
|---|---|---|---|
| `WithShape<T, S>` | `export type WithShape<T, S> = match type T { case E[...] => … }` | 把 `T` 的维换成 `S` 的维（`ShapeOf` 的逆） | 需新写 |
| `BroadcastDims<S1, S2>` | `export type BroadcastDims<S1, S2> = …` | 两个 `int[...]` 维表按 NumPy 广播合并（缺维补 1、逐维取大、不可广播则拒绝） | 需新写；**要能对维包做逐位运算**，这是唯一有语言风险的件（§6.1） |
| `ElementPromote<A, B>` | `export type ElementPromote<A, B> = match type A { … }` | 元素类型提升表（`f32×float→float`，`bf16/f16` 按 7.3 算 f32 再一次舍入） | 需新写；嵌套 `match type` 即可表达 |

**`pow` 的完整样子**（三处改动，一笔提交）：

```camel
// 1) src/stdlib/types.cml：三个类型函数（如上）

// 2) src/stdlib/tensor.cml：一条无体声明取代 C++ 记录的签名
/// 逐元素幂，广播同 `maximum`：结果为提升后的元素类型、广播后的维；标量×标量不走这里（`math::pow`）。
/// @example pow(f32[2, 3] 的值, 2.0) => f32[2, 3]
export func pow::<A, B>(lhs: A, rhs: B) => WithShape<ElementPromote<ElementOf<A>, ElementOf<B>>,
                                                    BroadcastDims<ShapeOf<A>, ShapeOf<B>>>
    when is_numeric_value::<A>() and is_numeric_value::<B>()
```
```cpp
// 3) src/stdlib/tensor/ops/elementwise_ops.cpp:736：记录只留内核与事实，签名三项交给声明
defs.push_back(binaryDef<k::BinaryOp::Pow>("pow", {"pow", "__pow__"}));   // 加 "pow" 名字，保留 __pow__
// binaryDef 里删掉 .params / .resultDoc / .infer（由声明编进同一条记录，663：每项信息只来自一个入口）
```
```cpp
// 4) src/stdlib/tensor/module.cpp:91 的 kDeclaredOperators[] 加 "pow"
//    （守卫要求它正好等于声明名集合，两个方向都比）
```

§1 的 `elementwise_ops.cpp:736` 那一行就这样对上了：`("pow", {"__pow__"})` 是"记录存在、名字是保留名"，
迁移后 `pow` 第一次有了 Camel 名；`^`/`__pow__` 仍由 C++ 导出（保留组由 C++ 导出的规则见
`scripts/stdlib-declarations.mjs:71-79`），两条入口**同一个 URI** `tensor:pow`、**同一条记录**，正是 663 的形状。

## 4 ③ 迁移顺序与每步判据

每步都是"一笔提交 + 一张读数"，前一步不绿不进下一步。

| 步 | 做什么 | 判据（缺一不算） |
|---|---|---|
| 0 | **先取基线**（不改代码）：`camel inspect --ops` 全量快照、`node scripts/stdlib-reference.mjs --verify-ops`、`node scripts/stdlib-declarations.mjs --check` 三份原文 | 三份都绿；`--verify-ops` 的 `checked / paramsCompared / resultsCompared` 记下来当基线；**若已有红（§7 那一处静态对不上要先说清）** |
| 1 | `types.cml` 三个类型函数 + 用例（`is_same::<BroadcastDims<int[2,1], int[1,3]>, int[2,3]>()` 一类） | 用例绿；`--verify-ops` 不动（没碰算子）；编译期开销可忽略 |
| 2 | **`pow`**（本方案的样板）：删记录签名三项、加声明、加 `kDeclaredOperators`、参考文件不动 | 见 §5 的四道判据全绿；三条形状各能编译并跑（`T×T`、`T×number`、`number×T`），标量×标量仍走 `math::pow` |
| 3 | B 类其余 7 条（`add subtract multiply divide mod maximum minimum`） | 同上；`maximum/minimum` 本就在参考文件里，迁完从文件里删掉并重生成文档 |
| 4 | A 类 12 条（一元）——最简单，形状不变，只差参数种类 | 同上；`gelu/erf` 的 `*_grad` 是内部助手，不迁 |
| 5 | C 类 6 条（比较，`TensorLikeOrScalar` 三态） | 同上；判据多一条：数组字面量实参（`[1.0, 2.0]`）仍按 5.1 字面量规则走 |
| 6 | D 类 8 条（形状随值参数） | 同上；这一步需要类型函数**读常量参数**（`reshape<shape>`），若语言今天给不了，就停在 6 之前报回来，不硬做 |
| 7 | E/F 两类（axis、可选参数） | **前置条件是语言先有可选参数语法**；没有就只登记为待办，不迁 |

迁完 `signatures/tensor.cml` 清空 → 删文件与 `stdlib-reference.mjs` 里 `computed:` 那一行。

## 5 ④ `--verify-ops` 怎么逐条比（这一步的关键）

"逐条一致"要拿**同一条记录的两份文本**比，不是拿行为比。量具是现成的三件：

1. **快照**：`camel inspect --ops <probe 导入 tensor>` 给每条算子打一行 JSON
   （`introspect.cpp:259`：`module name uri with norm purity signature accesses accesses_declared record
   uses_stated clause_pure external may_fault cost_stated cost [private] [semantics]`）。
   迁移前存 `before.txt`，每步后存 `after.txt`，`diff` 只许出现**本步迁过的那几条**，且**除 `signature`
   的拼写之外一个字都不许变**（`purity`/`may_fault`/`accesses`/`record`/`uses_stated`/`clause_pure`/`cost`
   全都要原样——这些正是"同一条记录"的证据）。
2. **`scripts/stdlib-reference.mjs --verify-ops`**：声明 ↔ 注册表逐条比（`verifyOps()`，`3495` 起）：
   每个注册算子必须有声明、参数个数与**逐位参数名**、结果类型（`matchTypeVerdict` 读类型函数）、纯度。
   它自己有两个"不许空跑"的计数：`paramsCompared`、`resultsCompared` 掉到 0 就算失败。**迁移后这两个数
   不能降**；迁过的算子若落进它的 `unchecked / paramsUnchecked` 名单，就是"比不了"，要在交付里单列。
3. **`scripts/stdlib-declarations.mjs --check`**：守住"迁过去的不许迁回来"——声明固定了签名的名字，
   C++ 不许再登记 `StaticFuncTypeResolver`；`kDeclaredOperators` 与声明名集合**两个方向**都要相等
   （`module.cpp:91` 那份表）；某个名字的签名仍由 C++ 算（`DynamicFuncTypeResolver`）才允许留在 C++，
   且必须记在 `signatures/<module>.cml` 里。

**不一致怎么记**：交付里一张三列小表 —— 名字 / `before` 与 `after` 的 `signature` 文本 / 判决
（`同`、`文本差（说明为什么，例如 TensorOrScalar 的 `Tensor | number` 变成类型函数的展开写法）`、
`行为差（是缺陷，不许提交）`）。行为差一票否决。

配套的行为判据（缺了"逐条一致"没有意义）：`vjp-parity --tables` 的 `newDebt 0 / staleCount 0`、
`feat/trans` 与 `feat/compile` 对当前晋级红表 **甲 0**、AST 单列一笔、`[provenance] tip + binary built`、`dirty=0`。

## 6 ⑤ 风险与不改的部分

**6.1 唯一的真风险：维包的逐位运算。** `BroadcastDims<S1, S2>` 要拼两个维包、逐位取大、补 1。今天
`match type` 的一个 case 只能绑**一个** dims pack（`ShapeOf` 的 `case E[...Dims]`），`static for` 能遍历
一个包。若"用两个包算出一个新包"今天表达不了，**按 663 的第三条退路办**：写成不带右边的类型函数
（`export type BroadcastDims<S1, S2>`，编译器给），像 `PayloadOf` 那样"有名字、有文档、可组合"。
这不算开一门类型层语言（原则 23），是 663 明文允许的编译器原语。**先做一天的验证，验证不过就走退路，
不许在库里塞特例。**

**6.2 元素类型提升是语言规则，不是库的偏好。** 今天在 `binaryInfer` 里（C++）：`f64×f32→f64`、
整数张量与浮点、`bf16/f16` 算 f32 再舍入一次（7.3）、标量字面量按 5.1 的宽容。搬进 `ElementPromote` 时
**一个字都不许改语义**；任何一条行为差都按缺陷记，不放宽、不为通过改规则。

**6.3 可选参数是语言缺口，不是本族的问题。** 参考文件里的 `axis?: int`/`eps?: number` 是**参考写法**
（`signatures/tensor.cml` 不参与编译），真声明位置没有这个语法（`src/stdlib/*.cml` 里没有一处 `?:`）。
F 类 6 条要么等语言补，要么按 663 把默认值写进类型的文字（不推荐）——**登记为待办，不在本卡里做**。

**6.4 不改的部分。** `elementwise_ops.cpp` 的内核、`traits`、`vjp`、`semantics`/`index`（索引语义）一律不动；
`math::pow`（标量）不动；`tensor.cml` 里已有的 26 条固定签名不动；`signatures/tensor.cml` 只在某条迁完后
删**那一条**；设计原则不动。**不许**出现"C++ 记录 + Camel 窄重载"两套入口（663/红线），也不许给 `pow` 做
"原生特例"。

## 7 要决策者点头的两件事 + 一处待确认

1. **`pow` 要不要第一次拥有 Camel 名 `pow`？** 迁移的定义就是"记录搬到声明"，而声明必须有名字 ⇒ 是。
   代价：`tensor::pow` 成为可写的名字（今天只有 `^`/`__pow__`），参考页与 `inspect --ops` 多一行，需要一个
   新用例钉住"名字可写、行为与 `^` 相同"。若不想加名字，那就只能停在"参考文件里补条目"，不是迁移。
2. **`BroadcastDims`/`ElementPromote` 走 Camel 还是走 663 的编译器原语退路？** 建议：先按 §6.1 验一天，
   验得过就 Camel 写，验不过就走原语——两条都不违反原则 23，但后者要写进设计文档的"编译器原语"名单。
3. **待确认（step 0）**：静态对照有一处对不上——`mean_axis_stated` 在 `src/stdlib/tensor.cml` 里有声明，
   却不在 `kDeclaredOperators[]` 的 26 条里（而 `mean_stated` 在）。我按源码读到的是这样，`--verify-ops` /
   `stdlib-declarations --check` 今天是不是绿的，要用 step 0 的实测回答；**不绿就先修它，再谈迁移**。

## 8 读数怎么复现（全文数字的出处）

```bash
# 100 条记录、47 条算出来的、20 条保留名、7 条内部：按下面六种登记形态取名字去重
git grep -n 'binaryDef<\|unaryDef<\|compareDef<\|reduceAxisDef<\|internal(' -- src/stdlib/tensor/ops/
git grep -n '\.name *= *"' -- src/stdlib/tensor/ops/
# 26 条声明名（与 kDeclaredOperators 对照）
git grep -n '^export func' -- src/stdlib/tensor.cml
sed -n '91,105p' src/stdlib/tensor/module.cpp
# 43 个名字 / 55 条 export func
grep -c '^export func' src/stdlib/tensor/signatures/tensor.cml
# pow 的三行
sed -n '203,206p;218,229p;736p' src/stdlib/tensor/ops/elementwise_ops.cpp
```