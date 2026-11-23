# 一个 f64 的卷积答什么：`float`，还是 `f32` —— 规则的答案从入参来，还是从注册表来

2026-11-23 ｜ 来自 `lane/onnx-lowering` ｜ 对象:`src/stdlib/nn/tensor_ops.cpp` 的 `conv2dInfer`、`poolInfer`、`shapedLike`、`batchNormTrainGradInfer`;`test/cases/modules/nn/conv2d_shape_error.cml`;`test/cases/modules/onnx/export_train_step_conv.cml`

## ① 事实：图上写 `float`，规则答 `f32`，两者在**参数梯度**上相遇

`export_train_step_conv` 的模型权重是 `float[...]`（f64），`step` 里对它们跑 `conv2d`。
导出器问注册规则这个节点的答案，规则答 `f32`；而图上这一点写的是 `float`。相遇的地点不是
卷积本身，而是**参数的梯度**：`conv2d_kernel_grad` 的答案按规则是 f32，`zeros_like(conv1_w)`
按值是 f64，两者在一个 `tensor:add` 上撞：

```
[Error]: Pass 'std::default' failed with an internal error:
the two tensor operands have different element types ('float' and 'f32')
```

读数（同一台主机，`-Jobs 8`，负载 1m 17–33，不与静机可比）：

- 修这一轮的三处桥之前：`export_train_step_bn`/`conv` 都停在
  `the rank of an operand is not known statically`（`dy` 没有 rank）；
- 三处桥之后（见 ⑤ 第二条）：两处都推进到上面这条**元素类型**的拒绝；
- 再把 nn 的规则改成「随入参」：`export_train_step_conv` 导出并通过，
  `export_train_step_bn` 推到 BN 的反向（那里发出的是 f32、而答案声明 f64）,
  **但 `modules.nn/conv2d_shape_error` 转红** —— 见 ④。

先把一个容易混的点钉住：`float` 与 `f32` **不是同一个类型的两种拼法**。
`src/compile/src/gct/builder.cpp:124-126` 里 `f32` → `Type::Float32()`，
`f64` 与 `float` → 同一个 `Type::Float64()`。所以这不是「拼法归一」的问题，
而是**一个算子的答案的元素类型从哪来**：从它自己的注册规则里写死的那一个，还是从它读到的入参。

## ② 甲：nn 的规则「随入参」

`conv2d`、两个池化窗口、`batch_norm_train_grad`、以及所有「形状照着某个入参」的梯度规则，
答那个入参的元素类型（入参的元素类型不知道时才退回 f32）。

- 代价一：`conv2d_shape_error` 转红（④）；
- 代价二：`src/stdlib/nn/signatures/nn.cml` 里那批 `f32` 实例的读法要跟着定 —— 该文件自己
  写着它们「for reference only … not compiled」，但 `registry.signatures_match` 拿它跟登记的
  `OpDef` 对，所以「实例」与「规则」在今天不是一句话；
- 反对它的事实（这一轮读出来的）：`k::conv2d` 之类的内核**只算 f32**。`requireF32`
  （`src/stdlib/tensor/kernels/conv.cpp:82`）逐个检查入参，f64 的张量在运行期是一条故障
  （`refuseTensorElementType`，不是缩窄）。所以甲只把**类型**改成随入参：`export_train_step_conv`
  能导出（导出器不执行内核），但同样的程序真跑起来会在 `conv2d` 上被拒。甲要成立，还得把
  这些内核加宽到 f64，或者先说清「f64 的卷积在 Camel 里不是可执行的值，只能导出」。

## ③ 乙：规则不动（f32 写死），把差异放到别处

- **(乙1) 调用处转换**：规则保持 f32，程序自己写 `f32(conv2d(...))`。这样 `conv2d_shape_error`
  原样成立，而 `export_train_step_conv` 的 `step` 必须在每个卷积/池化/BN 之后写一次 `f32(...)`、
  反向上再把梯度原路转回来（语言没有隐式转换，这些转换都得写出来）。代价：一个 f64 的模型，
  每过一处 nn 算子就多一对转换，而它们只是为了让规则成立。
- **(乙2) 规则写成类型参数**：`conv2d::<T>` 答 `T`，把甲的语义放进注册表而不是写在 C++ 里。
  这不是第三个答案，是甲的一种写法；好处是「答案随入参」成为**声明**，`signatures` 与 `OpDef`
  仍然对得上。
