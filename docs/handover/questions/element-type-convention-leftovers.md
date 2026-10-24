# 元素类型约定：本车道实测后仍红的项，逐项分类

车道：`handover/h459-flip-kernels`（族=内核与示例，题=「未述元素类型的值按数组/字面量规则取 `f64`，而模块内核按 `f32` 约定」）。
本文件只记录**实测**与**归属**，不为过渡类型发明语义。落点：`a0a113e69`（族基线）→ `e1de421b3`（本车道两处修复之后）。

## 1. 裸 `Tensor` 形参 / 返回值与写明元素类型的格子（`lrit`、`mnist`）

逐字读数（族基线，`test/plans/feat/modules/nn.plan.toml`）：

```
[FAIL] lrit
    [stdout] lrit.cml:78:14: [Error]: No matching function found for argument types: <> (f32[?, ?], Tensor);
             operator '=' is not defined for these operand types.
[FAIL] mnist
    [stdout] mnist.cml:92:14: [Error]: No matching function found for argument types: <> (f32[?, ?], Tensor);
             operator '=' is not defined for these operand types.
```

判定点：

* `test/cases/modules/nn/lrit.cml:71` `var w = zeros([num_features + 1, 1])` → 格子类型 `f32[?, ?]`；
  `:28` `sync func update(var w: Tensor): Tensor`、`:39` `sync func train(weights: Tensor): Tensor`
  → 结果类型是**未述元素类型**的数组（`ArrayType::numericUnknown()`）。
* `:78` `wait w = train<...>(w)` 落在 `=` 上：`assignableFrom(f32[?, ?], Tensor)` 为假——
  目标写明元素类型、来源未写明，是**收窄**方向，按终态语义应当拒绝。
  `mnist.cml:88,92` 同形。

两条可能的解法，本车道**都没有做，也不该由本车道硬修**：

1. **语言侧**：裸 `Tensor` 形参按调用点实例化（设计原则 5.5）。
   `docs/handover/reviews/questions-2026-10-23.md:375` 已裁定「必须。这是设计原则 5.5 本身」，
   `docs/handover/questions/bare-tensor-parameter-instantiation-measured.md` 是它的实测页。
   该轨落成后这两条自然变绿，**用例一行不用改**。
2. **用例侧**：把两个示例的整条签名链（`predict`/`update`/`train`/`train_step`/`build_one_hot`）
   改写成 `Tensor<f32, [?, ?]>`。本车道不做：那样会把两个教学示例写成非泛型，
   且 `train_step` 的递归返回也要一起写死——正是「为过渡类型发明语义」的反面（改示例去迁就别名）。

结论：**归 1 号轨（裸 `Tensor` 形参实例化）**，本车道只登记不改。

## 2. 五条「必须拒绝」的用例（拒绝已是死代码）

`slice_grad_refuse_reshape_array`、`slice_grad_refuse_concat_array`、`refuse_new_array`、
`refuse_cast_array`、`concat_array_part.skipped_and_refused`：`expect_exit = 1`，实测 `exit 0`。

判据：`modules/tensor/ops/vjp.cpp:67` `isTensorNode` = `isTensorType` = `isNumericArrayType`，
别名之后**数值数组就是张量节点**，所以
`creation_ops.cpp:153`（`new`）、`layout_ops.cpp:642`（`reshape`/`flatten`/`unsqueeze`）、
`:688`（`concat` 的数组部分）、`elementwise_ops.cpp:499`（`cast`）里的
`!isTensorNode(...)` 分支恒不触发。`refuse_cast_array` 已改由元素类型规则报出来：

```
[FAIL] refuse_cast_array
    [stdout] The gradient of a value of type 'float[]' has element type 'f32': operator 'tensor:cast_like' produced it
```

归属：`docs/handover/reviews/questions-2026-10-24-evening.md`（`adjoint-of-a-numeric-array-after-the-alias`）
裁定——**删掉这些恒不触发的拒绝的车道，在同一个提交里把这五条用例改写成梯度值与手写伴随的逐位比较**。
本车道不重复该轨，也不改这五条：它们断言的正是那批拒绝的存在。

## 3. bf16/f16 的 `%`（协调者 2026-10-25 转来的裁定）——**已做**

裁定原文（转述）：「别名之后没有『张量参数』与『数组参数』之分，只有数值数组；不要找判据去区分它们……
bf16/f16 是与 f32 同等的浮点类型（7.3），数值数组上的 `%` 对它们应当支持（逐元素，在 f32 中算、舍入一次）
——今天的 `ReducedPrecisionUnsupported` 是如实标出的实现缺口，补上内核后消失。」

落点与读数（逐字，同一份用例 `test/cases/modules/tensor/dtype/bf16_modulo.cml`）：

