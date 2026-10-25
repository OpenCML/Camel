# `var` 同秩加宽与裸 `Tensor` 返回值：`lrit`/`mnist` 不在任何改写名单里，且计划对 `static_types` 的前提不完整（h483 lane，2026-10-25）

> 本文件只报选题与读数，不定语义。`docs/design/**`、`docs/design-principles*`、`docs/language/**`
> 一个字未动；本 lane 只改了 `test/cases/modules/tensor/static_types.cml` 与它的计划条目。

## 一、作者计划里与这五条单元有关的原文（逐字）

`docs/design/arrays-memory/plan.cn.md`：

- `:784` **Q5（`var` 加宽）**：「`var xs = [1, 2]; xs->append(3)` 要求加宽保留 rank 1（`int[?]`）。
  建议：**同秩加宽**（更安全，`append` 仍工作）；代价是 `var w = zeros([2,1]); w = ones([5])` 不再通过，
  要改写 `static_types.cml` 这一个用例。任意秩加宽会让 `var xs = [1]` 加宽成任意秩、`append` 失去形状检查。」
- `:323`：「`test/cases/modules/tensor/static_types.cml`：`var w = zeros([2, 1]); w = grow(w) (Tensor any)`
  依赖“任意秩加宽”；按 Q5 的结论（同秩加宽）改写成两次 `var` 或显式 `as`。」
- `:447`：「`test/cases/modules/tensor/static_types.cml`、`var_tensor_reference.cml`、
  `shape_inference/var_argument*.cml`：`var` 加宽与裸 `Tensor` 的用法按 Q5 调整。」
- `:698` 表格行：「`var` 加宽 | 任意秩 | 同秩（Q5） | `static_types.cml` 需要改写」
- `:757` 表格行：「`var` 加宽 | 同秩会改写 `static_types.cml` | Q5；选同秩，改写一个用例」
- `:190`（`widened` 的定义）：「同秩、所有维度变 `?`，保留元素与 `numericUnknown`。」

**名单里只有 `static_types.cml`。**`lrit`、`nn.lrit`、`mnist`、`nn.mnist` 在这份计划里一次都没出现
（`docs/design/arrays-memory/plan.cn.md` 全文 grep `lrit`/`mnist` 零命中）。`:447` 另外点到的
`var_tensor_reference.cml` 与 `shape_inference/var_argument*.cml` 在本树上已绿，本 lane 未动。

## 二、`static_types`：名单内、已按 `:323` 的两半改写（本 lane 的读数）

红前（本 lane 基线，`handoff/h465-flip-integration` + `origin/develop` 合并后）：