- **(乙3) 导出器补转换**：规则保持 f32，导出器在边界上插一个 `Cast`，把 f32 的答案变成图上
  写的 `float`。只让导出器自洽、不动语言；代价是往导出的模型里插一个**程序里没有的**转换，
  而反向上它得再插一个反向的 —— 那正是「导出器替语言做题」，本车道不该这么收。值得记下的是
  这一半**已经**在做：`lowerConv2d` 把三个入参都按 `TypeCode::Float32` 取
  （`src/stdlib/onnx/lowering.cpp:1038`；`LowerContext::input` 在元素类型不同时发一个 `Cast`），
  所以导出的 Conv 全是 f32。断的是「卷积的答案回到 f64 的参数树」那一步 —— 那里没有谁转。

## ④ `conv2d_shape_error` 里那句 `=> f32[...]`：是期望，还是一个被写死的答案

那个用例的全部内容是：

```cml
func convolve(x: f64[...], kernel: f64[...], bias: f64[...]): f32[...] {
    return conv2d(x, kernel, bias)
}
```

它要验的是**通道不匹配在运行期被拒**（正文打印 `sum(y)`）。参数声明成 f64、返回声明成 f32、
正文里没有任何转换 —— 所以这句声明只可能是两者之一：

- **是期望**：那就等于说「f64 的卷积答 f32」是语言的一条规则，甲不成立；
- **是被写死的答案**：作者抄了签名里那一个 `f32`，而它验的东西与元素类型无关，甲成立，声明
  改成 f64[...] 即可（不是改测试的判据，是把它抄错的那一半抄对）。

这题有两处容易漏的读数。其一，这个用例里**只有返回类型这一处**说了元素类型：
`signatures/nn.cml` 的头写着「For reference only: these declarations are not compiled」，
它由 `registry.signatures_match` 与登记的 `OpDef` 对，而不在调用处检查 —— 一个 f64 的实参
能交给声明成 `f32[...]` 的 `conv2d`，本身就是这件事的读数。其二，那句 f64 能跑到
「通道不匹配」而不是「`conv2d` 只要 f32」，是因为 `k::conv2d` 先算 `geometry`
（`src/stdlib/tensor/kernels/conv.cpp:157-160`）再 `requireF32`（同文件 `165`）：
通道在元素之前被拒，所以元素检查从来没走到。

我读到的是后者：用例正文没有转换，而它想验的是形状。但这只是我的读法，**不是我能定的**：
把一句用例声明改掉，等于替作者回答「规则答什么」。

## ⑤ 我这一轮做了什么，以及为什么不动

- 甲的整笔改动做过、量过、**已回退**，证据在 `015b989ade`。那一笔的说明原样引用在这里，
  它是实验记录，不是代码：

  > A registered rule's element type is the language's statement about an operator, not the
  > exporter's to restate, and which of the two is right -- a rule that follows its argument, or
  > a call that converts at the boundary -- is a question for the language author (design
  > principles: undecided questions go to them, and semantics are not changed to make a test
  > pass).

  它同时记下两条读数：`export_train_step_conv` 导出并通过；`conv2d_shape_error` 转红，
  红在它那句 `=> f32[...]` 上。
- 留在树上的只有三处**导出器/推论之间的桥**，它们不改任何语义，只把值已经有的事实交给问它的
  人：一个符号 int 列表的长度（`inferenceTypeOf`）、一个图上算出来的列表的已知项
  （`applyOper` 的 `partials`）、一次转换的范域（`:op/ato*` 取入参而不是节点泛型类型）。
- 这三处与 ② 的元素类型是**同一形状**：一个答案丢掉了值拥有的一个事实（长度、已知项、范域），
  而下游正依赖那件事。今天是第四处，前三处已修；这一处丢的是**元素类型**，而定它要用的是
  语言层的判断，所以它停在问题页上。
## 决策者答（2026-11-23 19:00）

**甲，用乙2 的写法：元素类型随入参，并写成声明。** 张量算子的答案随操作数的元素类型，这是其余张量算子（`add`、`matmul`、归约）已经遵守的规则；nn 的规则写死 `f32` 是同一类事里的例外，不是另一条规则。所以：

- `conv2d`、池化、`batch_norm` 及其梯度声明成 `::<T>`（T 是浮点元素类型），答 `T`；规则在声明里，不在 C++ 里写死。
- 内核同一笔加宽到 f64（按元素类型模板化）。只改类型、不加宽内核，会留下"能导出、跑起来报错"的值，那比现在更糟。设备程序暂时只有 f32 的，按放置规则回落到 CPU。
- `conv2d_shape_error` 的 `=> f32[...]` 是抄错的那一半，改成 `f64[...]`；它验的是通道不匹配，与元素类型无关。
- ④ 里那条更深的读数单独记账：`signatures/nn.cml` 写着"不编译、只供参考"，调用处不按它检查，于是声明与 `OpDef` 规则是两份。这正是核心抽象第 17 节要收的："签名写在声明里，只有确实要计算的才是类型函数"。nn 这批随本条改成真正编译、调用处检查的声明。
