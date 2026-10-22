# 49 计算签名的账本：`stdlib/signatures/` 的每条条目各是什么

**问题**（H194）：`stdlib/signatures/` 还有 14 个文件、一批只作参考的 `export func` 条目。
它们到底是什么？每条属于哪一类、差什么才能写成声明、最该先问作者的是哪一件事？这个目录还应
该存在吗？缺口本身的语法与提案见 [47](47_declaration_syntax.md)，迁移配方与检查器见
[48](48_declaration_migration.md)；本页不重复那两页，只把**条目**逐条归账。

## 0. 回答

**14 个文件、224 条条目、170 个名字。** 四桶各是：

| 桶 | 标签 | 条目 | 名字 | 含义 |
|---|---|---:|---:|---|
| 1 | `declarable-today` | **22** | 17 | 用今天的语法（按元数／拼写重载、`typeas`）就能逐点复现 C++ resolver，零决策 |
| 2 | `needs-one-feature` | **104** | 99 | 各差一条已记录在 47 的机制或政策；换一条机制就移动一批 |
| 3 | `permanently-reference-only` | **98** | 54 | 声明一定是错的，不是难写 |
| 4 | `not-a-signature` | **0** | 0 | 本基点没有：224 条都对应 `inspect --ops` 打印的算子 |

- **最该先问作者的一件事**：是否允许一条声明**把结果类型交给算子的类型规则**（47 §4.3
  “结果取自实参”，约定 `docs/stdlib-conventions.cn.md:47`）。这一条政策单独移动 **62 个名字**
  （`tensor` 49 + `nn` 13，桶 2 的 60%），代价是规则条数——`Tensor | number` 的每个参数位有
  5 种标量拼写，8 个逐元素名字因此各要 25 条——不是风险。同一机制下的 `tiles` `run*` 3 个还要
  with 函数的参数表（Q-V），其余六条机制合计 34 个名字。
- **这个目录去不掉**：桶 3 的 98 条永远不会变成声明（`globals` 93 条没有声明宿主；
  `range`/`sum`/`mean` 5 条违反“一个名字一处”）。但它今天不该是 14 个文件：不动任何机制先迁掉
  桶 1，就只剩 10 个文件 202 条；两条大政策都落地后，下界是 **2 个文件 98 条**。

## 1. 目录里到底有什么

- **条目 = 一条 `export func`**（`#op<'uri'>` 绑定下一条），按文件顺序记；名字是 `export func`
  的名字。224 条里有 54 条与另一条同名（`math:sqrt` 4 条、`tensor:sum/mean` 各 2 条、
  `json:encode` 等），所以是 **224 条 / 170 个名字**。
- **14 个文件都对得上** `scripts/stdlib-reference.mjs` 的 `computed:` 字段；`--verify-ops` 逐条
  与 `inspect --ops`（794 行）对账，224 条都落到一个已注册算子上，这就是桶 4 为空的意思
  （没有任何一条是“只有文档、没有算子”的条目；目录里剩下的只有注释）。
- **brief 的 241 条不在任何基点上**：H194 的基点 `29a55e0d4` 是 232 条，H163 之后今天 224 条；H183 删除
  `stdlib/signatures/rewrite.cml`（10 条，`rule`/`rule` with `when`/`analysis` 的参考条目，
  机制见 [46](46_rules.md)）之前是 242 条。241 与两者都不符，按树上的数记账。
- **`--check` 的 112 不是这 224 条**：它数的是 **12 个拥有签名文件的模块**里记录的计算签名（去重
  后 112 个名字）。这 224 条里另有 `globals` 93 条（不可导入）与 `ckpt` 7 条（没有 `stdlib/ckpt.cml`），
  两者是**不同集合**，不要相加。`env`/`process`/`hash` 已由 H163 迁走，它们的签名文件已删除。

## 2. 四桶的判定规则

判据是**机制**，不是感觉：读 resolver 的接受集合与结果规则，再用已经编译好的编译器试写声明
（§4、§9）。

