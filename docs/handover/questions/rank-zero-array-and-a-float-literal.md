# 秩 0 数组与 Camel 浮点字面量相遇时算在哪个元素类型（`declaration_evaluator` 的 43 条 `dtype differs`）

本文只报选题与读数，不定语义。作者页面（`docs/design/**`、`docs/design-principles*.md`、`docs/language/**`、`docs/handover/reviews/**`）一个字未动。

## 1. 读数（`test/plans/feat/modules/index_semantics.plan.toml:126`）

- 翻转线（本集成树，tip 见提交）：`[FAIL] declaration_evaluator`，`test/plans/feat/modules/index_semantics.plan.toml:149` 的 `assert(t.unsound === '0' && t.mismatches === '0' && t.undeclared === '0', total)` 不成立。
- 逐字（宿主实测，`camel <case> tensor::test_index_eval` 的输出）：
  - `EVAL_MISMATCH op=tensor:add (f32[], -2.895099639892578): dtype differs (kernel f64, declaration f32)`
  - `EVAL_MISMATCH op=tensor:add (3.217700719833374, f32[]): dtype differs (kernel f64, declaration f32)`
  - `EVAL_MISMATCH op=tensor:add (f32[], 3.0211567878723145): dtype differs (kernel f64, declaration f32)`
  - `EVAL_MISMATCH op=tensor:add (3767.09326171875, f32[]): dtype differs (kernel f64, declaration f32)`
  - `EVAL_MISMATCH op=tensor:subtract (1.4355849027633667, f32[]): dtype differs (kernel f64, declaration f32)`
  - 汇总行：`EVAL total operators=33 cases=2780 agree=2737 faults=203 points=53887 unsound=0 mismatches=43 undeclared=0`
  - `(f32[], -2.895…)` 里 `f32[]` 是**秩 0**（`formatShape({})` 打 `[]`；生成器 `index_eval.cpp:765` 的秩可以取 0），`-2.895…` 是一个 Camel 浮点字面量（`Arg::Kind::Number`）。43 条全是这个形状：`tensor:add`/`tensor:subtract`，一侧秩 0 的 f32 数组、另一侧浮点字面量，两个方向都有。
- 同一用例在**翻转前**的 develop 线是 `[PASS] declaration_evaluator`（`D:\Temp\h465-base-layer.log`，那条线整层只红 2 条文档）。所以这条红是翻转线带来的。

## 2. 两侧各自怎么得到这个类型（file:line）

- **内核侧给出 f64**：`f32[]` 秩 0 数组与 f64 标量相遇时，两侧都被当作标量走「两个标量按加宽提升」那条路 —— `modules/tensor/kernels/elementwise.cpp:215-224` 的 `operandType()`：`if (lhs.scalar != rhs.scalar) { … 取 tensor 的元素类型 … } return promoteTensorTypes(lhs.dtype, rhs.dtype);`，`promoteTensorTypes(f32, f64) = f64`。
- **但同一份代码的注释说该给 f32**：`elementwise.h:30-33`「True for a Camel scalar: a value, not a tensor. It takes the other operand's element type when the two meet (design principles 5.1, the literal takes its context), so a `2.0` in `x * 2.0` never turns an f32 tensor into an f64 one.」；`Operand::of(const Array *)`（`elementwise.h:35-37`）把**任何**数组（含秩 0）算作 `scalar = false`，`ScalarOperand::view()`（`:56`）才算 `scalar = true`。内核实际给 f64，说明这一次调用里秩 0 的 f32 数组**没有被算成那个 tensor**。
- **声明求值器侧给出 f32**：`modules/tensor/passes/index_eval.cpp:379-387` 把 `Kind::Number` 读成 `Data{TypeCode::Float32, {}, {f32(number)}}`（注释：「a number as the kernels take it (a rank-0 f32 operand, ScalarOperand)」），`:433-441` 的 `pointwise()` 又按**第一个操作数**的类型定型（`a.type == Float32` → 结果 f32）。
- **比较处**：`index_eval.cpp:728-731`（`if (!bounded && !eval.values.empty() && kernel.dtype != eval.dtype) return "dtype differs";`）。这一段与 `Arg::operand()` 都出自 `421f244dbb`（2026-10-15，`git blame`），**翻转前后没动过** —— 会让两侧分开的是内核/类型规则那侧的改动（`elementwise.cpp:215-224` 出自 2026-10-23 的 `b7872c4c91`）。
- 同一次调用里，字面量喂给内核的是 **f64**（`index_eval.cpp:340` 的 `Arg::type()` → `Type::Float64()`、`:358` 的 `slot()` → `toSlot(double)`），喂给求值器的却是 **f32**（`:384`）。也就是说**同一个实参**在测试工具里有两个类型读法，而这一次分歧的落点正是秩 0。

## 3. 这是一个待定的语言问题，不是可以就地判死的期望

- 如果「Camel 字面量取上下文、遇到数组就取数组的元素类型」（内核自己的注释、设计原则 5.1）**在秩 0 数组上也算数**，那么这一次该是 f32：内核那条 f64 是缺陷（秩 0 数组被算成标量，于是走了「两个标量加宽」）。
- 如果「两个秩 0 的值相遇就是两个标量，按加宽提升」是语言要的答案，那么该是 f64：`index_eval.cpp:384` 的 f32 代理与用例的期望要跟着走（那是测试工具与期望，不是语义）。
- 两种终态都能让这条红消失，但**方向相反**，而且都会改到今天没有明文规定的东西 —— 秩 0 数组是翻转才成为一等形状的。所以写在这里等作者一句话，而不是二选一。

## 4. 我没有做的事

- 没有改任何语义、没有放宽 `index_semantics.plan.toml:149` 的断言、没有把 `mismatches` 改成非零、没有动 `test/cases/`。
- 只加了一条**测试工具**的诊断：`index_eval.cpp:729-731` 的报文从 `dtype differs` 变成 `dtype differs (kernel <t>, declaration <t>)`。它只把两侧的类型名放进报文（没有它就只能猜），不是断言的一部分（该计划断言的是 `!out.includes('EVAL_MISMATCH')`，前缀未变）。

## 5. 读数口径（补注）

本页的读数取自翻转线（`handover/h506b-bare-tensor`）：本页最后改动 `7b6b93cbb`，与翻转线 tip
`39784865d` 上的 blob 相同。develop 侧同一条用例今天仍是 `[PASS]`：在 `origin/develop` `c3b17207e`
之上只多 `docs/handover/` 文件的树上实测，`test/plans/feat/modules/index_semantics.plan.toml` 的
`declaration_evaluator` 为 `[PASS]`（它要求 `mismatches === '0'`）。