# 转置解析器的新签名：绿 15 条，不是 29 条

- 状态：待作者确认数字（2026-11-02 14:30 提出）
- 涉及：`modules/transforms/transpose_operator.cpp`、`modules/transforms/transpose.cpp`、`stdlib/vjp.cml`

## 作者的原话（14:10）

> 那 29 条红是 `TransposeResolver` 还没换新签名：`ef60bd42a` 查明它们都停在 `stdlib/vjp.cml:73:47`，
> 原因是「旧的转置类型遇上新的单余切调用」，不是 `with_grad`。……`modules/transforms/transpose_operator.cpp:110`
> 仍写着 `(knowns.., cotangent) => (cotangents..)`，照 11:30、13:10 的约定改成 `(ȳ) => (每个参数一项的余切)`，
> 类型只由参数函数的类型决定。**读数：29 条转绿。**

## 实测（`lane-b2`，`d4f50f500`）

1. 那一行在本分支上**已经是新签名**：`transpose_operator.cpp:111-114` 返回
   `(f: (float) => float | (float, ...) => float) => (cotangent) => (cotangents..)`，随本车道那一笔
   `ef7aa1e92` 落的；develop 上仍是旧串，因为它只在这一笔里改过。
2. 那个 `signature()` 只是打印的拼写（`REINTERP`/拒绝说明用），它本身不带绿；动用例的是
   `transposeSignature`（`transpose.cpp:117-161`），也在同一笔里。
3. 全量（本分支、当前 sha）：`total: 8522, pass: 8467, fail: 35, skip: 10, known failures: 10`。
4. develop 那 29 条在本分支上**绿 15 条**：`vjp_arities`×4、`vjp_api`×8、
   `grad_where_the_tangent_ignores_the_operands`、`grad_array_with_int.fvm`、
   `tools.autodiff_migration_ledger.ratchet`（它的 `pass` 回到记录的 12）。
5. **11 条没绿，根不是签名**：
   - `grad_array_no_int.fvm`、`grad_of_an_array_parameter.nvm`、`grad_array_one_element.nvm`、
     `grad_array_two_elements_first.nvm`、`grad_array_sum.nvm`、`grad_array_product.nvm`、
     `grad_of_a_division.nvm` —— 停在
     `TRANSPOSE function=rulesets.jvp::index_into<float> skip=cotangent-needs-a-value uri=:op/idx_arr position=0`
     与 `TRANSPOSE function=autodiff::div_tangent<float> skip=cotangent-needs-a-value uri=:op/div_d position=1`，
     两句的 `reason` 都是 `the rule's right side reads a value the caller does not carry`。
   - `split_branch_author_vjp.nvm/.fvm/.jit/.prl` —— 停在
     `TRANSPOSE function=split_of_jvp_of_f_backward arm-stop region=1 node=6 arg=0 callee=autodiff::mul_tangent<float>`。
   两族共同的根是「一个参数只能是端口（值）或答案（余切），不能两者都是」，即捕获缺口那一件。
6. 另有 22 条 `transpose_*` 在本分支上变红，那是**用例写法**还锁着旧拼写（`test/plans` 里的类型串），
   归 `lane-casemig`（23 文件 / 50 处，逐条表 `76fd69924`），与签名无关。
7. 第 5 条那 11 条的捕获缺口，插桩实测（成员构建之前；`i` 是 `div_tangent` 的参数号）：

   ```text
   TRANSPOSE-PRE function=split_of_jvp_of___g_63___backward callee=autodiff::div_tangent<float> i=0 forwarded=1
   TRANSPOSE-PRE function=split_of_jvp_of___g_63___backward callee=autodiff::div_tangent<float> i=1 forwarded=1
   TRANSPOSE-PRE function=split_of_jvp_of___g_63___backward callee=autodiff::div_tangent<float> i=2 forwarded=0
   TRANSPOSE-PRE function=split_of_jvp_of___g_63___backward callee=autodiff::div_tangent<float> i=3 forwarded=1
   ```

   成员里要的是源参数 3（`db`）的值，而 `callInputs[3]` **forwarded=1**——值在调用点拿得到；
   但 `callInputs[2]`（`da`）**forwarded=0**，所以**「把每个参数的值都打进 bundle」已排除**：它会在
   `i=2` 落成 `skip=call node arg=…`，即把一种拒绝换成另一种。形状因此收窄为**两遍**：第一遍走成员
   构建，把 `param=N` 这类「要值」记下来而不中止；第二遍按那个集合开捕获端口，调用点只 forward
   集合里的参数（集合里若有一个 forwarded=0 就保持原来的拒绝，不填 0）。两遍已批准、正在落，
   改动不触及用户可见的 `transpose(g)` 的签名。

## 我方默认

按第 7 条那个两遍形状落捕获（**不是**全参数 bundle），目标是关掉第 5 条那 11 条；
**不为了凑「29 条绿」去改用例或改期望值**，也不把第 6 条那 22 条迁移件算进这一笔。