1. **桶 1**：存在有限条声明，接受集合与结果类型和 resolver 逐点相同。用到的只有文档已裁定的
   写法：按元数／拼写重载（[47](47_declaration_syntax.md) §2.1、§2.2；约定
   `docs/stdlib-conventions.cn.md:46`）、`typeas`（约定 §2）、`#op` 确切 URI（约定
   `docs/stdlib-conventions.cn.md:40`）。这样的条目只剩两种“为什么还没迁”：模块没迁
   （`unmigrated-module`），或曾被写成“按设计是计算签名”（`misjudged-computed`）。
2. **桶 2**：结果或参数类型真的取决于实参、调用侧类型实参或实参的结构；每条都指到 47 的一节。
3. **桶 3**：没有声明宿主，或标准库自己的规则使声明成为第二份签名——这时声明不是“难写”，是
   **错**。
4. **桶 4**：文档条目，或没有算子的名字。本基点为 0；最接近的是
   `stdlib/signatures/tensor.cml:345-346` 那条“`show` 已声明”的注释——它是注释，不是条目，
   不参与分类（§10 缺陷 5）。

## 3. 逐模块

| 文件 | 条目 | 名字 | 桶 1 | 桶 2 | 桶 3 |
|---|---:|---:|---:|---:|---:|
| `algo.cml` | 7 | 7 | 7 | – | – |
| `autodiff.cml` | 2 | 2 | – | 2 | – |
| `ckpt.cml` | 7 | 7 | – | 7 | – |
| `globals.cml` | 93 | 51 | – | – | 93 |
| `graph.cml` | 6 | 6 | 2 | 4 | – |
| `json.cml` | 4 | 2 | – | 4 | – |
| `math.cml` | 8 | 3 | 8 | – | – |
| `nn.cml` | 20 | 18 | – | 20 | – |
| `onnx.cml` | 1 | 1 | – | 1 | – |
| `para.cml` | 2 | 2 | 2 | – | – |
| `python.cml` | 6 | 5 | – | 6 | – |
| `random.cml` | 3 | 3 | 3 | – | – |
| `tensor.cml` | 55 | 53 | – | 50 | 5 |
| `tiles.cml` | 10 | 10 | – | 10 | – |
| **合计** | **224** | **170** | **22** | **104** | **98** |

桶 4 全为 0，未单列一列。

逐条记录在 `scripts/computed-gaps.mjs`（`LEDGER`，按文件顺序，含 `#op` URI、桶、机制与证据），
`node scripts/computed-gaps.mjs` 打印上面两张表。名字按机制分组：

