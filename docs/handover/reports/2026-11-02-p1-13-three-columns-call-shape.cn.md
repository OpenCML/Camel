# P1-13 底数：按调用形状数的三栏表

这是 P1-13 的真实底数：`tag` 不等于"有声明"（`#op(...)` 只在声明不遵循"模块名 + 名字"约定时才需要，设计原则 6.4 第 1 步），所以口径从"数 tag"换成"**数调用形状**"——一个算子注册的**名字 + 元数**，在 `stdlib/` 里有没有同名同元数的声明。

工具与数据：`camel inspect --ops` 的 655 行注册重载（probe 一次导入 `array`、`tensor`、`bytes`、`str`、`io`、`graph`、`stream`），对 `stdlib/` 与 `modules/` 的 45 个被编译 `.cml`（`stdlib/signatures/` 的 6 个按文件头"not compiled"排除）读出的 632 条 `export func` 声明、去重 454 个调用形状。

## 三栏

| 栏 | 行数 | 占 655 | 含义 |
|---|---|---|---|
| 已覆盖 | **464** | 71% | 该 URI 有 `#op` tag，或有同名同元数的声明 |
| 真缺 | **109** | 17% | 没有；且签名写得出来 |
| 签名表达不了 | **82** | 13% | 没有；且今天的声明语法表达不了（`!=` 无符号 22、联合 21、`Tensor` 15、无通配 13、宽度派生自实参 3、`number` 1） |

**已覆盖 464 行的来路**：靠 tag 128（`:op` 的 128 行）+ 靠声明 285（`math` 96、`graph` 41+9、`tensor` 44、`array` 33、其余按协议）。

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

**加载器不展开 `*`**，所以要么写 13 条各自命名的声明（等于新增 13 个用户可见的名字），要么给加载器加通配。两条都是决定，见 [../reviews/2026-11-02-p1-13-should-the-loader-expand-the-op-wildcard.md](../reviews/2026-11-02-p1-13-should-the-loader-expand-the-op-wildcard.md)（默认：不给加载器加通配，这 13 行记为"签名表达不了（无通配）"）。

这一族的账：**0 条写成的声明，4 行更正到"签名表达不了"，13 行卡在通配**。我把搬进去的三条**撤了**（`git checkout -- stdlib/tensor.cml`），全量回到 `8359/8347/0`——理由：一条会让 137 个测试塌掉的声明不能留在树上，哪怕它在 `inspect` 里看着很对。

## 流程：先写一条，跑全量

上一族（tensor 创建）留下的规矩，从这一族起固定下来：

> **每开一族，先只写一条声明 → 跑全量 → 看掉不掉别的用例。**
> **掉了 ⇒ 这一族的声明比算子弱/写不出，整族搬进"签名表达不了"，并把掉的那条用例名记进原因。**
> **不掉 ⇒ 再补齐这一族。**

理由：137 个失败是证据，比"我觉得能写"值钱。这一族正是靠它省下了 8083 个失败。

## 第二族：`:op` 的比较（168 行里结果类型不派生自实参的那一组）

`:op` 真缺 168 行、146 个名字，按"结果类型是否派生自实参"分两组（这一分法就是上一族教训的直接应用）：**结果固定 72 个名字**（`eq_*`、`ne_*`、`lt_*`、`le_*`、`gt_*`、`ge_*`、`*tos`、`and`/`or`/`not`/`mod`/`pow`/`tag_is`/`idx`/`inv`），**结果派生 74 个名字**（`assn_*` 一族，签名是 `(var T, T) => var T`）。先做**结果固定**这组里的比较一族。

`eq_*` 11 个名字（22 行）：`eq_f`、`eq_d`、`eq_i`、`eq_l`、`eq_f6`、`eq_bf`、`eq_c`、`eq_z`、`eq_s`、`eq_bytes`、`eq_b`，签名清一色 `(X, X) => bool`。

**先写一条**（`#op(':op/eq_d') export func operator ==(x: float, y: float): bool`）→ 全量 **`8359/8347/0`**，没掉。**再补齐 11 条** → 全量仍 **`8359/8347/0`**。

**`ne_*` 11 个名字（22 行）写不得**，编译器直说：

