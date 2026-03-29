# Inline Pass Test Cases

本目录用于覆盖 `std::inline` 的关键结构语义回归点，重点关注：

- **策略化目标选择**：`std::inline::small`（非 arm 小子图）/ `std::inline::arm`（arm 小子图）/ `std::inline::hybrid`（两者并集，`std::inline` 默认等价 `hybrid`）；
- **SCC 入口保护**：若 SCC 内调用目标属于“被 SCC 外调用的入口 callee”，则默认不内联，避免递归入口向内反复展开；
- 入口 `ctrlEntry` 收敛规则（单目标不建 `SYNC`，多目标建 `SYNC`）；
- 无参数函数的 `exitNode`（值出口锚点）反向入口推导；
- 内联后“一次性子图”注册表清理（`subGraphs/dependencies` sweep）；
- 分支场景下内联后值/控路径保持正确。

## Recommended Manual Checks

对每个 `.cml` 建议至少执行两组命令：

```powershell
camel "<case>.cml" std::inline std::gir
camel "<case>.cml" std::inline std::fvm
```

其中：

- `std::fvm` 用于行为正确性（返回码与打印）；
- `std::gir` 用于结构检查（是否仍残留一次性子图、是否出现异常 SYNC 扇出）。

策略回归建议附加执行：

```powershell
camel "inline_strategy_small_non_arm.cml" std::inline::small std::gir
camel "inline_strategy_small_non_arm.cml" std::inline::arm std::gir
camel "inline_strategy_hybrid_mix.cml" std::inline::hybrid std::gir
camel "inline_strategy_hybrid_mix.cml" std::inline std::gir
camel "inline_scc_entry_guard_recursion.cml" std::inline std::gir
camel "inline_value_ctrl_gate_anchor.cml" std::inline std::gir
```

