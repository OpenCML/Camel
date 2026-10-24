# 依赖模型的递归到不动点：裁定落地后的 5 个未定项（H463 交回）

裁定来源：`docs/handover/reviews/questions-2026-10-24-evening.md` §`model-dependent-recursion-unrolls-after-the-flip`
（`origin/fsh-dev`）。落地分支：`handover/h463-spec-fixed-point`，改动只落在
`src/runtime/draft_inline.cpp`。下面每条都带读数，没有读数的一条我也说明了为什么取不到。

探针（已删除，未提交）：`CAMEL_LOG_INFO_S("H463", ...)` 在 `collectDirectFuncSpecializations` 的每个
binding 上；`CAMEL_LOG_INFO_S("H463find", ...)` 在 `findSpecialization` 的每个 miss 出口与
`specializeDirectFuncInDraft` 的计数守卫前。取值命令形状：
`camel -vvvv --log-preset none --log-include H463,H463find <case>.cml`。

## 1. `Tensor` / `[]` 的元素位置算不算「类型变量」

裁定①只点了 extent：「实参的精确长度（字面量 `[k]` 的 `int[1]`）是值的性质，不进实例的键」。
`Tensor` = 元素未定（numeric-unknown）+ 任意秩；`int[]` 的元素是写死的 `int`。元素位置算不算
「声明里的类型变量」，裁定没说。

两种读法的读数（都只在 `draft_inline.cpp`，`int[]←int[1]` 那条 extent 读法两种都一样）：

| 读法 | `ort_loop_values` | `ort_linear_recursion` | `ort_loop_loaded_weights` | `export_loop_values_array` | 新增红 |
|---|---|---|---|---|---|
| A：元素代入，extent 不代入 | 红 | 红 | 红 | 红 | `export_gated_gru_forward` |
| B：元素也不代入 | **绿** | **绿** | **绿** | **绿** | `export_gated_gru_forward`、`export_tree_recursion`、`pass_export_refusals_json` |
| C：第一圈元素代入，第二圈起连元素一起放宽 | **绿** | **绿** | **绿** | **绿** | 三条 gated-GRU 形状（见第 3 条） |

`ort_loop_values`（`shrink5`，autodiff 生成的递归）读法 A 下的探针：同一个 key 反复生成新实例——

```
50  binding kind=0 idx=0 bindsValue=false formal='Tensor' refined='f32[..]'
20  collect FUNC 28 callee='shrink5' root='0' bindings=1 bindsValues=false arm=true cycle=true
12  collect FUNC 71 callee='shrink5$tspec23' root='11' bindings=0 ...
11  collect FUNC 71 callee='shrink5$tspec25' ...   (再 $tspec27 $tspec29 $tspec31 $tspec33 $tspec35)
 5  FUNC 28 callee='shrink5' (id 11, root 11) is not cached; count=8
```

即 key（`Tensor`→`f32[..]`）从头到尾没变，实例却一路造到计数上限 8；`$tspec23..$tspec35` 这条链
就是被展开的 8 层。读法 A 的 miss 原因（`H463find`）：

```
35  miss: no entry for base 'shrink5' (id 11, key rev 44; program holds rev 44)
28  miss: no entry for base 'shrink5' (id 11, key rev 11; program holds rev 11)
```

revision 相同、表里却已经没有条目：每次特化都会重写「调用者自己」的那个调用点，旧的拷贝随之
不可达，commit 的 `eraseIf` 把指向它的条目删掉，下一轮又在同一个 key 上造一个新拷贝。
读法 B 下同一用例：`bindings=0`（`shrink5` 一次都不特化），探针从 242 行降到 35 行，模型正常导出。

**交回的问题**：元素位置到底算不算「声明里的类型变量」？裁定①的「extent 不进键」不足以定这一条，
而四条 ORT 用例的结果完全取决于它。

## 2. 「第二圈」指哪一圈

裁定的字面是：「若同一递归函数沿调用环把 `n` 一路绑成不同的值（多态递归），**第二圈起**把 `n`
放宽为未知，不再生成新实例」。语料里没有任何形参写维度名（`T[n]`），所以能取到的只有「多态递归
沿环再绑」这层意思。我落的判据是：**调用者在调用环上是同一个函数的类型实例**
（`draft.typeSpecializationRoot() == calleeGraph->id()`），从这一圈起实例的形参类型取声明本身。