```
[FAIL] static_types
    [stdout] static_types.cml:22:5: [Error]: No matching function found for argument types:
             <> (f32[?, ?], f32[5]); operator '=' is not defined for these operand types.
             (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

红后（同一用例改写后）：`[PASS] static_types`，逐字 stdout
`static_types w_sum=5.0 pick=[3, 2] same_seed=true g=10.0`（`g=10.0` 是新增读数：原用例算了
`grow` 的裸 `Tensor` 结果却从未断言它）。

**但计划 `:323` 的前提不完整，这是本 lane 实测出来的**：那一行把 `w = grow(w)` 当作「任意秩加宽」的
来源；实测它**独立地**被拒——与加宽无关：

```
P1  var w = zeros([2, 1]) / w = grow(w)          （只有这一次赋值）
    h483_probe_p1.cml:12:5: No matching function found for argument types:
    <> (f32[?, ?], Tensor) … (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
P2  var w = zeros([2, 1]) / w = ones([5, 1])     → PASS（同秩加宽：只有 extent 变）
P3  var w = zeros([2, 1]) / var v = ones([5])    → PASS（计划 :323 的“两次 var”，不含 grow）
P4  var w: Tensor = zeros([2, 1]) / w = ones([5])→ PASS（格子类型写明为裸 Tensor）
P5  w = ones([5]) 之后再 w = grow(w)             → 报在 13:5（grow 那一行），
                                                   不是 12:5
P6  P4 的写法 + 本用例全部内容                   → PASS，断言逐字
    `static_types w_sum=5.0 pick=[3, 2] same_seed=true`
P7  一个推断格子 + 值保秩 + `let g = grow(w)`    → PASS，断言逐字同上
```

读法：**格子写明元素类型、来源未写明**是收窄方向，`assignableFrom(f32[?, ?], Tensor)` 为假
（`docs/handover/questions/element-type-convention-leftovers.md:24-25` 已如此判定）。所以
`var` 格子一旦装的是数组，任何返回裸 `Tensor` 的调用都进不去——这与 Q5 是**两件事**，`:323`／`:784`
只说了后一件。本 lane 按 `:323` 的字面「两次 `var`」改写时，剩下的 `w = grow(w)` 仍然红
（红后读数：`static_types.cml:29:5 … <> (f32[?, ?], Tensor)`），把 `grow` 的结果挪进自己的绑定才收口。

## 三、不在名单里的四条：`lrit`／`nn.lrit`／`mnist`／`nn.mnist`（未改）

红前逐字（本 lane 基线；`nn.*` 是 `test/plans/feat/opt/screens.plan.toml:96`/`:106` 用同一对用例）：

```
[FAIL] lrit / nn.lrit
    [stdout] lrit.cml:33:5: [Warn]: The call of '<__assn__>: (var Tensor, Tensor) => var Tensor' is not
             in a sync block, and it takes a var argument. …      （:34:5 同形，两条 warn 与本次无关）
    [stdout] lrit.cml:78:5: [Error]: No matching function found for argument types:
             <> (f32[?, ?], Tensor); operator '=' is not defined for these operand types.
[FAIL] mnist / nn.mnist
    [stdout] mnist.cml:92:5: [Error]: No matching function found for argument types:
             <> (f32[?, ?], Tensor); operator '=' is not defined for these operand types.
```

`lrit.cml:71` `var w = zeros([num_features + 1, 1])`、`mnist.cml:88` `var W = zeros([d, num_classes])`
都是**运行时** extent，格子类型 `f32[?, ?]`；`:78`／`:92` 的 `w = train<…>(w)` 右边是裸 `Tensor` 返回值。
本 lane 未改这四条，理由：它们不在 `:323`／`:447`／`:698`／`:757` 任何一处名单里，且要改就得在
P6（把格子写成 `Tensor`）与 P7（把返回值挪到自己的绑定）之间**替作者选一种语义**。
`docs/handover/questions/bare-tensor-instantiation-return-rank.md:77-85` 已经把这个问题问过一次
（「`lrit`/`mnist` 要不要按 Q5 的结论改写用例？」），本页是它在终态树上重新量到的读数。

## 四、请裁定

1. `static_types` 本 lane 的改写（值保秩 + 裸 `Tensor` 结果进自己的绑定 + 新增 `g=10.0` 读数）是否
   就是 `:323` 的本意？若不是，P6／P7 两种写法都已实测能收口且断言逐字不变，可直接指定一种。
2. `lrit`／`mnist` 这四条要不要改？两条路都已实测能编译（P6/P7 同形），代价是二选一：
   把教学示例的格子写成 `Tensor`（放弃 rank 检查），或把 `train` 的返回值挪到自己的绑定
   （`w` 之后不再承载训练结果）。**不改**也是一条路：这两条用例的期望目前只有 `expect_exit = 0`
   （`screens.plan.toml` 那两条连期望都没有），终态下它们到底「本来就该报错」还是「该绿」，需要一句裁定。
3. `:323` 与 `:784` 要不要按实测补一句：「返回裸 `Tensor` 的调用进不了写明元素类型的 `var` 格子，
   与加宽规则无关」？

## 五、同一族的两个慢层读数（集成 lane 补，只事实）

同样「裸 `Tensor` 进不去写明元素类型的位置」的形状，在慢层还有两处，它们比上面四条更重：**用例根本
编译不过**（不是期望问题），而 develop 上同样两个程序都能跑。

`test/cases/feat/mm/gc/gc_training_peak.cml:29`（`let next = sgd(wf, g, 0.5)`）：

```
error[SemanticDiag::NoMatchingFunction]: No matching function found for argument types: <>
(f32[?, 4], Tensor, float).
    --> test/cases/feat/mm/gc/gc_training_peak.cml:29:23
    <nn:sgd>: (params: P, grads: grad of P, lr: float) => P
```

- 逐字读数：`tip=c5c506e44` 上 `camel gc_training_peak.cml --arg steps=2 --arg batch=64 --arg width=64
  std::nvm` 直接报上面这条；同一命令在 develop（`tip=0a1a509f5`）打印
  `gc_training_peak base=18056 peak=28808 low=22664 released=0 loss=0.6931471805599647` 并退出 0。
- 参数里 `wf = zeros([width, 4])` 的类型是 `f32[?, 4]`（`width` 是运行时实参），
  `g = loss_grad<wf>(X, labels)` 的类型是**裸 `Tensor`**；`nn:sgd` 的签名印在
  `modules/nn/optim.cpp:567`，`P` 与 `grad of P` 都绑到第一个实参的类型上，裸 `Tensor` 与
  `f32[?, 4]` 不统一 ⇒ 拒绝。`test/plans/feat/mm/gc_pacing.plan.toml:487`／`:521` 两条
  （`gc_pacing.training_400`／`.training_800`）因此在慢层红，连续两次（`188.00 ms`／`180.22 ms`
  与 `129.56 ms`／`209.13 ms`），**是编译期拒绝、不是抖动也不是过期期望**。
- 它与本页第三、四节的 `lrit`／`mnist` 同一族（裸 `Tensor` 与未定元素类型的位置），所以问题也同一处：
  `grad of P` 该不该接受「未定元素类型的梯度」这种裸 `Tensor`。本文只记读数。