# 一问：同一个算子有两条身份（`array:` 与 `tensor:`）

**问题**：`array:argmax_axis` 与 `tensor:argmax_axis` 是两个已注册的 `OperId`（实测 340 / 422，语义声明挂在 422 上），
图里的节点带 340——**哪一个是正名？另一个该消失，还是该作为别名存在？**

## 一、读数（原文，未加工）

```
TILES_OPERATOR_ID array:argmax_axis id=340 tensor_spelling=422
TILES_OPERATOR_ID tensor:reshape id=406 tensor_spelling=422
```

取数方式：`camel <case> tiles::test_outline_compiled std::nvm`，`CAMEL_TILES_TEST_REASONS=1`，
作用域是 `f32[2,3] → int[2]` 的 argmax；主机无卡，读数与设备无关（pass 只读程序与表）。

`tensor:reshape` 那一行是**对照**：它的 `id=406` 与 `tensor_spelling=422` 不同也说明不了什么——那只是
"另一个 URI 的 id"。真正的判据是下面这条：`array:argmax_axis` 与 `tensor:argmax_axis` **都 `lookup` 得到**，
**不是一个名字的两种拼法被共用，而是两条各自存在的身份**。

## 二、为什么不是版本问题、也不是注册遗漏

区域分析对 `array:argmax_axis` 的回答是 `semantic_declaration`，而 `graph_regions.cpp:142` 是
`!decl || decl->key.major != 1`——两项分开打印的读数是：

```
TILES_OPERATOR_DECLARATION array:argmax_axis undeclared major=-1
TILES_OPERATOR_DECLARATION tensor:reshape declared major=1
```

- **`undeclared`**：340 上**没有**语义声明（`major=-1` 表示"无声明"，不是"版本是 -1"）。
- 也就是说命中的是 `!decl` 那半句，**不是** `major != 1`。若只是版本不对，会读到 `declared major=2` 之类。
- 同时，`tensor:argmax_axis`（422）**在表里**、名字可查——所以既不是"忘了注册"，也不是"注册了但版本旧"，
  而是**声明挂在另一条身份上**。

## 三、它同时影响哪三处

1. **进表**：`modules/tiles/src/graph_regions.cpp#operations` 用哪个 id，决定了任何 device 程序认哪条身份。
   写 `tensor:argmax_axis` 拿到的是一个**没有节点携带**的名字（实测：表里有它，作用域照旧
   `unsupported_operator`）；写 `array:argmax_axis` 才查得到，但它没有声明。
2. **`onnx::export`**：导出的算子名来自 `OperatorTable` 的 URI。两条身份意味着同一个算子可能以两个名字
   出现在导出物里，下游读到的名字取决于这条链路上哪个模块先 intern。
3. **`std::jgir`**：JSON GIR 里的算子名同理，是 URI，不是函数名。

**在区域侧加一层 340→422 的映射，是掩盖**：它让 tiles 知道这件事，而进表与导出两处继续各说各话，
第三处成了唯一知道真相的地方——下一次有人改导出，会再发现一遍。

## 四条候选出路与代价

- **(a) `array:` 是正名。** `tensor:` 那批要么改名、要么消失；21 条 B 组表项与导出、GIR 的算子名要一起动，
  影响面最大，但语义最干净（Camel 表面用哪个模块声明，算子就是哪个模块的）。
- **(b) `tensor:` 是正名。** `array.cml` 的 `#op('array:argmax_axis')` 要改成同一个 id（声明处直接引用
  `tensor:` 的名字），或者让 `#op` 支持"同一算子的第二个拼写"并由表保证指向同一 id。改动最小，
  但需要 `#op` 语义上说得通。
- **(c) 保留两条身份，明确它们是两个不同算子。** 那就必须解释二者的语义差异；今天没有差异（同名、同律、
  同主机实现），所以这条路等于把不一致固定下来。
- **(d) 等 A 组之后再说。** 代价是 A 组最后一条（`argmax_axis`）无法进表——它的读数已经是"声明不在节点
  携带的那条身份上"，这一条不裁，进表那笔就永远缺"declared major=1"。

## 我的建议

