# P1-13 底数：按调用形状数的三栏表

这是 P1-13 的真实底数：`tag` 不等于"有声明"（`#op(...)` 只在声明不遵循"模块名 + 名字"约定时才需要，设计原则 6.4 第 1 步），所以口径从"数 tag"换成"**数调用形状**"——一个算子注册的**名字 + 元数**，在 `stdlib/` 里有没有同名同元数的声明。

工具与数据：`camel inspect --ops` 的 655 行注册重载（probe 一次导入 `array`、`tensor`、`bytes`、`str`、`io`、`graph`、`stream`），对 `stdlib/` 与 `modules/` 的 45 个被编译 `.cml`（`stdlib/signatures/` 的 6 个按文件头"not compiled"排除）读出的 632 条 `export func` 声明、去重 454 个调用形状。

## 三栏

| 栏 | 行数 | 占 655 | 含义 |
|---|---|---|---|
| 已覆盖 | **413** | 63% | 该 URI 有 `#op` tag，或有同名同元数的声明 |
| 真缺 | **189** | 29% | 没有；且签名写得出来 |
| 签名表达不了 | **53** | 8% | 没有；且今天的声明语法表达不了（联合参数 21、`Tensor` 参数 15、宽度派生自实参 11、宽度在文件里 5、`number` 1） |

**已覆盖 413 行的来路**：靠 tag 128（`:op` 的 128 行）+ 靠声明 285（`math` 96、`graph` 41+9、`tensor` 44、`array` 33、其余按协议）。

## 一条必须说清的限度

"同名同元数"是**必要**条件，不是充分条件：声明可以在，但它的 `when` 条件挡着张量实参。真跑过的一批（`ones::<f32>([2, 3])` 作实参，`std::nvm`）：

| 名字 | 声明在哪 | 接受张量实参？ |
|---|---|---|
| `sum`、`mean`、`min`、`max`、`argmax`、`argmin` | `stdlib/array.cml:155,167,177,187,196,205` | **接受**（6.0 / 1.0 / 1.0 / 1.0 / 0 / 0） |
| `abs`、`exp`、`log`、`erf`、`erfc` | `stdlib/math.cml` | **拒绝**：`WhenConditionUnmet`，`when is_float::<T>()` 对 `f32[2, 3]` 为假 |
| `pow` | `stdlib/math.cml:335` | **拒绝**：`(x: T, y: T)` 把 `T` 绑成 `f32[2,3]`，第二个实参 `float` 对不上 |
| `dim` | `stdlib/array.cml:290` | **假匹配**：轴是 `<1>` 里的静态参数，不在括号里 |

所以"已覆盖 413"里，**这 13 个只有 6 个是被证明接受的**；其余 400 行是**结构匹配**（名字 + 元数一致），没有逐个真跑。这不是漏做，是要说清读数能证明什么：结构性覆盖 ≠ 调用得通；把 400 行全跑一遍是另一件事，其代价与判据要先定。

## 第一族：tensor 的创建与读取（17 行）——写不得，原因是"声明比算子弱"

清单（`inspect --ops`，URI / 元数）：`tensor:zeros(1)`、`tensor:ones(1)`、`tensor:eye(1)`、`tensor:full(2)`、`tensor:from_bytes_aggregate(2)`、`tensor:from_bytes_bf16(2)`、`tensor:from_bytes_bool(2)`、`tensor:from_bytes_f16(2)`、`tensor:from_bytes_f32(2)`、`tensor:from_bytes_f64(2)`、`tensor:from_bytes_i32(2)`、`tensor:from_bytes_i64(2)`、`tensor:load_npy_bool(1)`、`tensor:load_npy_f32(1)`、`tensor:load_npy_f64(1)`、`tensor:load_npy_i32(1)`、`tensor:load_npy_i64(1)`。

试过、也真跑过：把 `stdlib/signatures/tensor.cml:20,26,36` 那三条**作者已经写好的**声明文本（`zeros`、`ones`、`eye`，写法不是我发明的）搬进编译文件 `stdlib/tensor.cml` 的 "creation" 一节。

