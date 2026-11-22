# 协调者手上的全部工作

格式与规则见 `internal/README.md`。从 `inbox.md` 接手的条目用原编号，自己发现的用 `C-日期-序号`。

| 编号 | 来源 | 内容 | 车道 | 状态 | 更新时间 | 完成提交 |
|---|---|---|---|---|---|---|
| C-1123-1 | `lane/stage2-frontend-oper` 的实测 | autodiff 没有 `:op/access_key` 的导数规则：`no derivative rule for operator ':op/access_key' of type 'float[4, 2]'`，tuple 那几处梯度算成 `(0.0)`；9 处（`transpose_tuple_component` 4、`transpose_loop_escape` 2、`transpose_no_tangent.tuple_member`、`jvp_loop.struct_state_is_lifted`、`transformer_step_gradient`、`stated_extents/sum_axis_in_a_training_step`） | 待派 | open | 2026-11-23 01:20 | — |
| C-1123-2 | 同上 | `std::cpp` 不认识 `:op/access_key`：`uses the operator ':op/access_key' on a value of type 'P', which has n…`；`entry_args/cpp_struct_refused` 与 `feat/trans` 的 `print_shape`、`enum_types`、`recursive_enum_types` 共 4 处，另 `static_expansion/` 2 处 | 待派 | open | 2026-11-23 01:20 | — |
| C-1123-3 | 同上 | 按 kind 数成员读的校验判据要改：`Error: field accesses changed:` 与 `the caller does not project the returned tuple: 0`，`port/fill metadata: PASS` 不再出现。**起因是闸门按 family 判而非按 kind 判**，判定改窄后本该绿的项翻了 | 待派 | open | 2026-11-23 01:20 | — |
| C-1123-4 | 同上 | 宏求值器不填 `ArgsView::resultType()`：`src/stdlib/std/macro/eval.cpp` 的 `tryExecuteStaticOper` 与 `executeGraph`（`std::nvm`/`std::fvm`/`std::prl`/`kernel/runtime/rules.cpp` 都填），15 个 `killed` 全是它撞 `cast.cpp:318 Guarantee: target != nullptr`。**翻前翻后同数，是既有缺口，不是翻出来的** | 待派 | open | 2026-11-23 01:20 | — || C-1123-5 | 分支卫生审计 | `lane/stage2-frontend-flip`（2 笔 / 落后 87）**已被取代，可删**：`aa9550c33` 的三个算子声明 = 已合的 `801e22a43`（`src/stdlib/prelude.cml` 5 处 `:op/cast|fill|access|access_key` 齐全）；`4aea1fd90`（换算翻转）= `fo1` 的 `060091eb5`；成员读 = `fo1` 的 `72df5049c`。它改的 `lib/…` 是搬家前路径 | 收车道 | resolved | 2026-11-23 01:25 | — |