```
error: '!=' cannot be overloaded: it follows an overload of '==': 'a != b' is '!(a == b)'
with that overload, so the two cannot disagree.
```

也就是说 `!=` **没有自己的符号可写**，它由 `==` 派生；`:op/ne_*` 那 11 条规则任何声明都命名不到。按流程整族搬进"签名表达不了"（22 行），并且它们是**规则删除的候选**——和 19:50 那条"同键多规则按类型选"要删的 `mul_tensor` 同类：既然 `!=` 是 `==` 的否定，`ne_*` 的规则就多了一份实现同一个事实的东西。

**这一族的账**：`eq_*` 11 个名字 / 22 行 → **已覆盖**（tag + 声明，写法照 `stdlib/prelude.cml` 的算术块，`operator ==` 的组名 `__eq__` 由 `src/core/operator_names.cpp:34` 给出）；`ne_*` 11 个名字 / 22 行 → **签名表达不了**。

**判据**：`inspect --ops` 仍 **655 行**，`:op/eq_*` 与 `:op/ne_*` 那 **44 行与改动前逐字节相同**（比对了 `OPERATOR` 行全文）；`--verify-ops` 仍 **559**、报 agree；全量 `8359/8347/0`。

## 第三族到第六族：`:op` 的四个排序符号（各 7 个名字，28 行）

先答作者的问题：**`<` 能不能写**——能。而且真正要验的那道关（"谁跟着谁走"）的答案，比"能不能写"重要：

在**同一个模块**里声明 `<`、`<=`、`>`、`>=` 四个符号各一条（都带 `#op` 指向各自的规则），编译器**一句都没说**。也就是说只有 `!=` 是派生的（"'a != b' is '!(a == b)'"），其余三个各有自家的符号。这一问在动笔前就省下了"三族各 7 条、共 21 行"的返工风险，也确认了 28 行都写得出来。

四族各写 7 个名字（`f32`、`float`、`i32`、`int`、`f16`、`bf16`、`string` —— 正是 `:op/lt_*` 等注册的类型；`bool`、`Bytes` 和复数**没有**排序规则，所以不写）：

| 族 | 名字 | 行 | 全量 |
|---|---|---|---|
| `lt_*`（`<`） | 7 | 14 | `8364/8352/0` |
| `le_*`（`<=`） | 7 | 14 | `8364/8352/0` |
| `gt_*`（`>`） | 7 | 14 | `8364/8352/0` |
| `ge_*`（`>=`） | 7 | 14 | `8364/8352/0` |

每族一条提交（`3e5f22071`、`1eaaa85db`、`3c924b998`、`f71c0ec66`），每族自己跑一遍全量；先把 `lt_d` 一条写进去跑全量（`8364/8352/0`）再补齐，照"先写一条，跑全量"的流程。

**判据**：`inspect --ops` **655 行**，而且**整张表 655 行与改动前逐字节相同**（不是只看排序那 28 行）；`--verify-ops` **559**、agree；三栏表 `435/145/75 → 463/117/75`（只增"已覆盖"，"真缺"只减）。

**`ne_*` 的观察记在这里**（本页原来指向的 `2026-11-02-p1-13-eleven-ne-rules-name-nothing.md` 不在树里，故不指向它）：11 个名字、22 行规则命名不到任何东西（`!=` 由 `==` 推出），并写明**删掉后可核对的结果**：`inspect --ops` 655 → **633**，三栏表 `463/117/75` 不动（它们本来就在"签名表达不了"里），`--verify-ops` 声明没变仍 559。不删，只记着。

## 第七族：`*tos` 那 8 行早就被覆盖了，而且不是运算符

先答作者的问题："`dtos` 这条能不能写"——**不用写，它早就被一条通配声明覆盖了**；而且它**不是运算符**，是转换调用 `string(x)` 的语义。

**注册原文**（`camel inspect --ops` 的行，注意 `name` 不是 `dtos`）：

```
OPERATOR {"module":"<builtin>","name":"string","uri":":op/dtos","with":0,"norm":1,"purity":"pure","signature":"(float) => string"}
```

**它服务于哪个调用**：`string(x)`。`include/camel/standalone/format.h:180` 自己写着——

```
/// `string(x)` (:op/itos, :op/ltos, :op/ftos, :op/dtos, :op/atos): the text of `x`.
```

