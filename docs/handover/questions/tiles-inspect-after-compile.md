# 已经编译过的图怎么读：`tiles::inspect`/`source`/`benchmark` 撞上 `tiles::compile` 自己插入的上下文参数

## 结论

`tiles::inspect`、`source`、`benchmark` 是模块 `tiles` 导出的**算子**（`modules/tiles/src/module.cpp`
的 `getOperatorGroups()`，参考签名在 `stdlib/signatures/tiles.cml`），**不是遍**：Tiles 注册的遍只有
`tiles::compile`（`modules/tiles/src/region_pass.cpp`，`registerModulePass`）。流水线里写
`tiles::inspect` 是一个未知遍：

```
error[RuntimeDiag::InvalidPassParameter]: Invalid pass specification 'tiles::inspect': 'tiles::inspect' is not one non-generic function.
```

所以"同一条流水线里 inspect 之后接 compile 会失败"的准确条件是：**程序里调用报告算子** ∧ **流水线里
有 `tiles::compile`**，与遍序无关。

留下带上下文参数（`with`）节点的**不是 inspect，是 `tiles::compile`**：它按设计把段换成
`OPER tiles:exec`（`docs/design/gpu/plan.cn.md` §2.5、§4.1），实现里是 `region_outline.cpp` 的
`draft.addOperNode(type, run, …)` 加 `draft.setWithInputs(call, {fnNode})`，段图作为**上下文实参**
绑给 `tiles:run_preferred_resident`，两侧是 `tiles:retain_preferred` 与 `tiles:download`。

报告算子对**整个函数图**调 `prepare()`（`modules/tiles/src/module.cpp` 的 `inspectKernel`/`sourceKernel`/
`benchmarkKernel` → `prepareProgram` → `compile`），编译器 `eval` 走到那个 exec 节点时，
`modules/tiles/src/compiler.cpp` 的 Oper 分支对 `withInputsOf(id)` 非空直接
`throw UnsupportedComputation("operator context arguments are not supported yet")`，
`checked()` 再把它变成 `RuntimeDiag::RuntimeError`。

## 逐字读数（均为 `--devices cpu`；退出码在最后一列）

程序：

```cml
module repro_inspect_compile
import { Tensor, reshape } from tensor
import { inspect } from tiles

#gpu
func pair(x: Tensor<f32, [3]>): Tensor<f32, [3]> { return x * 2.0 }

func main(): int sync {
    let x = reshape([1.0, 2.0, 3.0] as f32[], [3])
    println(inspect<pair>(x))
    return 0
}
```

1. **正对照 A：报告算子在场、流水线里没有 `tiles::compile`**（`camel repro.cml std::nvm`）→ 退出码 0，
   打印该程序的设备计划 JSON：

   ```
   {"schema":1,"values":3,"kernels":1,"materialized_values":3,"allocations":3,"logical_bytes":28,"allocated_bytes":28,"borrowed_inputs":0,"borrowed_bytes":0,"stages":[{"output":2,"implementation":"standard","members":[2],"outputs":[2]}],"outputs":[2],"graph_fact_instances":1,"specialized_instances":0,"numeric_permissions":[0,0,0],"indexed_nodes":[2],"operator_contracts":[{"identity":"tensor:multiply@1.1","declared":true,"numeric":"correctly_rounded"}]}
   ```

2. **复现 B：同一个程序 + `tiles::compile std::nvm`** → 退出码 1，`stderr` 全文：

   ```
   error[RuntimeDiag::RuntimeError]: An error has occurred: tiles: pair node 6: pair node 5: operator context arguments are not supported yet.
      --> repro_inspect_compile.cml:13:20
       |
    13 |     println(inspect<pair>(x))
       |                    ^^^^^^
   help: Check the runtime environment and input data for unexpected issues.
   ```

   把 `inspect` 换成 `source` 得到逐字相同的一条（只有位置列不同）；`benchmark` 同路（同一个 `prepare`）。

3. **正对照 C：`tiles::compile` 在场、程序不调用报告算子**（同一形状的 `pair`，`main` 直接 `println(pair(x))`，
   流水线 `tiles::compile std::nvm`）→ 退出码 0，`TILES_PLACEMENT`/`TILES_RESIDENCY` 六行之外打印
   `tensor([      2.,       4.,       6.], dtype=float32)`：pass 自己正常，运行期执行 exec 节点时
   `runPreferredResidentKernel` 编译的是**段**（`compileTypes`），不是包装图。

