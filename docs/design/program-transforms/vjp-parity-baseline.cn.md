# 求导对照的基线读数：两份求导表差在哪（2026-11-14）

- 状态：**基线**。第六节第 6 路（求导对齐）的第一次读数：库的 `vjp` 推到与引擎对齐，每次合并报台账读数，**引擎冻结**。
- 树：`D:\Projects\Camel-wt\ledger`，分支 `lane-ledger`，基线 `146bb01ba`（= 实时 `origin/develop`）。
- 本轮改动面：**只有本页**。`src/runtime/retype*`、`src/passes/opt/defun/**` 一行未动；这一轮只测量、只立基线。
- 判据：**读数先于动作**——先有一个能重复跑、能对着看的数，再谈"先动哪一类"；否则"对齐了"只是印象。

## 1. 口径

台账已经存在，本页不另造：

| 东西 | 位置 | 干什么 |
|---|---|---|
| 判定 | `scripts/vjp-parity.mjs` | 两份求导表各压成同一个键 `head@arity`，求差，逐条对清单 |
| 清单 | `scripts/vjp-parity-exemptions.tsv` | 每个差一行：标记（算子的名字或 `+ - * / ^` 这类记号）、主人、理由；通配符被拒 |
| 自检 | `test/tools/check-vjp-parity.mjs` | 三道读数：真树 `0 new`；加一条没人答的律必须 exit 1 并点名；把那行登记进清单后必须 exit 0 |
| 闸门 | `test/plans/feat/compile/transforms.plan.toml` 的 `vjp_api.the_two_tables_are_compared` | CI 里跑的是自检，不是判定本身 |

**一句话的口径**：一个键两边都答，或者这个键的差在清单里有一行写着主人和理由——两者都没有的那条差就是欠账（exit 1）。
"一致"是计数，不列举；"不一致"逐行列出，因为那是人要去动的东西。

**两侧怎么读**（`--tables` 的原样排法）：

- 库侧：`stdlib/rulesets/transpose.cml#pow_d_t  left side: pow_d(x, y)`——规则符号与它左边的写法。
- 引擎侧：`tensor:conv2d: modules/nn/tensor_ops.cpp (setVjp, line 849)`——URI 与注册点，`setVjp` 或 `setRule` 分清。
- 差就是"只有一边有这一行"：`the engine's table: no row reduces to pow_d@2` / `the library's table: no rule files conv2d`。

**单位是键，不是数。** 两侧的"读数"是"谁把这个键归档"对"谁注册了这个键"，不是两个浮点数。程序级的数值对照是另一张表
（[2026-11-04 的 11 项逐条重算](../../handover/reports/2026-11-04-where-the-library-and-the-engine-still-differ.cn.md)），
本页不与它混用：那张表回答"两条腿跑同一个程序各答什么数"，这张回答"两份表差哪些键"，混在一起会让"读数变了"分不清是数变了还是键变了。

四条命令，全在树根跑，不建树、不跑计划，只读源码：

```powershell
node scripts/vjp-parity.mjs            # 判定：0 new 即绿；有未登记的差 exit 1；树读不了 exit 2
node scripts/vjp-parity.mjs --tables   # 三张表：both / library-only / engine-only，每条差一行
node scripts/vjp-parity.mjs --json     # 同样的三张表，机器可读（本页的数字就是从这里数的）
node test/tools/check-vjp-parity.mjs   # 三道自检，本轮实跑：3/3 PASS
```

## 2. 本轮读数

判定的原样一行：

```text
PASS the two derivative tables differ only where the list says so: 103 law(s) filed under 54 key(s),
116 engine registration(s) under 100 key(s), 48 key(s) both answer, 6 library-only and 52 engine-only
exempt, 0 new
```

| 数 | 值 | 意思 |
|---|---|---|
| 库的律 | 103 条 | 两个规则集（`transpose.cml` + `jvp.cml`）的 `rule` 声明 |
| 库的键 | 54 个 | 这些律归档在多少个 `head@arity` 上 |
| 引擎的注册 | 116 个 | `setRule` + `setVjp`，含模板注册按实例化展开（`F64`/`F32` 是两条） |
| 引擎的键 | 100 个 | 含 `arity=null` 的（名字在规则集里没有元数可读） |
| **一致** | **48 键** | 两边都答，同一件事两条腿都能走 |
| **不一致** | **58 行** | 6 行库有引擎无 + 52 行引擎有库无 |
| 其中未登记（欠账） | **0 行** | 这就是这张表绿的唯一意思 |
| 清单行数 / 陈旧 | 80 行 / **24 行** | 清单比树多 24 行：这些差已经不存在了，行可以删 |

