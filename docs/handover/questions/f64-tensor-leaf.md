# 元素类型没写下来的张量叶子：按值的元素类型，还是按「没写就是 f32」

## 现象（都在 `handover/h353-f64-leaf` 上用构建出的二进制量过）

写明的元素类型已经在这个分支上修好：`Tensor<f64, [?]>` 的叶子走原生 `nn:sgd` / `nn:adam` 之后仍是
f64（两个元素 16 字节，值是 f32 装不下的 `16777216.5`），守卫用例是
`test/cases/modules/optim/native_dtype_exact.cml`。**没写下**的那一半没动，因为它是两侧共用的、已经
写下来的规则，改它会改变现有程序的数值。

`full([3, 2], 0.1)`（`0.1` 是 `float`，所以数据是 f64）放进一个 `Tensor` 字段——`mlp.cml`、
`embedding_mf.cml` 都这么写——量出来是：

```
probe untyped value=0.6 elements=6 before_bytes=48 grad_bytes=48 native_bytes=24 camel_bytes=24 state_bytes=48
probe written  before_bytes=24 grad_bytes=24 native_bytes=24
```

- 这个叶子一步之前 48 字节（6 个元素、8 字节一个：f64），它的梯度也是 f64；
- 走一步原生 `nn:sgd` 之后 24 字节（4 字节一个：f32），走一步 Camel 模块的 `adam` 之后也是 24 字节；
- 同一次量里，Camel 模块给这个叶子建的矩 `state.m.w` 是 **48 字节（f64）**：这一步在模块内部是
  「f64 的矩进、f32 的参数出」；
- 第二行是同一个叶子写上 `f32(0.1)` 的对照：24 字节进、24 字节出。

## 为什么需要裁决

`docs/design-principles.cn.md` 7.4 要求元素类型精确，要按比程序写下的更低的精度算得先有 7.2-7.3 的
`change_precision` 许可，而这里没人写过；H238 的裁定「创建与转换不得静默改变类型」同理：
`full([3, 2], 0.1)` 创建出来的是 f64 张量，优化器把它按 f32 更新，是一次没写下来的降精度。

但这条规则不是随手写的：`stdlib/optim.cml` 的 `case _` 支写成 `cast(x, 'float32')` 并注明与原生路径
相同，`test/cases/modules/optim/agreement.cml` 的注释也写着「两边都把叶子窄化成 f32」。改它要同时改
三处（Camel 模块、原生路径、`docs/technical/optim.md`），并且会改变现有程序的数值。

Camel 侧今天也没有「按另一个值的元素类型转换」的算子：`cast(t, dtype)` 的 dtype 是一个字符串常量
（`stdlib/tensor.cml:351`），元素类型没写进类型里的叶子在类型层分派不出来。所以「值的元素类型说了算」
在 Camel 侧需要语言先给出写法，不是一处局部修改。

## 选项与代价

- **(A) 值的元素类型说了算（建议的方向）**：原生侧 `leafElementType` 读运行时的 dtype（没写下时用值
  的），Camel 侧需要 `cast_like(x, y)` 一类算子，或等 `Tensor` 成为数组别名、元素类型在类型里可见之后
  按类型分派。代价：`mlp.cml` / `embedding_mf.cml` 这类程序（`full([3, 2], 0.1)`）的训练从 f32 变成
  f64，数值变、每元素 8 字节；两侧必须同时改，否则同一拼写在两边不一致。收益：7.4 在这条拼写上也被
  满足，写明与没写下的 f64 叶子行为一致。
- **(B)（本轮落地）没写就是 f32，并把规则写进文档**：`modules/nn/optim.cpp` 的 `leafElementType` 对没
  写下的类型返回 `Float32`，与 `stdlib/optim.cml` 的 `case _` 支一致；`docs/technical/optim.md` 的算术
  表和「缺口」表都按这条写。代价：`full([3, 2], 0.1)` 造出的 f64 张量在优化器里被窄化，与 7.4 的字面
  要求相抵；好处是两边现有一致性与数值一个都没动。
- **(C) 把决定留给用户**：核心与优化器都不动，训练前由用户显式转（`cast(w, 'float64')`）。代价：忘了
  写的地方仍然静默降精度，等于要程序作者每次记着 7.4。
- **(D) 训练位置拒绝没写下元素类型的叶子**：`nn:sgd` / `nn:adam` 与 `optim` 的 `init` / `update` 要求
  叶子写明元素类型。代价：`mlp.cml` / `embedding_mf.cml` 这类程序必须改写（写成 `f32(0.1)` 即可）；
  收益：规则由编译器查，不靠约定，与 7.4 一致。

## 建议

定 (A) 这个方向，并把它排到「`Tensor` 成为数组别名、元素类型进类型系统」那件事上一起做：到那时
`Tensor` 叶子的元素类型在类型里可见，Camel 模块的 `case _` 支可以按类型分派，两侧一次改齐。在那之前
保持 (B)（本轮已按 (B) 落地，并把规则写进了 `docs/technical/optim.md`），因为 (A) 只改原生侧会让同一
拼写在两边不一致，(D) 会强制改写现有程序。若作者认为「没写下的元素类型在训练里就是 f32」才是想要的
语义，那 (B) 只需要在设计原则里写下来，本轮的代码与测试都不用再动。

## 影响

只动优化器的叶子处理，不牵动图边、类型系统与数值内核的其它算子；「写明的元素类型」那一半已经落地并
有守卫（`optim.native_tensor_leaf_keeps_its_element_type`，四个后端）。若走 (A)，要一起看
`stdlib/optim.cml` 的 `case _` 支与 `docs/technical/optim.md` 的算术表；`agreement.cml` 用的是写
`f32(...)` 的叶子，不受影响。

## 来源

- `handover/h353-f64-leaf`（基点 `origin/develop`）：守卫用例
  `test/cases/modules/optim/native_dtype_exact.cml`（写明的 f64 叶子），以及一次临时探针（没写下的
  叶子，上面两行就是它的输出；探针已删除）。量法是 `size(to_bytes(...))`：`to_bytes` 按值**实际**的
  元素类型给字节，所以 8 字节一个元素就是 f64。
- 规则的两处出处：`stdlib/optim.cml` 的 `case _` 支、`test/cases/modules/optim/agreement.cml` 的模块
  注释；原生侧的对应实现是 `modules/nn/optim.cpp` 的 `leafElementType`。
- 用到了没写下的 f64 叶子的程序：`test/cases/modules/nn/mlp.cml:28`、`test/cases/modules/nn/embedding_mf.cml:37`。