读数：判据只留 `isValid(callee->typeSpecializationRoot())`（翻转前就有的那条，等于少一半）时，
上面四条 ORT 用例全红；加上「调用者是同一函数的实例」后四条全绿。代价是 gated-GRU 的两条
（`export_gated_gru_step`、`ort_gated_gru_step`）变红（见第 3 条）。

**交回的问题**：判据是「调用者是同一函数的类型实例」，还是「同一个 key 第二次被求值」，
还是别的？这一条决定 gated-GRU 两条的去向。

## 3. `export_gated_gru_forward` 的期望 op 列表被裁定①必然改掉

plan 自己写明了期望形状的来历（`test/plans/feat/modules/onnx.plan.toml`，`export_gated_gru_forward`
的 `desc`）：「H44c 的缓存不拥有自己的图，使第二种形状成为导出器给出的形状：优化器的特化不再
钉在程序里，导出再把延续特化一次，并把下一个 gate 提到臂外」。也就是说期望的
`Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,...` 这条顶层算子表，正是「延续被多特化一次」的产物；
而「多特化一次」靠的正是实参的形状（extent）进 key。裁定①把 extent 移出 key 之后，导出给出的形状
退回另一种（少一次延续特化，gate 留在臂内）：

```
期望： Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,MatMul,...
实得： Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,Slice,Concat,Reshape,Abs,Cast,ReduceMean,Greater,If,...
```

三种读法都会碰掉这一条（A 也会，因为 `Tensor` 的秩不再进 key）。同一族还有 `export_gated_gru_step`、
`ort_gated_gru_step`（读法 C 下失效）。

**交回的问题**：这三条的期望（顶层算子表、If 的个数）是不是随裁定①一起改成新形状？改哪一条由谁
负责——它们是行为断言不是拼写断言，我没有动。

## 4. `export_loop_carried_array` 的断言 `which is a value of type` 与 P4 后的措辞不符

行为已回到 develop：`exit 1`（红前是 `expected exit 1, got 0` + 打印 `exported`，模型被写出来）、
在递归调用处拒绝、点名第 2 个实参、不写模型文件。拒绝文本现在是：

```
onnx.export_model: in 'walk': a recursive call changes argument 2 of a recursion whose depth
depends on the model input, which is an int array whose elements depend on the model input;
a loop carries tensors and scalars and tuples and structs of them, and a carried state with an
array field is refused until native multi-dimensional arrays settle the shapes of fixed-size
arrays (H17 items 2 and 4) (at ...export_loop_carried_array.cml:12:60)
```

plan 断言的第二句是 `which is a value of type`（红前红后都在这句上失败：红前 `missing detail: 1`，
红后 `missing condition: 1`）。同族的 `export_loop_carried_array_field` 断言的却是新措辞
（`desc`：「诊断点名数组字段和缺的东西（原生多维数组，H17 第 2、4 项）」）。`modules/onnx/exporter.cpp`
里 `valueKind()` 先判 `Value::Form::IntArray` 再判常量，所以 P4 之后带数组的叶子一律报新措辞。

**交回的问题**：`export_loop_carried_array` 的第二句断言按 P4 的措辞更新（像 `_field` 那条一样），
还是 `valueKind()` 该先判常量（返回 `a value of type 'int[1]'`，即 develop 的文本）？这是导出器
`exporter.cpp` 的措辞/分支，属 h456 的边界，我没有动。

## 5. 维度名放宽之后诊断怎么报

裁定②说「第二圈起把 `n` 放宽为未知」。语料里没有形参写维度名的递归（`test/cases/**` 里没有
`T[n]` 形参的多态递归），所以「放宽之后，调用点的实参长度和形参未知维度不一致时诊断说什么」
我取不到读数，也没有实现一条只在这种情形下才走的分支。**交回的问题**：这一条要读数才能定，
需要一条形参写维度名的多态递归用例。

## 附：本次落地改了哪两处（都在 `src/runtime/draft_inline.cpp`）

1. `collectDirectFuncSpecializations` 的 `bindType`：实例的形参类型不再取实参的 extent
   （常量长度、开秩都保持声明写的样子），元素位置在第一圈取实参的元素；`relaxNames` 为真时
   连元素一起放宽（第 2 条）。
2. 值绑定在「分支臂 + 调用环」上的擦除保持不变（那是常量展开的停机机制，不是本次的兜底）。

## 6. 计数上限还承重吗：cap 8 → 4096 的对照实测

把 `include/camel/runtime/draft_inline.h` 的上限临时改成 4096（除这一行外代码相同）重跑：