58 行按"键"算是 **56 个差**（`argmax`、`argmin` 各两行：整张量的和带轴的两种拼法共用一个标记）。
80 − 56 = 24，就是那 24 行陈旧项的来源。

## 3. 不一致的分组

| 组 | 行数 | 差在哪一边 | 能不能搬 |
|---|---|---|---|
| A 库有、引擎真的缺一行 | 2 | 两半的律库里都写了，引擎一行没有 | **能，且只差一格 `setRule`** |
| B 库有、引擎无：stated 拼法 | 4 | `*_stated` 是给残差带 extents 的写法，引擎走普通算子 | 不搬：这是库的设计，不是缺口 |
| C 引擎有、库无：只由规则造出来的机器算子 | 10 | `:tangent/*` 5 + `:struct/*` 5 | 不搬：程序写不出来，只有规则建它们 |
| D 引擎有、库无：张量模块的内部算子 | 5 | `sum_to` `broadcast_like` `expand_axis` `cast_like` `permute_inverse` | 不搬：引擎自己的规则写它们 |
| E 引擎有、库无：没有切空间的值 | 14 | 比较、离散转换、形状/标签读 | 不搬：`Tangent<i32>` 是 `()`，没有东西可搬 |
| F 引擎有、库无：前向半没有 `jvp_*` | 23 | 库在 lift 就停住，反向的问题根本没问到 | **能，但这才是要排队的欠账** |

2 + 4 + 10 + 5 + 14 + 23 = 58。A 与 F 是两条能动的方向，B/C/D/E 是账，不是债。

每组一个代表例子，两侧逐行对照（`--tables` 的原样行）：

**一致（48 个键，一个代表）**——`+@2`，两侧都答：

```text
  both: +@2
      library: stdlib/rulesets/transpose.cml#add_t        left side: x + c
               stdlib/rulesets/jvp.cml#jvp_add            left side: x + w
      engine:  :op/add_d: src/builtin/operators/derivative.cpp (setRule)
```

**A（2 行）**——`pow_d@2`，库的两半都写了律，引擎没有 `math:pow_d` 这一行：

```text
  pow_d@2  [exempt]                                  owner lane-missinglaws
      stdlib/rulesets/transpose.cml#pow_d_t  left side: pow_d(x, y)
      stdlib/rulesets/jvp.cml#jvp_pow_d      left side: pow_d(x, y)
      the engine's table: no row reduces to pow_d@2
```

同组另一行是 `log_d@1`（`stdlib/rulesets/transpose.cml#log_d_t`，引擎无 `log_d`）。

**B（4 行）**——`sum_axis_stated@4`，库为了把 extents 当残差带过去，把和写成了带轴带 extents 的原语；引擎答同一个程序走 `sum`：

```text
  sum_axis_stated@4  [exempt]                        owner lane-axistated
      stdlib/rulesets/transpose.cml#sum_axis_stated_t  left side: sum_axis_stated(t, axis, keepdims, extents)
      the engine's table: no row reduces to sum_axis_stated@4
```

同组：`mean_stated@2`、`max_axis_stated@5`、`min_axis_stated@5`。

**C（10 行）**——`:tangent/zeros_arr`，微分本身要用、程序写不出来的算子：

```text
  tangent/zeros_arr  [exempt]                        owner engine-autodiff
      :tangent/zeros_arr: src/builtin/operators/derivative.cpp (setRule)
      the library's table: no rule files tangent/zeros_arr
```

**D（5 行）**——`sum_to`，引擎规则自己写的降形：

```text
  sum_to  [exempt]                                   owner engine-autodiff
      tensor:sum_to: modules/tensor/ops/gradient_ops.cpp (setVjp)
      the library's table: no rule files sum_to
```

**E（14 行）**——`numel`，答一个整数，没有切空间：

```text
  numel  [exempt]                                    owner engine-autodiff
      tensor:numel: modules/tensor/ops/gradient_ops.cpp (setVjp)
      the library's table: no rule files numel
```

**F（23 行 = 16 核心 + 7 `nn`）**——`layer_norm`，程序写得出来，引擎答得出，库的前向半没有它的律：

```text
  layer_norm  [exempt]                               owner lane-rulesets
      tensor:layer_norm: modules/tensor/ops/reduction_ops.cpp (setVjp)
      the library's table: no rule files layer_norm
```