* **红前**（注册前重建后跑同一个用例）：
  `bf16_modulo.cml:35:33: [Error]: The call of 'operator %' cannot take a value of the reduced-precision element type 'bf16'. ...`
  `bf16_modulo.cml:41:37: [Error]: The call of 'operator %' cannot take a value of the reduced-precision element type 'f16'. ...`
  `bf16_modulo.cml:47:49: [Error]: No matching function found for argument types: <> (float[], float[]); operator '%' is not defined for these operand types. ...`
  `bf16_modulo.cml:51:48: [Error]: ... (i32[], i32[]) ...`（候选只有 `:op/mod_i`、`:op/mod_l`）
* **红后**（`bf16_modulo.nvm/.fvm/.jit/.prl` 四条全 PASS）：
  `bf16 values: 1.5 -1.5 1.5 1.0` / `bf16 bytes: 8 [192, 63, 192, 191, 192, 63, 128, 63]`
  `f16 values: 1.5 -1.5 1.5 1.0` / `f16 bytes: 8 [0, 62, 0, 190, 0, 62, 0, 60]`
  `f64 value: -1.5` / `i32 value: -1` / `mixed scalar: -1.5 15.0`

补的东西（`41fc9549e`）：`BinaryOp` **末位**加 `Mod`（`elementwise_ops.cpp` 的算子名表按枚举下标索引，
只能追加）、逐元素内核（bf16/f16 在 f32 中算、存回舍入一次；float 走 `std::fmod`；整数走语言自己的
截断余数 `camel::utils::remainder`，即 `%` 不取整）、注册
`binaryDef<Mod>("mod", {"__mod__","mod"})`。标量 `%` 不在范围内、也未受影响：`binaryInfer` 要求至少
一个张量操作数，两个 bf16 标量仍然无人可匹配，`stdlib_reduced.plan.toml:87-97` 那条原样通过。

**注册一个新算子必须同时声明它**：加完注册，整层立刻多了两条红（`registry.signatures_match`、
`registry.migrations_stay_migrated`，逐字 `stdlib/tensor.cml: 'mod' (tensor:mod) is registered but not
declared` / `1 difference(s) between the declarations and the registered operators`），检查器是
`scripts/stdlib-reference.mjs:1583`。按 `divide`/`pow` 同一格式在 `stdlib/signatures/tensor.cml` 补上
`export func mod(lhs: Tensor | number, rhs: Tensor | number): Tensor`（含 `@pure`、`@example`）即消失；
`stdlib/tensor.cml` 那页不用动（它只声明 shape/bytes/模块状态那些，不声明逐元素算子）。

### 3.1 顺带发现并修的：约简精度标量操作数被当成 bool 读（**跨车道，可按需丢弃**）

`modules/tensor/kernels/elementwise.cpp` 的 `ScalarOperand::fromSlot` 只处理 f32/f64/i32/i64，
bf16/f16 落到 `default:` 走 `scalarToBool`，于是「约简元素类型数组 与 字面量」的**每个**二元算子
都用 0/1 参与运算（不是 `%` 独有）：

```
(new(bf16[7.5, -7.5, ...]) % 2.0)[1]   ->  -0.0      应为 -1.5（实为 fmod(-7.5, bf16(0x0001))）
(new(bf16[7.5, -7.5, ...]) * 2.0)[0]   ->   7.3e-40  应为 15.0（实为 7.5 * bf16(0x0001)）
修后：mixed scalar: -1.5 15.0
```

修法：`ScalarOperand` 的 union 加 `uint16_t u16`，bf16/f16 槽照写保存两字节（`half.h` 就是把它存在
槽的低半）。单独一个提交（`16e3a2d25`）；若约简精度/张量内核那条车道已在改同一个函数，可直接丢掉这
一个提交，同时回退 `41fc9549e` 中用例的 `mixed scalar` 一行。

## 4. `resident_loop_declares_its_blocks`（整层 `Compare-Object` 里唯一新增的红）

归属：**不是本车道**，A/B 已证。

* 整层基线（`a0a113e69`）**没有发现任何 `gpu.*` 计划**（`[PLAN]` 里 `gpu` 零条），
  修复后整层那一次发现了 `gpu.tiles` 与 `gpu.profile`（主机上设备可见了）。
* 带本车道改动跑 `test/plans/gpu/tiles.plan.toml`：`total: 101, pass: 88, fail: 13`，
  其中 `[FAIL] resident_loop_declares_its_blocks`；
  把本车道 5 个文件 `git checkout --` 掉重建后重跑同一条计划：`total: 101, pass: 88, fail: 13`，
  `[FAIL] resident_loop_declares_its_blocks` **逐字相同**。
* 读数：`TILES_PLACEMENT {...,"gpu":0,"gpu_required":0,...}`、`TILES_RESIDENCY {...,"body":"host",...}`，
  期望 stdout 含 `Resident loop: PASS`。归 tiles/放置那一轨。