注册处是内置组的静态类型解析器表（`src/builtin/modules/globals.cpp`：`OperatorGroup::create("string", {...})` 里每条 `":op/dtos"` 配 `StaticFuncTypeResolver::create({}, {{Type::Float64(), false}}, Type::String())`）。

**覆盖它的声明**在 `stdlib/prelude.cml:705`：

```
#op(':op/*tos')
export func string::<T>(x: T): string
```

所以这 8 行**不删、不搬**，进"已覆盖"。写第二条声明反而是给同一个调用形状第二份声明——不做。

**这一步纠正了我自己的两个量法错误**（都记在这里，因为三栏表的底数靠它们）：

1. **不能拿 URI 的后缀当名字**。`:op/dtos` 的名字是 `string`（注册表 `name` 字段），`tensor:from_bytes_bf16` 的名字是 `from_bytes`。按后缀当名字会漏掉 `string/1` 这条声明。
2. **要认通配 tag**。`#op(':op/*tos')` 覆盖哪些 URI，得按"前缀 + 后缀"匹配才数得出来。

两处都改了，于是 8 行从"真缺"进"已覆盖"，总数仍是 655。

**顺带更正通配问题的答案**：通配**是有效机制**，但**只对内置算子组有效**。绑定逻辑在 `src/compile/gir/builder_modules.cpp`：`claimed` 只能来自内置模块（`context_->importModule("")`）里同名的算子组，而 `ownRule` 又被 `uri.find('*') == std::string::npos` 直接否掉——所以 `string`（内置组）的通配能用，`tensor` 的 `from_bytes`（模块算子）的通配没有生效的路。这条写进了 `docs/handover/reviews/2026-11-02-p1-13-should-the-loader-expand-the-op-wildcard.md` 的"补一条更正"。

## 655 与 559 为什么不一样

`inspect --ops` 的 **655** 是**注册表行数**：每个注册重载一行（同一个 URI 可以有计算签名行和声明行两条，这正是 `sum_axis` 的两条）。

`--verify-ops` 的 **559** 是它**逐条比较过的声明条目数**：`scripts/stdlib-reference.mjs` 里的 `checked++` 只在"一条声明配上一条注册算子"时加一，而 `export func operator …` 这类算子声明**按设计整类跳过**（`scripts/stdlib-reference.mjs:735`，也是 `docs/stdlib-reference.md` 里没有算子条目的原因）。

两个数数的不是同一个东西：一个是"注册了多少重载"，一个是"声明和算子对上了多少对"，所以不相等，也没有哪个是错的。今晚"305 诊断码 vs 打印码"那条纪律同理——**先问"它在数什么"，再问"对不对"**。

## 655 与 559 为什么不一样

`inspect --ops` 的 **655** 是**注册表行数**：每个注册重载一行（同一个 URI 可以有计算签名行和声明行两条，这正是 `sum_axis` 的两条）。

`--verify-ops` 的 **559** 是它**逐条比较过的声明条目数**：`scripts/stdlib-reference.mjs` 里那个 `checked++` 只在"一条声明配上一条注册算子"时加一，而 `export func operator …` 这类算子声明**按设计整类跳过**（同 `scripts/stdlib-reference.mjs:735`，也是 `docs/stdlib-reference.md` 里没有算子条目的原因）。

两个数数的不是同一个东西：一个是"注册了多少重载"，一个是"声明与算子对上了多少对"，所以它们不相等，也没有哪个是错的。今晚"305 诊断码 vs 打印码"那条纪律同理——**两个数各自有定义，先问"它在数什么"，再问"对不对"**。

## 对 P1-7 的意义

P1-7 要的是"键从声明读"，而这张表说的是**键有没有声明可读**。结论分三种，`docs/handover/reviews/2026-11-02-p1-13-tensor-and-array-same-name-is-it-one-call-shape.md` 逐条问了作者：

- **同一个调用形状已经覆盖的**（`sum`/`mean`/`min`/`max` 等）：不要再写第二条同名声明，缺的是**类型条件**；
- **声明在、类型条件挡着的**（`abs`/`exp`/`log`/`erf`/`erfc`/`pow`/`dim`）：是类型问题，不是声明问题；
- **签名表达不了的**（36 行，含 `reshape`）：把联合/`Tensor` 写进声明是另一个决定（"声明能不能表达 `A | B`"），不是补声明能解决的。