| 用例 | cap=8（读法 C） | cap=4096 | 结论 |
|---|---|---|---|
| onnx 计划红集 | 25 条 | 25 条，逐条 `Compare-Object` **完全一致** | 上限对这四条不承重 |
| `ort_loop_values` / `ort_linear_recursion` / `export_loop_values_array` | 绿 | 绿 | 同上 |
| `ort_loop_loaded_weights` | 绿 3.34 s | 绿 **1.97 s** | 同上 |
| `specialize_growing_shape.fold_specialize.bounded` | `GRAPH_STATS {"graphs":5,"nodes":52,"edges":56,"opers":13,"direct_calls":4,"indirect_calls":1,"closures":0,"branches":3,"loops":0,"bytes":1976,"specializations":1}` | **同一个 JSON，逐字相同** | 与上限无关：天然有界在 5 |
| `export_gated_gru_forward` | `...,If,Slice,Concat,Reshape,...,Identity`（第三种形状） | `Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,MatMul,Add,Mul,Cast,ReduceMean,Identity`，`ifs=7` | **上限仍在承重**：8 是链被截断的产物 |
| `shape_unroll.simplify.rgir` | `callee run$spec3 not straight-line` | 同一条 | 与上限无关 |
| `resident_loop_declares_its_blocks` | 红 | 红：`` `#gpu` scope 'step': operator 'tensor:iter' cannot run on the device (no device implementation) `` | 与上限无关：算子放置变了 |

三条要交回的判断：

1. `export_gated_gru_forward` 在 cap=8 下的红是**上限截断**造成的形状；cap=4096 给出的才是 plan
   `desc` 自己写的「把延续拷进两条臂」那一种（`2^3-1 = 7` 个 If）。按裁定「不靠计数兜底」，
   这一条应当是在 4096 下重推期望，或者由作者定上限该是多少——我没有替作者改上限。
2. `specialize_growing_shape.fold_specialize.bounded` 的断言 `stats.graphs > 5 && stats.graphs <= 40`
   下界在键改完后失效（实得 5，且与上限无关）：它钉的「有界」在新键下天然成立，值从「8 个特化」
   变成 5 个图，下界按新图重推即可（读数在两个上限下一致）。
3. `export_gated_gru_step` / `ort_gated_gru_step` **不是形状断言**，是导出被拒：
   `onnx.export_model: in 'arm2.1.pb': an indirect call remains after simplification`——
   第二圈放宽之后，把这处间接调用解掉的类型实例不再生成。要在第二圈上再收窄，需要作者定
   （varA/varB/varC 三种读法的红集对照在第 1、2 节）。
   `shape_unroll.simplify.rgir` 不是金标文件，是逐图断言「特化后的 forward 及其每个被调用者
   无分支」；红后 `run$spec3` 的被调用者是 `run$tspec6$spec7`（值特化落在类型实例里）。
4. `gpu.tiles resident_loop_declares_its_blocks` 的新红与上限、与形状断言都无关：错误从
   `TILES_RESIDENCY` 的输出差异变成 `` `tensor:iter` cannot run on the device ``，
   即 tiles 的算子放置随图变了；这一条要么随裁定重推放置，要么说明键改动不该影响它。

`kMaxTypeSpecializationsPerRecursiveFunction` 没有被改动，也没有被当作修复使用；读法 A 下它仍然
是那四条 ORT 用例唯一的停机点（`count=8`），这正是裁定要说「不靠计数兜底」的地方。

## 7. 落定的一版：第二圈不再造拷贝，而是用调用者自己

第 6 节之后的读法（记作 E）把第二圈的处理从「形参类型取声明」（表里的读法 C）改成
**「这次调用要的实例就是调用者自己」**：`specializeDirectFuncInDraft` 里当调用者是一个函数自身的
类型实例、绑定全是类型绑定、且这个实例声明的形参类型都接受实参时，把这处调用的被调用者指到
调用者自己（不造新拷贝）；不接受时照旧特化。这一版去掉了 `relaxNames` 的第二圈条款，
`kMaxTypeSpecializationsPerRecursiveFunction` 仍是 8。

红前→红后（整层 fast，基线 fail 295）：

| | 读法 C（上两轮） | 读法 E（本版） |
|---|---|---|
| 整层 fail | 297 | **294** |
| 相对基线 | 减 4、增 6 | **减 4、增 3** |
| `export_gated_gru_step` / `ort_gated_gru_step` | 红（`an indirect call remains after simplification`） | **绿** |
| `specialize_growing_shape.fold_specialize.bounded` | 红（`graphs` 5，我把期望改成了 `=== 5`） | **绿**（`GRAPH_STATS {"graphs":12,...,"specializations":1}` 落在原来的 `> 5 && <= 40` 窗口内，故该条期望**还原成基线原文**，不再需要重推） |
| `shape_unroll.simplify.rgir` | 红 | 红（见下） |
| `export_gated_gru_forward` | 红 | 红（见下） |