| 桶 | 机制 | 条目 | 名字 | 证据 |
|---|---|---:|---|---|
| 1 | `misjudged-computed` | 22 | `sort` `insert_sort` `quick_sort` `merge_sort` `merge_sorted_arrays` `flatten` `unique`（algo）· `constant` `add_constant`（graph）· `sqrt` `shl` `shr`（math）· `unordered_foreach` `unordered_reduce`（para）· `choice` `sample` `shuffle`（random） | `modules/algo/module.cpp:43-136`、`modules/algo/sequence.cpp:193-224`、`modules/graph/read.cpp:73-85,686-705`、`modules/graph/txn.cpp:432-444`、`modules/math/module.cpp:64-88`、`modules/math/numeric.cpp:255-307`、`modules/para/module.cpp:46-104`、`modules/random/module.cpp:43-83` |
| 2 | `result-from-argument` | 65 | tensor：`new` `empty` `zeros` `ones` `full` `eye` `random` `randn` `add` `subtract` `multiply` `divide` `pow` `maximum` `minimum` `where` `neg` `abs` `exp` `log` `sqrt` `rsqrt` `sigmoid` `tanh` `relu` `gelu` `erf` `reshape` `flatten` `unsqueeze` `transpose` `permute` `concat` `slice` `take` `one_hot` `max` `min` `argmax` `argmin` `softmax` `log_softmax` `softmax_grad` `layer_norm` `matmul` `linear` `zeros_like` `ones_like` `cast` · nn：`conv2d` `conv2d_input_grad` `conv2d_kernel_grad` `conv2d_bias_grad` `max_pool2d` `avg_pool2d` `max_pool2d_grad` `avg_pool2d_grad` `batch_norm` `batch_norm_train` `batch_norm_train_grad` `embedding` `embedding_table_grad` · tiles：`run` `run_device` `run_preferred_resident` | `modules/tensor/ops/op_def.h:55-72,139-161`、`modules/tensor/ops/registry.cpp:61-143`、`modules/nn/tensor_ops.cpp:653-853`、`modules/tiles/src/module.cpp:486-592`；[47](47_declaration_syntax.md) §4.3 |
| 2 | `arbitrary-function-type` | 12 | `grad` `with_grad`（autodiff）· `program_of` `graph_of` `open_function` `to_function`（graph）· `export_model`（onnx）· `gpu` `source` `inspect` `benchmark` `regions`（tiles） | `modules/autodiff/operators.cpp:55-98`、`modules/graph/txn.cpp:370-386,401-447`、`modules/onnx/module.cpp:138-145`、`modules/tiles/src/module.cpp:486-592`；[47](47_declaration_syntax.md) §3.1 |
| 2 | `type-predicate` | 10 | `save_tree`（ckpt）· `encode`（json）· `softmax_cross_entropy` `softmax_cross_entropy_grad` `sgd` `adam_state` `adam`（nn） | `modules/tree/module.cpp:30-60`、`modules/json/module.cpp:71-90`、`modules/nn/tensor_ops.cpp:798-853`、`modules/nn/optim.cpp:390-463`；[47](47_declaration_syntax.md) §3.5 |
| 2 | `structural-type-function` | 7 | `tree_map` `tree_reduce` `tree_unzip` `tree_leaves` `tree_paths`（ckpt）· `upload` `download`（tiles） | `modules/tree/module.cpp:30-60`、`modules/tiles/src/module.cpp:486-592`；[47](47_declaration_syntax.md) §4.4、§4.5 |
| 2 | `type-argument-return` | 4 | `load_tree`（ckpt）· `decode_as`（json）· `from_bytes`（tensor） | `modules/tensor/ops/binary_ops.cpp:127-143`、`modules/json/module.cpp:83-90`；[47](47_declaration_syntax.md) §3.4 |
| 2 | `variadic` | 4 | `py_call` `py_print` `py_println`（python） | `modules/python/module.cpp:374-467`；[47](47_declaration_syntax.md) §3.2 |
| 2 | `module-type-argument` | 2 | `py_wrap` `py_unwrap`（python） | `modules/python/module.cpp:476-503`、`src/compile/gct/builder.cpp:4871-4884`；[47](47_declaration_syntax.md) §3.3 |
| 3 | `no-operator-host` | 93 | `globals` 的全部条目（如 `int` `parse_int` `print` `format` `map` `get` `contains`，51 个名字） | `src/builtin/modules/globals.cpp:2144`（`BuiltinModule("")`）、`docs/stdlib-conventions.cn.md:40`（`*` 通配只在签名文件里） |
| 3 | `one-name-one-source` | 5 | `range` `sum` `mean`（tensor，含两条带 `_axis` URI 的重载） | `modules/tensor/ops/creation_ops.cpp:219-228`、`modules/tensor/ops/reduction_ops.cpp:313-330`；[47](47_declaration_syntax.md) §4.2 |

## 4. 桶 1：22 条今天就能迁，零决策

这 17 个名字今天的声明就能覆盖，但 C++ 仍然自己算：或者曾被记成“计算签名”，或者模块还没迁移。
在主机上对**编译好的编译器**试过（`CAMEL_STD_LIB` 指向 stdlib 的副本，不动树）：

```
export func sort(xs: (typeas T)[]): T[]                      // insert_sort/quick_sort/merge_sort/unique 同形 1 条
export func flatten(xss: (typeas T)[][]): T[]
export func merge_sorted_arrays(left: (typeas T)[], right: T[]): T[]
#op<':mark/unordered_foreach_arr'>
with <f: (item: typeas T) => void>
export func unordered_foreach(xs: T[]): void
#op<':mark/unordered_reduce_arr'>
with <f: (acc: typeas T, item: T) => T, initial: T>
export func unordered_reduce(xs: T[]): T
#op<'rand:choice'>
export func choice(xs: (typeas T)[]): T                       // sample/shuffle 同形
#op<'math:sqrt'>
export func sqrt(x: i32): f32                                 // int→float、f32→f32、float→float，共 4 条
#op<'math:shl'>
export func shl(x: i32, count: i32): i32                      // 4 条拼写；shr 同形
export func constant(n: Node, sample: i32): i32?              // Int32/Int64/Float32/Float64/bool/string，共 6 条
export func add_constant(t: Txn, g: Graph, value: i32): Node  // 同样 6 条
```