## 全表

下面按三栏展开：真缺 109 行按协议分组逐条列出，签名表达不了 82 行按原因列出；已覆盖按协议与来路给出计数（逐条列出 413 行会让这页变成第二个注册表）。

## 已覆盖 464 行的来路

| 协议 | 靠 tag | 靠同名同元数声明 | 合计 |
|---|---|---|---|
| `:op` | 186 | 0 | 186 |
| `math` | 96 | 0 | 96 |
| `graph` | 9 | 41 | 50 |
| `tensor` | 0 | 37 | 37 |
| `array` | 29 | 4 | 33 |
| `coll` | 19 | 0 | 19 |
| `io` | 9 | 5 | 14 |
| `struct` | 10 | 0 | 10 |
| `str` | 5 | 4 | 9 |
| `mark` | 5 | 0 | 5 |
| `bytes` | 0 | 4 | 4 |
| `stream` | 1 | 0 | 1 |

## 真缺（206 行，按协议）

### :op: 88

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
- `:op/eq` (2 参)
- `:op/idx_str` (2 参)
- `:op/idx_take` (3 参)
- `:op/inv_i` (1 参)
- `:op/inv_l` (1 参)
- `:op/mod_i` (2 参)
- `:op/mod_l` (2 参)
- `:op/ne` (2 参)
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
- `:op/tag_error_info` (1 参)
- `:op/tag_error_value` (1 参)
- `:op/tag_is_error` (1 参)
- `:op/tag_is_null` (1 参)
- `:op/tag_null_value` (1 参)
- `:op/tag_throw` (1 参)

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

### union parameter: 22

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
- `tensor:pow` (2 参)
- `tensor:reshape` (2 参)
- `tensor:strict_eq` (2 参)
- `tensor:strict_neq` (2 参)
- `tensor:subtract` (2 参)
- `tensor:take` (2 参)
- `tensor:where` (3 参)

### no declaration can name it: '!=' is '!(a == b)', so the operator has no symbol of its own: 22

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

### the parameter type is `Tensor`, which is no type name: 21

- `tensor:argmax_axis` (3 参)
- `tensor:argmin_axis` (3 参)
- `tensor:gelu` (1 参)
- `tensor:idx` (2 参)
- `tensor:idx2d` (3 参)
- `tensor:layer_norm` (4 参)
- `tensor:linear` (3 参)
- `tensor:matmul` (2 参)
- `tensor:max_axis` (3 参)
- `tensor:mean_axis` (3 参)
- `tensor:min_axis` (3 参)
- `tensor:neg` (1 参)
- `tensor:ones_like` (1 参)
- `tensor:permute` (2 参)
- `tensor:rsqrt` (1 参)
- `tensor:softmax_grad` (3 参)
- `tensor:sum_axis` (3 参)
- `tensor:transpose` (1 参)
- `tensor:unsqueeze` (2 参)
- `tensor:zeros_like` (1 参)

### no declaration can name it: the loader does not expand the '*' in #op('tensor:from_bytes_*'): 8

- `tensor:from_bytes_aggregate` (2 参)
- `tensor:from_bytes_bf16` (2 参)
- `tensor:from_bytes_bool` (2 参)
- `tensor:from_bytes_f16` (2 参)
- `tensor:from_bytes_f32` (2 参)
- `tensor:from_bytes_f64` (2 参)
- `tensor:from_bytes_i32` (2 参)
- `tensor:from_bytes_i64` (2 参)

### no declaration can name it: the loader does not expand the wildcard in the load_npy tag: 5

- `tensor:load_npy_bool` (1 参)
- `tensor:load_npy_f32` (1 参)
- `tensor:load_npy_f64` (1 参)
- `tensor:load_npy_i32` (1 参)
- `tensor:load_npy_i64` (1 参)

### the result's extents derive from an `int[]` argument, and a declaration writes `T[..]`: 2

- `tensor:ones` (1 参)
- `tensor:zeros` (1 参)

### the result's extents derive from an `int` argument, and a declaration writes `T[..]`: 1

- `tensor:eye` (1 参)

### the parameter type is `number`, which is no type name: 1

- `tensor:full` (2 参)