**优先 (b)**，理由是可验证且影响面小：Camel 侧 `stdlib/array.cml` 已经声明了 `argmax_axis`，用户写的是
`argmax`，节点带 `array:` 是自然结果；而语义声明这一类东西一直归 `tensor:` 模块所有（`semantics.cpp#declarations`
是唯一声明表）。让 `#op` 声明指向 `tensor:` 的 id，等于承认"标准库在 array 里声明、实现在 tensor 里"，
命名不需要迁移 21 条表项。

若你认为 `array:` 才是正名（因为用户看见的是 array 表面），选 (a) 也自洽，但要连同导出一起改，并且
21 条 B 组表项会跟着换名——那是另一趟列车的量。

**但 (b) 不是"只改一行"**：`array:` 那个拼写要么消失（连带 `stdlib/array.cml` 里所有用到它的地方），
要么成为别名；若成为别名，**"`#op` 允许两个拼写指向同一个 id"这件事本身就要作者裁**——今天 `#op`
的语义是"这个函数名对应这个算子"，没说一个算子可以有两个声明名。

## 取读数的一条临时手法（下一个人会想重复它）

查"为什么进不了区域"时，若 pass 只在成功时沉默，**可以临时把计划里的断言写成一个必然为假的值**，
让运行器把整段 stdout 连同分析读数一起打印出来；读到真值后**立刻把断言改回真实值**，
它只用于取数，不许留在计划里。本页所有读数都是这样取的，最终断言钉的都是真实值。

**无论选哪条，判据都是同一条读数**：`TILES_OPERATOR_DECLARATION <节点携带的那个 URI> declared major=1`。
在这一行出现之前，A 组最后一条不进表（缺口停在 26、`operations()` 停在 21）。
## 四、第三处（导出）的读数：名字取自 URI，而 lowering 表键的是另一条身份

三条读数，同一个程序（`f32[2,3]` 上 `argmax<1>()` 与 `reshape<[3,2]>`），主机无卡：

1. **`std::jgir`** 打印的是节点携带的 URI：
   ```
   "label": "tensor:reshape"
   "label": "array:argmax_axis"
   ```
   （argmax 外面还套一层标准库函数的调用节点 `array::argmax<f32>`，算子节点本身是 `array:argmax_axis`。）

2. **`onnx::export` 今天对 `argmax` 直接拒绝**（这是读数，不是待修）：
   ```
   error[RuntimeDiag::TranslationRefused]: Translation pass 'onnx::export_model' refused the graph:
   operator 'array:argmax_axis' has no ONNX lowering at opset 17 but receives a value that depends on the model input
   ```

3. **对照**：`tensor:reshape` 导出的 `.onnx` 里 op 类型是 `['Reshape', 'Identity']`。

**为什么**：lowering 表按 **URI** 键注册，`modules/onnx/lowering.cpp:1349` 注册的名字是
**`tensor:argmax_axis`**；节点带的是 `array:argmax_axis`，查不到，于是拒绝。**导出用的是 URI，不是别名表**——
导出这边早就认 `tensor:` 这个拼法，只有标准库的 `#op` 声明说 `array:`。

**结论一句**：导出用的是**算子 URI**（与进表同一个 `OperatorTable`），lowering 表按 URI 注册——所以
**若 `array:` 定为别名、`tensor:` 为正名（建议 (b)），导出会自动恢复**（它本来就在 `tensor:` 上注册着）；
**若 `array:` 定为正名（(a)），`modules/onnx/lowering.cpp` 那张表的键要跟着改**，否则 `argmax` 永远导不出去——
今天就已经导不出去，上面第 2 条就是证据。

**这使 (a)/(b) 从"两处改动"变成可算的三处账**：

| 选择 | 进表 | `std::jgir` | `onnx::export` |
| --- | --- | --- | --- |
| (b) `tensor:` 正名 | 写 `tensor:argmax_axis` | 名字会变成 `tensor:…`（读数变化，无功能影响） | **不改**（已注册） |
| (a) `array:` 正名 | 写 `array:argmax_axis` | 不变 | **要改** `lowering.cpp:1349` 的键 |

## 五、这不是孤例：六条轴归约都有两条身份

`argmax` 不是特例。`stdlib` 里同一个算子被**两处**声明，拼法不同：