`export_gated_gru_forward`：本版 cap=8 下仍是 `Slice,Reshape,Abs,Cast,ReduceMean,Greater,If,
Slice,Concat,Reshape,Abs,Cast,ReduceMean,Greater,If,MatMul,Add,Mul,Cast,ReduceMean,Identity`。
第 6 节测到的 `...,If,MatMul,...` / `ifs=7` 那串是**上一版 + cap=4096** 的读数，没有在本版重测。
要按 7-If 那条重推，就得同时让上限不再截断（本版在这一条上仍会撞到 8）；动不动上限是作者的决定。

`shape_unroll.simplify.rgir` 的性质判定：**不是裁定①的必然后果**。只做裁定①（extent 不进键、
元素照旧按实参取）时这一条是**绿**的（那版的新红里没有它）。它是**「第二圈用调用者自己、不再造
拷贝」**的后果：`run$spec3` 不再是直线，里面有一个 `BRCH`/`JOIN` 和一次 `FUNC run$tspec4`
（它自己的实例），即递归不再被完全展开成一条链：

```
["PORT","t"],["PORT","steps"],["PORT","x"],["OPER","tensor:shape"],["DATA","0"],
["OPER",":op/idx_arr"],["DATA","2"],["OPER",":op/idx_arr"],["OPER",":op/ge_l"],
["JOIN","JOIN"],["BRCH","BRCH"],["DATA","1"],["OPER","tensor:slice"],["DATA","[0, 0]"],
["FILL","int[]"],["OPER","tensor:reshape"],["OPER","tensor:add"],["FUNC","run$tspec4"]
```

而这一版拷贝有界正是裁定要的（「特化总量有界」）。所以这是**两个都成立的要求之间的取舍**：
完全展开（这条用例钉的）与拷贝有界（裁定要的）。两个读数（只做裁定①＝绿、本版＝红）都在这里，
交给作者定，我没有自行重推这条断言。

## 去上限落地后的新读数：`export_fold` 这条递归真的没有不动点（h465 集成 lane）

裁定第 3 条（"8 只是截断"）落地：`kMaxTypeSpecializationsPerRecursiveFunction = 8` 删除，换成不动点
守卫 `kTypeSpecializationFixedPointGuard = 4096`，撞上就 `throw`，**不静默停在某个截断形状**
（`bb4e01be8`）。

- 归属读数：`export_fold` 在翻转批（`228a2ba6a`，73 红那批）**绿**；这次去上限之后才红。它不是翻转
  造成的，是"去上限"造成的。
- plan 口径：`timeout_ms` 抬到 60000 也在 60 s 内不返回（60.04 s 被超时杀掉，`expected exit 0,
  got null`）。我已把它还原成 10000，**测试一行未改**。
- 主机直跑（`/usr/bin/time camel test/cases/modules/onnx/export_fold.cml`）：

  ```
  error[RuntimeDiag::RuntimeError]: ... onnx.export_model: the type specialization of 'unroll'
  did not reach a fixed point: 4096 instances, each with a key the one before it did not have.
    --> test/cases/modules/onnx/export_fold.cml:30:5
  WALL 256.99 s
  ```

  基座同一条 <10 s 且绿。也就是说：无上限读数下 `unroll` 这条递归**没有不动点**（每个实例的键都与
  前一个不同），而裁定第 3 条"键不同 → extent 与维度名放宽为 `?`"在这条路径上**没有生效**。守卫的
  `throw` 是**对的行为**（不静默停、也不截断），但它把一个翻前绿的用例变成红，代价是 257 s。
- 对照（说明守卫不是普遍代价）：同一批里 `specialize_growing_shape.fold_specialize.bounded` 的读数
  是不动点 **1 个实例**（`GRAPH_STATS {"graphs":99,...,"specializations":1}`），问题只在 `unroll`
  这类路径上。

两种终态（作者定）：

- (a) 第 3 条的放宽规则覆盖这条路径（守卫留作安全网）；
- (b) 这条递归的类型确实没有不动点，导出侧应在花掉 257 s 之前用它自己的诊断拒绝它（今天只有守卫
  在 4096 个实例之后事后报错）。