- 结果：全部装载成功，调用落到声明上（`algo:sort` 的值类型是 `int[]`、`algo:unique` 是
  `string[]`、`unordered_reduce` 是 `int`、`rand:choice` 是 `int`、`sqrt(9)` 是 `float`、
  `sqrt(i32(9))` 是 `f32`、`shl(1, i32(2))` 是 `int`、`graph:constant` 是 `int?`、
  `graph:add_constant` 是 `Node`）。
- 反例也成立：`unordered_foreach<(x: string) => println(x)>(int[])` 与
  `constant(n, [1.0, 2.0])` 都被拒绝，诊断里打印的就是这几条声明——声明没有放宽接受集合。
- 迁移任务：把名字从 `stdlib/signatures/<m>.cml` 移到 `stdlib/<m>.cml`，删掉 C++ 的
  `DynamicFuncTypeResolver` 与导出组（[48](48_declaration_migration.md) §5 的七步）。`algo`、
  `math`、`random`、`para` 四个文件可以整个删掉；`graph` 只剩 4 条。`env`/`process`/`hash`
  本来就没有 `stdlib/<m>.cml`，按 [48](48_declaration_migration.md) §4.2 的批次新建即可。
- 注意 `math` 的签名文件只记了 `shl`/`shr` 各 2 条（同型拼写），要写出 C++ 接受的全部调用
  需要 4 条（含混合拼写）；`47` §2.2 已裁定“一种拼写一条”，所以这是条数问题，不是缺口。

## 5. 桶 2：104 条，各差一条机制

按机制读法（每条都指到 47 的一节，那页有提案、依据与代价）：

- **`result-from-argument`（65）**：结果的 dtype／形状／轴由实参算出（`zeros([2,3])` 的形状、
  `conv2d` 的输出形状、`run` 的函数返回值）。声明给不出这条规则；近似声明会把该拒绝的调用
  放过去。这是唯一一条**政策**（其余是语法）。其中 `tensor` 49 个与 `nn` 13 个只差这一条；
  `tiles` 的 `run`／`run_device`／`run_preferred_resident` 的普通参数表也取自 with 函数
  （任意元数，见 Q-V），所以这一条政策独占 62 个，加上 Q-V 才到 65。
- **`arbitrary-function-type`（12）**：参数是“参数表未知的函数”（47 §3.1，Q-F）。`graph` 的
  4 个是 47 清单外的同形成员——47 §3.1 按 H168 的 9 模块口径写，没含 graph；迁移 graph 时
  不要再把它们当可声明。
- **`type-predicate`（10 条 / 7 名）**：接受集合是类型变量上的谓词（47 §3.5，Q3）：`save_tree`
  的“叶子都可存”、`json:encode` 的“类型有 JSON 形式”、`softmax_cross_entropy` 按 target 的
  dtype 分两条 URI、三个优化器的“参数有切线”。4 条 `softmax_*` 是同一名字的两条 URI，所以
  条目多于名字。
- **`structural-type-function`（7）**：结果类型是实参结构逐叶变换（47 §4.4；设计原则 5.5 第二期）。
  `ckpt` 的 5 个 `tree_*` 还叠了一层“没有执行器”（47 §4.5），即使类型函数有了也要等 H51 第 2 项。
- **`type-argument-return`（4 条 / 3 名）**：结果由 `::<T>` 或期望类型定（47 §3.4，Q1）；
  `load_tree` 还叠 Q3 的叶子谓词（48 §4.1）。
- **`variadic`（4 条 / 3 名）**：任意个实参（47 §3.2，Q-V）。它是这三个名字的前置而不是全部：
  `py_print`/`py_println` 的结果取第一个实参的类型，`py_call` 的第二个 URI 还要任意长元组／
  任意字段的结构体——所以 Q-V 单独解锁 0 个名字，与 47 §3.2 一致。