4. **对照 D：`regions<pair>()`（只做区域分析、不编译）+ `tiles::compile std::nvm`** → 不报错（退出码 0），
   但读数已经退化：`"regions":[]`，而 `"excluded"` 里 `{"node":5,"reason":"context_arguments"}` 就是 pass
   插的那个 exec 节点。也就是说：**分析型读者看到的是包装图，不是段**。

## 定位（符号 / 文件 / 上下文）

| 环节 | 位置 |
|---|---|
| 留下 with 参数的产生点 | `region_outline.cpp`：`addOperNode(type, run, device->executeUri, …)` + `setWithInputs(call, {fnNode})`；GIR 上是 `N06 Function(graph=pair/tiles.region/0) -> N07 tiles:run_preferred_resident [type="WITH"]`（`tiles::compile std::gir`） |
| 拒绝的产生点 | `compiler.cpp` 的 `Frame::eval`，`case rt::GCNodeKind::Oper`：`if (!g->withInputsOf(id).empty()) throw UnsupportedComputation("operator context arguments are not supported yet")`；该异常按契约是能力缺失（`tiles/internal/capability.h` 首行："Capability misses are dispatch decisions; compiler/runtime faults are not"） |
| 消费点 | `modules/tiles/src/module.cpp` 的 `checked()`：把 `std::exception` 变成 `RuntimeDiag::RuntimeError`，于是内部措辞 "not supported yet" 直接到用户面前（`region_outline.cpp` 的 `#gpu` 拒绝路径遇到空 `oper()` 时兜成 `<region>`，同一处缺口） |
| 为什么 pass 自己不失败 | `region_outline.cpp` 只对段图调 `compileTypes(fn)`；段图里没有 with 参数。运行期的 exec 节点编译的是段（`compiled_region.cpp` 的 `prepareProgram(…, staticRegion=true)`） |
| 为什么"整图重编"本来就不是答案 | 包装图两侧的 `tiles:retain_preferred`/`tiles:download` 是带效果的算子，`compiler.cpp` 对非纯算子另有一条拒绝（"unsupported or effectful operator"）：**即使支持上下文参数，包装图也不是一个设备程序** |

`inspect` 本身什么也没留下：`std::gir` 两次 dump 里 `main` 的 `N07 tiles:inspect` 及其 `WITH` 边
（源是 `Function(graph=pair)`，写在程序里）逐字相同；变的只有 `pair` 自己。

## 判据：不是"最小修能修的缺陷"，是今天还没有读法

- **设计已经定了将来怎么读**：`docs/design/gpu/plan.cn.md` §4.1 把编译单元定成（作用域图, 放置），产物作为
  目标事实挂在图上（`tiles.compiled_scope`，`tiles.compiled_region` 的扩展），§4.2/§4.5 与 S9 阶段把
  `inspect` 的读法定成"一个作用域一份产物"（`scope_one_compilation.cml`：inspect 一份产物、编译计数 1）。
  即**报告算子与 `tiles::compile` 同时在场是设计要的**，今天的失败与设计冲突。
- **今天该怎么报没有裁定**：G3/S9 在计划里明确排在后面（"等 P4 合入后再做"）。任何最小修都必须在几种读法里
  选一种：报段/单元的产物（要定 JSON 形状）、写一条点名"这张图已经被 `tiles::compile` 编译过"的诊断
  （要新诊断），或宣告"要读计划就不要在流水线里放 `tiles::compile`"。三条都是用户可见的裁定。
- 按"先定位、不改语义"处理：本页只给读数与位置，树里没有改动。

## 需要裁定的

1. **今天读什么？** 上面三条候选各要什么（形状/诊断/文档），哪一条现在是答案。
2. **G3/S9 是不是最终答案**（`inspect` 读 `tiles.compiled_scope`）；若是，`source`/`benchmark`/`regions`
   是否一起走这条路（它们今天都会走到同一个包装图）。
3. **在 G3 之前要不要一条明确的拒绝**：让报告算子在"图已经被本遍编译过"时给一条点名的诊断，而不是把
   `UnsupportedComputation` 当内部故障抛出去？这条要新诊断码吗。
4. **记录点**：`test/cases/modules/tiles/gpu_scope_segments.cml` 的注释今天把这件事写成该用例的取舍
   （"a pipeline that has already compiled the segments cannot answer that"）。本页落定后那句要不要改成引用
   本页——改它属于动另一条车道的用例注释，没有做。