**它们加载得进去，`inspect --ops` 也照样给行，但全量测试塌了 137 个**：

```
[SUMMARY] total: 8359, pass: 8210, fail: 137, skip: 10, known failures: 2
```

一个例子说清原因（`test/cases/compile/constas/constas_param.cml:12`）：

```
error[SemanticDiag::GenericArgumentsMismatch]: The generic function 'total' cannot be called
with argument types (f32[..]): argument 1 has type 'f32[..]', but its parameter 'x: f32[3]' is
'f32[3]' here ().
 12 |     println(total(3, ones::<f32>([3])))
```

`ones::<f32>([3])` 原来返回 **`f32[3]`**——计算签名会把 `shape` 实参里的**常量宽度**带到结果类型上，所以 `total(3, x: f32[n])` 的 `n` 能绑成 3。而声明只能写 `export func ones::<T>(shape: int[]): T[..]`，**结果退成 `T[..]`，宽度丢了**。137 个失败全是这一类：`constas`、`nn.lr`、`nn.mnist`、`tensor_add_assign`……凡是用 `zeros`/`ones`/`eye` 得到已知形状的地方都断。

所以这一族不是"补声明"能做的：**声明语法写不出"宽度来自 `int[]` 实参"的结果类型**。这正是作者把这几个留在了不编译的参考文件里的原因（`stdlib/tensor.cml` 的头注释说"limit is the generic parameter, not the operator"，今天量到了这一句的具体形状）。

因此 **`zeros`、`ones`、`eye` 从"真缺"移到"签名表达不了"**，原因记作：*结果宽度派生自一个 `int[]` 实参，声明的结果只能写 `T[..]`，会丢掉计算签名已经给的 `f32[3]`*。这行更正有 137 个失败作证，不是推断。

**`full` 同样移到"签名表达不了"**，但原因不同，是编译器直说的：

```
error[SemanticDiag::UnresolvedTypeReference]: Unresolved type reference: 'number'.
export func full(shape: int[], value: number): float[..]
```

`number` 是参考文件里的说明用词，**不是 Camel 的类型名**。

**`from_bytes_*`（8）与 `load_npy_*`（5）卡在通配**：参考文件用一条带 `*` 的声明覆盖它们——

```
#op('tensor:from_bytes_*')
export func from_bytes::<T>(bytes: Bytes, shape: int[]): T[..] when is_plain::<T>()
```

搬进编译文件后：

```
error[SemanticDiag::NativeDeclarationNotLoaded]: The declaration 'from_bytes' names the
operator 'tensor:from_bytes_*', which no loaded module implements.
```

**加载器不展开 `*`**，所以要么写 13 条各自命名的声明（等于新增 13 个用户可见的名字），要么给加载器加通配。两条都是决定。

这一族的账：**0 条写成的声明，4 行更正到"签名表达不了"，13 行卡在通配**。我把搬进去的三条**撤了**（`git checkout -- stdlib/tensor.cml`），全量回到 `8359/8347/0`——理由：一条会让 137 个测试塌掉的声明不能留在树上，哪怕它在 `inspect` 里看着很对。

## 655 与 559 为什么不一样

`inspect --ops` 的 **655** 是**注册表行数**：每个注册重载一行（同一个 URI 可以有计算签名行和声明行两条，这正是 `sum_axis` 的两条）。

`--verify-ops` 的 **559** 是它**逐条比较过的声明条目数**：`scripts/stdlib-reference.mjs` 里的 `checked++` 只在"一条声明配上一条注册算子"时加一，而 `export func operator …` 这类算子声明**按设计整类跳过**（`scripts/stdlib-reference.mjs:735`，也是 `docs/stdlib-reference.md` 里没有算子条目的原因）。

两个数数的不是同一个东西：一个是"注册了多少重载"，一个是"声明和算子对上了多少对"，所以不相等，也没有哪个是错的。今晚"305 诊断码 vs 打印码"那条纪律同理——**先问"它在数什么"，再问"对不对"**。

## 655 与 559 为什么不一样