- **`module-type-argument`（2）**：`PyObject<T>`（47 §3.3，Q-M）；`Device<T>` 是现成先例。

## 6. 桶 3：98 条，声明一定是错的

- **`globals` 93 条（51 名）**：语言全局是 `BuiltinModule("")`，不是模块、不可导入，没有声明
  宿主（[48](48_declaration_migration.md) §4.2；H141 调查未裁定）。而且 45 条用 `:op/*tol`、
  `:struct/len_*` 这类 `*` 通配——约定 `docs/stdlib-conventions.cn.md:40` 只允许它出现在不编译的
  文件里。要让它们变成声明，先得回答“内置函数是不是模块”这个语言问题，不是迁移任务。
- **`range`/`sum`/`mean` 5 条（3 名）**：全量形式与轴形式共用一个名字。声明替换整组，而轴形式的
  结果形状取自实参，写一半就违反“一个名字一处”（47 §4.2；48 §4.2）。放宽这条规则才会移动它们，
  47 明确不建议。
- 这一桶是目录的**下界**：98 条、2 个文件。

## 7. 先把哪一件交给作者

| 顺序 | 机制 | 名字（独占） | 要动什么 | 风险 |
|---:|---|---:|---|---|
| 1 | `result-from-argument` | 65（62） | 一条政策：声明写明参数，结果交给算子的类型规则；47 §4.3 | 低（规则多，语义不变） |
| 2 | `arbitrary-function-type` | 12 | 新类型形式（47 §3.1 的 A/B 案） | 高 |
| 3 | `type-predicate` | 7 | 新机制（47 §3.5 的 A 案） | 很高 |
| 4 | `structural-type-function` | 7 | 5.5 第二期的类型函数 + `tree_*` 的执行器 | 高（跨语言设计） |
| 5 | `type-argument-return` | 3 | 改声明机制与重叠检查（47 §3.4） | 高 |
| 6 | `variadic` | 3（0） | 等 5.5 的 `..ts`；单独不解锁任何名字 | 中 |
| 7 | `module-type-argument` | 2 | `PyObject` 覆写 `typeArguments()` + 7.5 补例外 | 低 |

前四行之外的名字都很少；**第一行是唯一值得现在决定的**：独立移动 62 个名字、一条政策，代价只是
把文档已裁定的重载规则写出来（`Tensor | number` 的每个参数位 5 种拼写，8 个逐元素名字各 25 条）。

## 8. 这个目录还应该存在吗

应该，但不是现在这个形状。

| 情形 | 文件 | 条目 | 说明 |
|---|---:|---:|---|
| 什么都不做 | 14 | 224 | 今天（H163 之后） |
| 只迁桶 1（零决策） | 10 | 202 | `algo`/`math`/`random`/`para` 的文件消失，`graph` 只剩 4 条 |
| 再加 `result-from-argument` | 10 | 140 | 只剩桶 2 的其余 42 条与桶 3 |
| 所有机制都答完 | 2 | 98 | 下界：`globals.cml` 93 + `tensor.cml` 5 |

结论：目录**不能删**——98 条里有 93 条没有宿主、5 条被“一个名字一处”挡住；但它应该从 14 个
文件缩到 2 个。桶 1 的 22 条不需要任何决定，先做掉；桶 2 的 104 条等 7 条机制——第一条就能
独立移动 62 个名字。

每一桶清零的最小改动：桶 1 是**零决策**（照 [48](48_declaration_migration.md) §5 迁 5 个模块）；
桶 2 是 §7 的第一条政策加其余六条机制（其中 Q-V 单独不解锁任何名字）；桶 3 **不可能**清零，
除非改语言：让内置函数有一个可导入的模块宿主，并放宽“一个名字一处”。

## 9. 检查器：第 7 条规则与红证明

`node scripts/stdlib-declarations.mjs --check` 的第 7 条（`scripts/computed-gaps.mjs` 的
`classificationProblems()`）要求：签名文件里的每条条目在账本里恰好一条记录（按文件顺序、
`#op` URI 相同）；每条记录的桶与机制都是账本定义的标签；机制所属的桶与记录一致。缺记录、
多记录、不存在的标签、标签与桶不符，都报出来。`node scripts/computed-gaps.mjs` 单独跑同一套
自检，并打印 §3 的两张表。

