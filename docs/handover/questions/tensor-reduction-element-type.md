# 张量归约是否返回元素类型（数组归约已经这样做了）

Author: 车道 `handover/h390-scalar-operand`（`D:\Projects\Camel-wt\h390-scalar-operand`），2026-10-24。Status: open — 需要负责人裁定。测量在 Linux 主机上、以最新 `origin/develop`（`f177b10db`）加本车道提交的构建实测（这台主机的 CUDA 不可用，读数走 `--devices cpu` 与 `tiles::compile` 的编译层判定）。

## 已经落地的部分

H371 的字面量规则（`cb627cdfe`）已经落地：`Tensor<f32>` 旁边的 `2.0` 取 `f32`（设计原则 5.1），已经取型的 f64 标量被拒并提示写 `f32(a)`。本车道补上了这条规则漏掉的一个位置：`loop` 的值——`let n: f32 = loop s = 0.0 while s < 3.0 do s + 1.0` 之前是 `DeclaredTypeMismatch`，因为状态里那个 `0.0` 没拿到 `f32`。

## 但归约的返回类型两侧不一致

- `stdlib/array.cml:182` `export func sum::<T>(xs: T[]): T when is_numeric::<T>()` —— 数组归约**返回元素类型**（`f177b10db`，今天 14:56，"one reduction declaration per element type"）。
- `stdlib/signatures/tensor.cml:229` `export func sum(t: Tensor): float` —— 张量归约返回 `float`。
- `docs/design-principles.cn.md:463`（§7.3）："浮点数组的归约在 f64 中累加、舍入一次，**返回元素类型（与张量归约一致）**。"

原则把数组侧的规则说成"与张量归约一致"，而代码里两侧不一致：按字面读，张量归约也该返回元素类型。AGENTS.md 规定原则与代码冲突时以原则为准、把冲突记为缺陷或待办、不要临场改语义——所以这条交回负责人。

实测（`x: Tensor<f32>`）：

```
error[SemanticDiag::DeclaredTypeMismatch]: This value has type 'float', which is not the declared type 'f32'.
  --> v1.cml:5:5
 5 |     let a: f32 = sum(x)
可用重载：<tensor:sum>: (t: Tensor) => float / <tensor:sum_axis>: (t: Tensor, axis: int, keepdims?: bool) => Tensor
```

## 这条决定 `crosslane` 这种形状能不能上设备

`test/cases/modules/tiles/loop_boundaries.cml:11-15` 的 `crosslane` 是 `s + f32(sum(s))`（`s: Tensor<f32>`）。三种写法今天的状态：

- `s + sum(s)`：按 H371 被拒（f64 标量旁边有张量）——就是上面那条；
- `s + f32(sum(s))`：解析、编译都过，但**上不了设备**。自动划分把该区域按 `scalar_tensor_operand` 拒掉（`modules/tiles/src/graph_regions.cpp:135-136,208`），于是整段留在宿主上跑（实测 `tiles::compile --devices cpu` 无错、结果正确）；写成 `#gpu` 作用域则是硬错误 `error[RuntimeDiag::GpuScopeUnsupported]: '#gpu' scope 'crosslane': operator ':op/dtof' cannot run on the device (no device implementation)`，因为设备程序表（`modules/tiles/src/graph_regions.cpp:43-64`）只列张量算子。
- 也就是说：**要让这段上设备，只能让 `sum` 给出 f32**（`s + sum(s)` 直接就是 f32 + f32）。这正是"M0 的设备图里没有 f64 标量"能不能成立的分岔口。

标量叶子本身的另一半（f32 标量到不到得了区域边界）是另一条问题，见 `f32-scalar-to-the-device.md`。

## 选项与代价

1. **张量归约返回元素类型**（`sum(Tensor<f32>)` 是 `f32`，`mean` 同理；累加仍在 f64 内、只舍入一次，与 §7.3 一致）。代价：`sum`/`mean` 的返回类型是每一个现有程序的类型，把结果交给 `float` 的写法会变成声明不匹配，要像数组侧那一批一样逐处核对并写显式转换；是一次需要单独测量的切片。
2. **保持 `float`，改 §7.3 的括号**（写成"数组归约返回元素类型；张量归约返回 float，由设备侧当广播标量用"）。代价：GPU 计划 M0 的目标"没有 f64 操作数进入设备图"在这个形状上永远要写出设备没有的 `f32(...)`，`crosslane` 这类形状只能按计划 §1.1 说的"运行期 f64 标量先保持拒绝"改断言。
3. **保持 `float`，给 `:op/dtof` 加设备程序**。代价：把"拒绝 f64 标量"换成"在设备上窄化"，与裁定相反，而且是把 H371 刚删掉的窄化路径请回来；每个元素都要多一次窄化。

## 建议

**选 1**。理由：§7.3 已经是这么写的、数组侧今天已经这么做，而它是唯一能让 M0 的"设备图里没有 f64 标量"在不引入任何设备侧 f64 路径的前提下成立的选项。验收建议两句：`sum`/`mean` 对 `Tensor<f32>` 的类型是 `f32`（对 `Tensor<f64>` 是 `float`）；`crosslane` 写成 `s + sum(s)` 后该区域变成设备区域（`std::profile` 的 `operator_rows[].device == cuda`，见 GPU 计划 §2 M0），且 `--devices cpu` 下的值与宿主一致。

## 出处

`docs/design-principles.cn.md:463`（§7.3）与 §5.1；`stdlib/array.cml:182`、`stdlib/signatures/tensor.cml:229`；`modules/tiles/src/graph_regions.cpp:43-64,135-136,208`；`test/cases/modules/tiles/loop_boundaries.cml`；裁定 `gpu-scalar-operand`（2026-10-23，`docs/handover/reviews/questions-2026-10-23.md`）；`docs/design/gpu/plan-modules.cn.md` §1.1、§2 M0。