`inspect --ops` 的 **655** 是**注册表行数**：每个注册重载一行（同一个 URI 可以有计算签名行和声明行两条，这正是 `sum_axis` 的两条）。

`--verify-ops` 的 **559** 是它**逐条比较过的声明条目数**：`scripts/stdlib-reference.mjs` 里那个 `checked++` 只在"一条声明配上一条注册算子"时加一，而 `export func operator …` 这类算子声明**按设计整类跳过**（同 `scripts/stdlib-reference.mjs:735`，也是 `docs/stdlib-reference.md` 里没有算子条目的原因）。

两个数数的不是同一个东西：一个是"注册了多少重载"，一个是"声明与算子对上了多少对"，所以它们不相等，也没有哪个是错的。今晚"305 诊断码 vs 打印码"那条纪律同理——**两个数各自有定义，先问"它在数什么"，再问"对不对"**。

## 对 P1-7 的意义

P1-7 要的是"键从声明读"，而这张表说的是**键有没有声明可读**。结论分三种，`docs/handover/questions/2026-11-02-p1-13-tensor-and-array-same-name-is-it-one-call-shape.md` 逐条问了作者：

- **同一个调用形状已经覆盖的**（`sum`/`mean`/`min`/`max` 等）：不要再写第二条同名声明，缺的是**类型条件**；
- **声明在、类型条件挡着的**（`abs`/`exp`/`log`/`erf`/`erfc`/`pow`/`dim`）：是类型问题，不是声明问题；
- **签名表达不了的**（36 行，含 `reshape`）：把联合/`Tensor` 写进声明是另一个决定（"声明能不能表达 `A | B`"），不是补声明能解决的。

## 全表

下面按三栏展开：真缺 189 行按协议分组逐条列出，签名表达不了 53 行按原因列出；已覆盖按协议与来路给出计数（逐条列出 413 行会让这页变成第二个注册表）。

## 已覆盖 413 行的来路

| 协议 | 靠 tag | 靠同名同元数声明 | 合计 |
|---|---|---|---|
| `:op` | 128 | 0 | 128 |
| `math` | 96 | 0 | 96 |
| `graph` | 9 | 41 | 50 |
| `tensor` | 0 | 44 | 44 |
| `array` | 29 | 4 | 33 |
| `coll` | 19 | 0 | 19 |
| `io` | 9 | 5 | 14 |
| `struct` | 10 | 0 | 10 |
| `str` | 5 | 4 | 9 |
| `mark` | 5 | 0 | 5 |
| `bytes` | 0 | 4 | 4 |
| `stream` | 1 | 0 | 1 |

## 真缺（206 行，按协议）

### :op: 168