红证明一：把一条已注册的条目重复追加进 `stdlib/signatures/algo.cml`（在主机的工作副本上做，
随后 `git checkout --` 恢复；`--verify-ops` 仍然通过，因为同名条目里有一条匹配）：

```
$ printf '\nexport func unique(xs: (typeas T)[]): T[]\n' >> stdlib/signatures/algo.cml
$ node scripts/stdlib-declarations.mjs --check
stdlib/signatures/algo.cml:59: 'unique' (algo:unique) is not classified in scripts/computed-gaps.mjs;
add it to the 'algo' row with a bucket tag
migrated modules stay migrated: 1 problem(s) across 26 declaration module(s)     [exit 1]
```

红证明二：把一个桶标签改成一个不存在的值：

```
$ node scripts/computed-gaps.mjs
scripts/computed-gaps.mjs: 'sort' (algo) names the bucket 'declarable', which has no tag; use one of
declarable-today, needs-one-feature, permanently-reference-only, not-a-signature
scripts/computed-gaps.mjs: 'sort' (algo) is in 'declarable' but its mechanism 'misjudged-computed'
belongs to 'declarable-today'
computed-gaps: 2 problem(s)                                                      [exit 1]
```

两条都恢复后：`migrated modules stay migrated: 26 declaration module(s), 112 computed signature(s)
left in C++, 0 excluded module(s), 224 signature entries classified, 0 problem(s)` 与
`computed-gaps: every entry is classified`。

## 10. 这一轮查出的文档缺陷

1. **代码注释把“能不能写”说反了**：`modules/algo/module.cpp:38-42`、`modules/algo/sequence.cpp:188-192`、
   `modules/math/module.cpp:64-67`、`modules/math/numeric.cpp:287-290`、`stdlib/para.cml:9-13` 都写
   “cannot be a declaration”。按 §4 的试写，有限条声明就能复现接受集与结果类型；正确的说法是
   “按当前约定被记作计算签名”，不是“写不出”。迁桶 1 时一并改掉。
2. **`48` 的 globals 数**：H194 时写“50 名 96 条”，树上是 51 个文件内名字、93 条声明。H200 已把
   48 改成“50 个公开名 / 93 条声明”，并注明文件里 51 个名字（含限定名 `std::mem::blocks`）与 96 条
   注册重载是两个度量。
3. **`48` 的模块数**：H194 时写“22 个模块”，H200 把表头、逐模块表与索引改成 26 个声明模块
   （H163 之后；H183 之后、H163 之前是 23）。
4. **`48` §3.4 的内联记录**：H194 时还说 `stdlib/math.cml`、`stdlib/graph.cml` 有 9 个内联
   `// dynamic func`；H180 合入后 `git grep "dynamic func" stdlib` 为空，H200 已按 0 内联改写
   （112 个计算名全在 12 个文件里）。
5. **`stdlib/signatures/tensor.cml:345-346`** 还留着“`show` 已声明”的注释（H175 已完成）；
   它是目录里唯一提到函数名却不是条目的文本，建议随下一次清理删掉。
6. **`47` §3.1 的 Q-F 清单**按 H168 的 9 模块口径，未含 `graph` 的 4 个函数值算子；它们同属
   Q-F（本页 §5）。
7. **`48` §3.5 的规则数**：H194 时写“有五条规则”，H200 已改成六条并把第 6 条（一个计算名一处）
   写进表；本页再加第 7 条（条目分类），两页对规则数的说法一致。
8. **brief 的 241 条**与树不符（232；H183 前 242），按树上的数记账。

## 11. 复现

```
node scripts/computed-gaps.mjs                      # 逐模块与逐机制的计数，并自检账本
node scripts/stdlib-declarations.mjs --check        # 需要已构建的 out/latest/bin/camel
node scripts/stdlib-reference.mjs --verify-ops      # 224 条对 794 行 inspect --ops
```

试写声明时用标准库的副本（`CAMEL_STD_LIB=/tmp/.../stdlib`），不要在树上改签名文件。