F 的 16 个核心键：`argmax` `argmax@1` `argmin` `argmin@1` `concat` `idx_take` `layer_norm` `permute` `pow` `slice_sub`
`unsqueeze` `erf_d` `erf_f` `erfc_d` `erfc_f` `sqrt_d`；7 个 `nn`：`avg_pool2d` `batch_norm_train` `conv2d` `embedding`
`max_pool2d` `softmax_cross_entropy` `softmax_cross_entropy_labels`。
树里写这些算子的程序（静态可查）：`test/cases/compile/transforms/vmap_argmax.cml`、`test/cases/modules/nn/conv2d_grad_sanity.cml`、
`test/cases/modules/tensor/grad_f64_width.cml`（`layer_norm`）、`test/cases/modules/tensor/linalg_layout.cml`（`permute`/`unsqueeze`）。

## 4. 下一轮先动哪一类

顺序的理由只有一条：**每一笔都要让某一行从"差"变成"both"，读数因此会动**；B/C/D/E 一行都不该动（它们按构造永不搬运），
所以 58 行里真正可以往下减的是 A 的 2 行 + F 的 23 行 = 25 行。

1. **先把 F 里的 4 行归错类**：`argmax` `argmax@1` `argmin` `argmin@1` 答的是整数索引，`Tangent<int>` 是 `()`——它们与
   `numel` `dim` `shape` 是同一类（E），不是"库的前向半走不到"。只改清单的分类与理由，不动代码，F 就从 23 行变成 19 行。
   **先做这个，因为分类错了的话，后面每一笔都会照着错的名单排队。**
2. **A 的 2 行**（`pow_d@2`、`log_d@1`）：库的两半都写了律，引擎一格 `setRule` 就能答（`modules/math/module.cpp` 的注册处，
   不在冻结范围）。这是 58 行里唯一"一行代码 + 一条已有的律 → 一个程序从拒绝变成答案"的形状；剂量最小，读数动得最干净。
   这两行随之作废（变成陈旧项，要一并删）。
3. **F 的 16 个核心键**，按"程序写得出来、两条腿都能加对照"排：`concat` `permute` `unsqueeze` `slice_sub` 先（改形状与拼接，
   库的前向侧今天明说"states a reshape"，先确认这句话在 `permute` 上还成立），再 `layer_norm` `pow`，最后 `math:` 那 5 个拼法
   （`erf_d` `erf_f` `erfc_d` `erfc_f` `sqrt_d`）。**最后这 5 个不是工作量问题，是先要回答一个结构问题**：`math:` 是规则集
   今天一条规则也没写过的命名空间，先要有"规则集能不能给一个模块的拼法写律"的决定。
4. **7 个 `modules/nn` 键放最后**：规则集不 `import` 任何 `nn`，对齐它们要先定"规则集看不看得见 `nn`"。这与第 3 条最后那 5 个
   是同一个问题的两个面，先答一次，两处一起动。
5. **24 行陈旧项随手删，但不单独成一笔**：它们描述的差已经不存在（`abs` `log` `sqrt` `maximum` `minimum` `where` `neg`
   `divide` `gelu` `linear` `erf` `erfc` `rsqrt` `transpose` `sum_axis` `max_axis` `min_axis` `mean` `argmax_axis` `argmin_axis`
   `stop_gradient` `pick_at` 等），删掉让清单等于树；单独删的合并只是把账本变短，不会让任何一行从差变成 both。

## 5. 复现

```powershell
cd D:\Projects\Camel-wt\ledger
node scripts/vjp-parity.mjs --json > vjp.json     # 本页所有数字都从这一份数出来
node scripts/vjp-parity.mjs | Select-Object -Last 1   # 第 2 节那一行
node scripts/vjp-parity.mjs --tables                  # 第 3 节每一块的原样行
node test/tools/check-vjp-parity.mjs                  # 3/3 PASS
```

**没有随机性**：脚本只读 `src/`、`modules/`、`stdlib/rulesets/` 与清单，输出顺序是排序后的键，同一个树跑两次逐字相同
（本轮连跑两次，两行都逐字一致）。唯一会动数字的是树的源码，不是运行环境——这也是它可以每次合并都报的原因。

一处已知的、不属于本路范围的现象：被导入模块里**规则右值是闭包**时，它的 `__rhs` 图签名退出类型不在 `TypeTable` 里
（`typeId()==0`）。它会让**数值**读数出"类型相关的不一致"，但不会动本页的键级数字（这个台账不跑程序、不看类型）。
另一个车道在修源头，本路不碰。