- `:op/and` (2 参)
- `:op/assn` (2 参)
- `:op/assn_add_bf` (2 参)
- `:op/assn_add_c` (2 参)
- `:op/assn_add_d` (2 参)
- `:op/assn_add_f` (2 参)
- `:op/assn_add_f6` (2 参)
- `:op/assn_add_i` (2 参)
- `:op/assn_add_l` (2 参)
- `:op/assn_add_s` (2 参)
- `:op/assn_add_z` (2 参)
- `:op/assn_and` (2 参)
- `:op/assn_b` (2 参)
- `:op/assn_d` (2 参)
- `:op/assn_div_bf` (2 参)
- `:op/assn_div_c` (2 参)
- `:op/assn_div_d` (2 参)
- `:op/assn_div_f` (2 参)
- `:op/assn_div_f6` (2 参)
- `:op/assn_div_i` (2 参)
- `:op/assn_div_l` (2 参)
- `:op/assn_div_z` (2 参)
- `:op/assn_f` (2 参)
- `:op/assn_i` (2 参)
- `:op/assn_l` (2 参)
- `:op/assn_mod_i` (2 参)
- `:op/assn_mod_l` (2 参)
- `:op/assn_mul_bf` (2 参)
- `:op/assn_mul_c` (2 参)
- `:op/assn_mul_d` (2 参)
- `:op/assn_mul_f` (2 参)
- `:op/assn_mul_f6` (2 参)
- `:op/assn_mul_i` (2 参)
- `:op/assn_mul_l` (2 参)
- `:op/assn_mul_z` (2 参)
- `:op/assn_or` (2 参)
- `:op/assn_owned` (2 参)
- `:op/assn_pow_bf` (2 参)
- `:op/assn_pow_c` (2 参)
- `:op/assn_pow_c_i` (2 参)
- `:op/assn_pow_c_l` (2 参)
- `:op/assn_pow_d` (2 参)
- `:op/assn_pow_f` (2 参)
- `:op/assn_pow_f6` (2 参)
- `:op/assn_pow_i` (2 参)
- `:op/assn_pow_l` (2 参)
- `:op/assn_pow_z` (2 参)
- `:op/assn_pow_z_i` (2 参)
- `:op/assn_pow_z_l` (2 参)
- `:op/assn_s` (2 参)
- `:op/assn_sub_bf` (2 参)
- `:op/assn_sub_c` (2 参)
- `:op/assn_sub_d` (2 参)
- `:op/assn_sub_f` (2 参)
- `:op/assn_sub_f6` (2 参)
- `:op/assn_sub_i` (2 参)
- `:op/assn_sub_l` (2 参)
- `:op/assn_sub_z` (2 参)
- `:op/atos` (1 参)
- `:op/ctos` (1 参)
- `:op/dtos` (1 参)
- `:op/eq` (2 参)
- `:op/eq_b` (2 参)
- `:op/eq_bf` (2 参)
- `:op/eq_bytes` (2 参)
- `:op/eq_c` (2 参)
- `:op/eq_d` (2 参)
- `:op/eq_f` (2 参)
- `:op/eq_f6` (2 参)
- `:op/eq_i` (2 参)
- `:op/eq_l` (2 参)
- `:op/eq_s` (2 参)
- `:op/eq_z` (2 参)
- `:op/ftos` (1 参)
- `:op/ge_bf` (2 参)
- `:op/ge_d` (2 参)
- `:op/ge_f` (2 参)
- `:op/ge_f6` (2 参)
- `:op/ge_i` (2 参)
- `:op/ge_l` (2 参)
- `:op/ge_s` (2 参)
- `:op/gt_bf` (2 参)
- `:op/gt_d` (2 参)
- `:op/gt_f` (2 参)
- `:op/gt_f6` (2 参)
- `:op/gt_i` (2 参)
- `:op/gt_l` (2 参)
- `:op/gt_s` (2 参)
- `:op/idx_str` (2 参)
- `:op/idx_take` (3 参)
- `:op/inv_i` (1 参)
- `:op/inv_l` (1 参)
- `:op/itos` (1 参)
- `:op/le_bf` (2 参)
- `:op/le_d` (2 参)
- `:op/le_f` (2 参)
- `:op/le_f6` (2 参)
- `:op/le_i` (2 参)
- `:op/le_l` (2 参)
- `:op/le_s` (2 参)
- `:op/lt_bf` (2 参)
- `:op/lt_d` (2 参)
- `:op/lt_f` (2 参)
- `:op/lt_f6` (2 参)
- `:op/lt_i` (2 参)
- `:op/lt_l` (2 参)
- `:op/lt_s` (2 参)
- `:op/ltos` (1 参)
- `:op/mod_i` (2 参)
- `:op/mod_l` (2 参)
- `:op/ne` (2 参)
- `:op/ne_b` (2 参)
- `:op/ne_bf` (2 参)
- `:op/ne_bytes` (2 参)
- `:op/ne_c` (2 参)
- `:op/ne_d` (2 参)
- `:op/ne_f` (2 参)
- `:op/ne_f6` (2 参)
- `:op/ne_i` (2 参)
- `:op/ne_l` (2 参)
- `:op/ne_s` (2 参)
- `:op/ne_z` (2 参)
- `:op/not` (1 参)
- `:op/or` (2 参)
- `:op/pow_bf` (2 参)
- `:op/pow_c` (2 参)
- `:op/pow_c_i` (2 参)
- `:op/pow_c_l` (2 参)
- `:op/pow_d` (2 参)
- `:op/pow_f` (2 参)
- `:op/pow_f6` (2 参)
- `:op/pow_i` (2 参)
- `:op/pow_l` (2 参)
- `:op/pow_z` (2 参)
- `:op/pow_z_i` (2 参)
- `:op/pow_z_l` (2 参)
- `:op/set_field` (3 参)
- `:op/set_index` (3 参)
- `:op/stos` (1 参)
- `:op/tag_error_info` (1 参)
- `:op/tag_error_value` (1 参)
- `:op/tag_is_error` (1 参)
- `:op/tag_is_null` (1 参)
- `:op/tag_null_value` (1 参)
- `:op/tag_throw` (1 参)
- `:op/ztos` (1 参)

