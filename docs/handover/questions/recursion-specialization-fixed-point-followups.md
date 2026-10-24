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

`kMaxTypeSpecializationsPerRecursiveFunction` 没有被改动，也没有被当作修复使用；读法 A 下它仍然
是那四条 ORT 用例唯一的停机点（`count=8`），这正是裁定要说「不靠计数兜底」的地方。