| 声明处 | 拼法 |
| --- | --- |
| `stdlib/array.cml`（用户调用的 `argmax`/`sum` 落在这里） | `array:argmax_axis`、`array:sum_axis`、`array:mean_axis`、`array:min_axis`、`array:max_axis`、`array:argmin_axis` |
| `stdlib/signatures/tensor.cml`（tensor 模块签名） | `tensor:argmax_axis`、`tensor:sum_axis`、…（同一批六个） |

`modules/onnx/lowering.cpp` 的键全在 `tensor:` 一侧：

```
add("tensor:sum_axis",  reduceAxisOp("ReduceSum"));
add("tensor:mean_axis", reduceAxisOp("ReduceMean"));
add("tensor:max_axis",  reduceAxisOp("ReduceMax"));
add("tensor:min_axis",   reduceAxisOp("ReduceMin"));
add("tensor:argmax_axis", lowerArgMax, 12);
```

于是**节点带 `array:`、lowering 键是 `tensor:`，逐个都导不出去**——已实测两条：

- `x->sum<0>()`：jgir 写 `array:sum_axis`，导出报 `operator 'array:sum_axis' has no ONNX lowering at opset 17`；
- `x->argmax<1>()`：jgir 写 `array:argmax_axis`，导出报同样一句。

**判据**：凡 `#op` 说 `array:` 而 lowering 登记在 `tensor:` 上的算子，今天都导不出去。这条分裂覆盖**六个**轴归约
（`sum`、`mean`、`min`、`max`、`argmax`、`argmin`），不是一批里漏改了一个——**是命名分裂的系统性后果**。

同一分裂也解释了区域侧：表里写的是 `tensor:sum_axis`、`tensor:mean_axis`、`tensor:max_axis`，
而节点带 `array:sum_axis` 等，**表里那三条对真实程序是空的**——缺口账本"有实现 8"这一栏是**表**的读数，
不是节点的读数。这一条要按节点重新量，才能知道 `sum_axis`/`mean_axis`/`max_axis` 到底算不算"有实现"。

## 六、裁定之后的第一笔：为什么"移动注册"不是机械操作

裁定：正名取 `tensor:`，不设别名；删 C++ 侧 `array:…_axis` 的注册。按裁定的实际走法应为
"把六条实现注册挪进 tensor 模块，`stdlib/array.cml` 改为从 tensor 导入"。**今晚试了第一步（只改
`#op` 拼法）并回退，得到两条硬约束**：

1. **body-less 声明必须由已加载模块实现**：只把 `array.cml` 的 `#op('array:sum_axis')` 改成
   `tensor:sum_axis`，只 `import array` 的程序立刻失败——
   `SemanticDiag::NativeDeclarationNotLoaded: …'tensor:sum_axis', which no loaded module implements`。
2. **`tensor:` 那侧没有同名的 `sum_axis` 函数可导入**：`stdlib/signatures/tensor.cml` 里
   `#op('tensor:sum_axis')` 紧跟的是 **`export func sum(t: float[..], axis: int, keepdims?: bool)`**——
   那条 `#op` 是"函数名 `sum` → 算子 `tensor:sum_axis`"的映射，**`sum_axis` 这个名字在 tensor 侧不存在**。
   而 `stdlib/array.cml` 的 `sum` 是泛型 `<T>`，tensor 侧是具体类型的重载。

**所以这一笔的真正内容是**：让 `array` 的泛型 `sum`/`mean`/`min`/`max`/`argmax`/`argmin` 指向
`tensor:` 那六个算子，同时不失去泛型表面——要么 `array` 的函数改成对 tensor 重载的转发（需要 tensor
侧补泛型入口或按类型分派），要么把六条实现注册真正搬到 tensor 模块下（`getArrayOpsMap()` 里那六条来自
`src/builtin/operators/array_axis.cpp`，而 tensor 的执行走自己的 OpDef/内核，两条实现路径不同）。
**这不是一次字符串替换，是一次 stdlib 表面与算子归属的对齐**；建议单开一笔、带完整门禁做。

`sort`/`argsort`/`merge_sorted` 不动：声明与实现都只在 `array:` 侧，按裁定判据它们的正名就是 `array:`。
