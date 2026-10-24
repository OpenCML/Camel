# tiles 的标量边界值是 f64 还是张量的元素类型

Author: 车道 `handover/h390-scalar-operand`（`D:\Projects\Camel-wt\h390-scalar-operand`），2026-10-24。Status: open — 需要负责人裁定。测量在 Linux 主机上、以最新 `origin/develop`（`f177b10db`）加本车道提交的构建实测（这台主机的 CUDA 不可用，全部读数走 `--devices cpu` 与 `tiles::compile` 的编译层判定）。

## 已经落地的部分

H371 的字面量规则（`cb627cdfe`）已经落地：`Tensor<f32>` 旁边的 `2.0` 取 `f32`，已经取型的 f64 标量被拒并提示写 `f32(a)`。**结论是这张量旁边的标量是 f32**，于是下面所有"标量"都是 f32。

## 但 f32 标量到不了设备：两处谓词只承认 f64 标量

实测两段用户今天就能写出来的程序（`Tensor<f32, [3]>`，`tiles::compile --devices cpu`）：

```
// (1) 运行期算出的 f32 标量
func runtimeScalar(x: Tensor<f32, [3]>): Tensor<f32, [3]> {
    let pre = x + x
    let n: f32 = loop s = 0.0 while s < 3.0 do s + 1.0
    return pre * n
}
→ error[RuntimeDiag::PassInternalError]: Pass 'tiles::compile' failed with an internal error:
     Tiles region transfer has an unsupported aggregate leaf.

// (2) 同一形状，标量换成 f64（`test/cases/modules/tiles/loop_boundaries.cml:16-20` 的 conditional）
func conditional(x: Tensor<f32, [3]>): Tensor<f32, [3]> {
    let pre = x + x
    let n = loop s = 0.0 while s < 3.0 do s + 1.0
    return pre * f32(n)
}
→ 同一条 PassInternalError（`camel test/cases/modules/tiles/loop_boundaries.cml tiles::compile std::nvm`）
```

机理：区域边界上的标量叶子要能被传输，而 `modules/tiles/src/graph_values.cpp:22-59` 的 `buildResidentType` 只给 `tensor::isTensorType(type) || type->code() == TypeCode::Float64` 一个可传输类型，**f32 标量返回 nullptr**，`modules/tiles/src/region_outline.cpp:42` 于是抛 "unsupported aggregate leaf"。同一族还有第二处：`modules/tiles/src/module.cpp:473-477` 的 `supportedLeaves` 把 `Float64` 与张量当作仅有的可接受叶子，于是 `scale(x: Tensor<f32>, k: f32)` 这个签名连**解析**都过不去（标量边界值的类型是 f32 时）：

```
func scale(x: Tensor<f32>, k: f32): Tensor<f32> { return x * k }
let x: Tensor<f32, [3]> = ...; let k: f32 = 0.5
source<scale>((x, k))
→ error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types:
   <(x: Tensor<float32>, k: f32) => Tensor<float32>> ((Tensor<float32, [3]>, f32)).
   可用重载：<tiles:source>: <fn: (...) => NumericAggregate>(inputs: NumericAggregate) => string
```

注意这条和"字面量是否装入形参"无关：把实参写成 `k: f32` 的变量、或写成 `f32(0.5)`，一样被拒；只有把参数写回 `k: float` 才解析得过去（**这正是 `test/cases/modules/tiles/runtime_scalars.cml:6` 与 `resident_scalars.cml:12` 的分歧**：前者 `k: f32`，后者 `k: float` 加 `x * f32(k)` 并写明"标量在边界上保持 float"）。

## 两侧的出处互相矛盾

- 裁定 `gpu-scalar-operand`（2026-10-23）："`Tensor<f32>` 旁边的 `2.0` 必须取 `f32`（设计原则 5.1），**设备侧不需要 f64 标量**" → 边界值应当是 f32，模块必须收。
- 模块自己的文档：`stdlib/signatures/tiles.cml:43-65` 的参考签名一律是 `(float, float)`；`test/cases/modules/tiles/resident_scalars.cml:9-12` 写着"The scalar stays a `float` at the boundary (a device resident one is passed in), and the operand the tensor sees is the f32 conversion" → 边界值应当是 f64，元素类型由 `f32(k)` 在区域内做出来。而这条路径今天也不通：`k: float` + `x * f32(k)` 解析得过，但区域编译器降不了 `:op/dtof`（`error[RuntimeDiag::RuntimeError]: tiles: scale node 3: scale node 2: unsupported or effectful operator ':op/dtof'.`），`#gpu` 下则是 `GpuScopeUnsupported: ':op/dtof' cannot run on the device (no device implementation)`（设备程序表 `modules/tiles/src/graph_regions.cpp:43-64` 只列张量算子）。

也就是说：**f32 标量（语言规定的形状）与 f64 标量（模块规定的形状）今天都到不了设备**，两个形状各缺一半。这不是临场能定的：它决定"M0 的设备图里到底允许哪种标量"。

## 选项与代价

1. **模块承认 f32 标量叶子**（`supportedLeaves` 与 `buildResidentType` 都接受 f32，必要时连 `int`/`i32`）：与裁定一致，"设备侧没有 f64 标量"成立，`runtimeScalar` 与 `loop_boundaries.cml` 的 `conditional` 都能过。代价：内核 IR 的标量类型、广播路径、`dtof` 的处置要一起看；`source`/`inspect`/`benchmark` 的可用签名因此变宽，需要一次有测量的慢层回归（这台主机跑不了 CUDA 慢层，只能给编译层与 `--devices cpu` 的读数）。
2. **模块保持 f64 边界值**，把 `f32(k)` 那条路修通（给 `:op/dtof` 设备程序或让它留在宿主侧在区域之前算完）。代价：与"设备侧不需要 f64 标量"相反；而且每个"元素类型是 f32 的张量乘一个运行期标量"都要多一次窄化。
3. **两条都留，但把语言侧的签名改成 f64**（即 `scale` 只能写 `k: float`）。代价：等于撤回 H371 在这条线上的结论（f32 标量旁边不该有 f64 标量），本车道的字面量规则就被这条堵住。

## 建议

**选 1**，理由：裁定已经把"设备侧的标量类型"定成元素类型，模块的两处 `Float64` 判断是裁定的实施缺口，不是另一个设计决定；选 2 与选 3 都要把刚定下来的规则再弯回去。验收建议两句：`runtimeScalar` 那段程序 `tiles::compile --devices cpu` 退出 0，且 `loop_boundaries.cml` 的 `conditional` 在 `--devices cpu` 下的值与宿主一致；`scale(x: Tensor<f32>, k: f32)` 的 `source<scale>((x, k))` 解析得过（这一条还需要"内建算子的实参是否自动装入"那条裁定的另一面，见 `entering-operator-args`）。

## 出处

裁定 `gpu-scalar-operand`（2026-10-23，`docs/handover/reviews/questions-2026-10-23.md`）；`docs/design-principles.cn.md` §5.1；`modules/tiles/src/graph_values.cpp:22-59`；`modules/tiles/src/region_outline.cpp:42`；`modules/tiles/src/module.cpp:473-477,504-525`；`modules/tiles/src/graph_regions.cpp:43-64,208`；`stdlib/signatures/tiles.cml:43-65`；`test/cases/modules/tiles/runtime_scalars.cml`、`resident_scalars.cml`、`loop_boundaries.cml`。