### var: 6

- `:var/after` (2 参)
- `:var/get` (1 参)
- `:var/new` (1 参)
- `:var/new/empty` (0 参)
- `:var/peek` (1 参)
- `:var/take` (1 参)

### graph: 6

- `graph:ne` (2 参)

### struct: 3

- `:struct/append_arr_owned` (1 参)
- `:struct/copy_arr` (1 参)
- `:struct/iter_str` (1 参)

### enum: 3

- `:enum/field` (3 参)
- `:enum/make` (2 参)
- `:enum/tag` (1 参)

### macro: 1

- `:macro/cmp` (1 参)

### coll: 1

- `:coll/map_idx` (2 参)

### mark: 1

- `:mark/isolate` (1 参)

## 签名表达不了（36 行，附原因）

### union parameter: 21

- `:coll/eq` (2 参)
- `:coll/neq` (2 参)
- `tensor:add` (2 参)
- `tensor:concat` (3 参)
- `tensor:divide` (2 参)
- `tensor:ge` (2 参)
- `tensor:gt` (2 参)
- `tensor:idx_take` (2 参)
- `tensor:le` (2 参)
- `tensor:lt` (2 参)
- `tensor:maximum` (2 参)
- `tensor:minimum` (2 参)
- `tensor:mod` (2 参)
- `tensor:multiply` (2 参)
- `tensor:one_hot` (2 参)
- `tensor:reshape` (2 参)
- `tensor:strict_eq` (2 参)
- `tensor:strict_neq` (2 参)
- `tensor:subtract` (2 参)
- `tensor:take` (2 参)
- `tensor:where` (3 参)

### the parameter type is `Tensor`, which is no type name: 15

- `tensor:gelu` (1 参)
- `tensor:idx` (2 参)
- `tensor:idx2d` (3 参)
- `tensor:layer_norm` (4 参)
- `tensor:linear` (3 参)
- `tensor:matmul` (2 参)
- `tensor:neg` (1 参)
- `tensor:ones_like` (1 参)
- `tensor:permute` (2 参)
- `tensor:rsqrt` (1 参)
- `tensor:softmax_grad` (3 参)
- `tensor:transpose` (1 参)
- `tensor:unsqueeze` (2 参)
- `tensor:zeros_like` (1 参)

### the result's extents derive from an `int[]` argument, and a declaration writes `T[..]`: 10

- `tensor:from_bytes_aggregate` (2 参)
- `tensor:from_bytes_bf16` (2 参)
- `tensor:from_bytes_bool` (2 参)
- `tensor:from_bytes_f16` (2 参)
- `tensor:from_bytes_f32` (2 参)
- `tensor:from_bytes_f64` (2 参)
- `tensor:from_bytes_i32` (2 参)
- `tensor:from_bytes_i64` (2 参)
- `tensor:ones` (1 参)
- `tensor:zeros` (1 参)

### the result's extents are the file's, known only when the file is read: 5

- `tensor:load_npy_bool` (1 参)
- `tensor:load_npy_f32` (1 参)
- `tensor:load_npy_f64` (1 参)
- `tensor:load_npy_i32` (1 参)
- `tensor:load_npy_i64` (1 参)

### the result's extents derive from an `int` argument, and a declaration writes `T[..]`: 1

- `tensor:eye` (1 参)

### the parameter type is `number`, which is no type name: 1

- `tensor:full` (2 参)
