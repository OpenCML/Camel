# 槽编码审计：窄值写槽必须零扩展

只读审计。结论全部由 `git grep` 与源码原文得出，不含推测。

## 0. 规则与基准

- 规则：比一个槽窄的值只占低字节，高半不是值的一部分；写进槽时必须零扩展。
- 基准实现：`src/kernel/include/camel/core/rtdata/base.h:136-155` 的 `toSlot<T>`，三个分支：
  - `:141` `T == slot_t`：原样返回（不窄化）
  - `:146` 同宽类型：`bit_cast`
  - `:148-152` 窄类型：`memcpy` 低字节进一个清零的 `slot_t`
- 同一规则在别处也写着：`src/kernel/runtime/writings.cpp:156-174` `canonicalConstant`，注释原文：
  「A slot is 64 bits, but an f32 or an i32 occupies the low 32 and a bool the low 8; the other bytes
  are no part of the value.」

## 1. 已修的那一处

`src/kernel/core/rtdata/conv.cpp:66-69`（`makeSlotFromPrimitiveData`，`case TypeCode::Int32`）曾为：

```cpp
return static_cast<slot_t>(intData->data());   // int32_t -> slot_t，符号扩展：-3 成 0xfffffffffffffffd
```

现为 `return toSlot<int32_t>(intData->data());`。同一函数的 `case TypeCode::Float32`（`:76-85`）本来就把高半清零并写了理由，两种口径不一致正是"同一个 -3 有两种编码"的来源。

## 2. 全仓 `static_cast<slot_t>` 逐处（`src/` 全搜，11 处）

| # | file:line | 原句 | 值窄否 | 判定 |
|---|---|---|---|---|
| 1 | `src/kernel/core/mm/poison.cpp:87` | `poisonWord.store(static_cast<slot_t>(base + kGuardBytes / 2), …)` | 否（地址，全宽） | 正确 |
| 2 | `src/kernel/core/mm/poison.cpp:109` | `poisonWord.store(static_cast<slot_t>(0xDEADF00DDEAD0000ULL), …)` | 否（全宽常量） | 正确 |
| 3 | `src/kernel/core/rtdata/array.cpp:622` | `return static_cast<slot_t>(value);`（`uint32_t value`，`memcpy` 得来） | 是，但**无符号源** | 正确（零扩展） |
| 4 | `src/kernel/core/rtdata/array.cpp:627` | `return static_cast<slot_t>(value);`（`uint16_t value`） | 是，无符号源 | 正确（零扩展） |
| 5 | `src/kernel/core/rtdata/array.cpp:630` | `return static_cast<slot_t>(*at);`（`at` 是 `const std::byte *`） | 是，`std::byte` 取值 0..255 | 正确（零扩展） |
| 6 | `src/kernel/core/rtdata/conv.cpp:78` | `return static_cast<slot_t>(longData->data());`（`Int64` 分支） | 否（全宽） | 正确 |
| 7 | `src/kernel/core/rtdata/conv.cpp:116` | `return static_cast<slot_t>(boolData->data() ? 1 : 0);` | 是，但值是 0/1 | 正确（零扩展） |
| 8 | `src/kernel/include/camel/core/rtdata/base.h:144` | `return static_cast<slot_t>(reinterpret_cast<std::uintptr_t>(value));`（`T == slot_t` 分支） | 否（整字/对象指针） | 正确（设计如此） |
| 9 | `src/kernel/runtime/writings.cpp:168` | `return static_cast<slot_t>(static_cast<std::uint32_t>(value));`（Int32/Float32 归一化） | 是，先转无符号 | 正确（零扩展） |
| 10 | `src/kernel/runtime/writings.cpp:170` | `return static_cast<slot_t>(static_cast<std::uint8_t>(value));`（Bool 归一化） | 是，先转无符号 | 正确（零扩展） |
| 11 | `src/stdlib/std/fvm/jit/runtime/trampoline.cpp:756` | `return static_cast<slot_t>(arm);`（`size_t arm`，`selectBranchArm` 的返回值） | 否（非负小整数） | 正确；`：755` 写槽用 `fromSlot<Int32>(arm)`，两处位型一致，写法不对称 |

判定链：一个 `static_cast<slot_t>` 只有在"源类型是有符号且窄于 64 位"时才会符号扩展。上表里唯一符合这一形状的是 `Int64` 分支（不窄）与 `std::byte`/`uint*`（无符号）；**对 `int32_t` 的 `static_cast<slot_t>` 已经 0 处**。

## 3. `toSlot<T>` 与它的调用面

- 实现见 §0；`toSlot<…>` 在 `src/` 中共 240 行调用。
- `:141` 的 `T == slot_t` 分支原样返回是必需的：它承担对象指针与整字值。责任在**调用点传窄类型**，不在这个分支。
- `:148-152` 的窄分支对 `i32`/`bool`/`f32`/`bf16`/`f16` 都适用，不需要按类型再加分支。

## 4. 其他"常量 / 字面量 → 槽"的入口

| 入口 | 用的转换 | 判定 |
|---|---|---|
| `src/kernel/core/rtdata/conv.cpp:54` `makeSlotFromPrimitiveData` | 按 `typeCode` 分派，i32 现走 `toSlot<int32_t>` | 已修 |
| `src/kernel/core/rtdata/conv.cpp:171/188/206/225/243/287` | 数组/元组元素也走同一函数 | 继承修复 |
| `src/compile/src/gir/draft_graph_builder.cpp:146-147` `addStaticData`、`:161-168` `addStaticSlot` | 原样交给 `appendStaticSlot` | 编码由上游算对 |
| `src/kernel/runtime/draft.cpp:2734` `appendStaticSlot`、`:2746` `materializeStaticValue` | 原样存 | 编码由调用方算对 |
| `src/kernel/runtime/draft_clone.cpp:388/555` | 复制已有静态槽位 | 已是编码，原样 |
| `src/kernel/runtime/definitions.cpp:126-135/551` | `toSlot<int64_t>` / `toSlot<double>` / `toSlot<bool>`，各带自己的 `Type` | 一致 |
| `src/kernel/runtime/writings_match.cpp:56/139/155` | 先 `canonicalConstant` 再存/比 | 已归一 |
| `src/kernel/include/camel/core/rtdata/half.h:257` `halfSlotOfBits` | `uint16_t` `memcpy` 进清零槽 | bf16/f16 正确 |
| `src/kernel/include/camel/core/context/frame.h:190` `set<T>` | 宽度随 `T`，窄类型走 `toSlot` | 责任在调用点传对 `T` |

## 5. 结论

除 `src/kernel/core/rtdata/conv.cpp:68` 外，同类缺陷 0 处。

同形未证的一条：`canonicalConstant`（`writings.cpp:161-174`）只覆盖 `Int32`/`Float32`/`Bool`，不含 `bf16`/`f16`。但半精度槽由 `halfSlotOfBits` 构造、紧凑数组读也零扩展，当前没有路径能产生带脏高半的 half 值，因此不记为缺陷。

## 6. 用什么读能证每一处

- i32/bool/f32 常量：`std::jgir` 打出的 DATA 节点 `label`（`"label": "-3"` 一类）与其槽的十六进制；修前是 `0xfffffffffffffffd`，修后应是低半有值、高半为零。实测读数：删除 `equality.cpp` 的 Int32 比较分支后，`i32_compares_by_number` 在 `std::nvm`/`std::fvm`/`std::jit` 三后端仍全 PASS。
- bf16/f16：现有 half 用例 + `std::gir` 的 `type=` 标注，读同一常量两次比较是否相等。
- 数组叶读（`array.cpp:615-630`）：紧凑数组用例读出的元素与同值标量常量比较，相等即证零扩展一致。
- 常量池（`appendStaticSlot`）：`std::jgir` 的 DATA 节点即常量池的可见面，槽值与其 `outputType` 一致即证。

## 7. 草稿区：第三件（等 `lane/i32-encoding` 合入后落地，不提前开枝）

形状照 `test/plans/feat/opt/generic.plan.toml:905-924`：解析 `std::gir` 的 DOT，取 subgraph 的 `label` 当图名，数 `$spec` 图；只断言"只有一个"，不断言编号。

用例（`test/cases/opt/generic/` 一类目录下）要点：

```
func f(x: i32) => i32 { return x }
func main() sync {
    let z: i32 = 0
    let m: i32 = -3
    println(f(-3) == f(m + z))     // 同一个 -3：字面量一路、i32 算术一路
}
```

计划条目要点：

- 遍：`std::cxx::specialize`，随后 `std::gir`
- `verify_script` 取图名集合，筛 `startsWith('f$')`
- 断言：该集合长度为 1；**不断言具体是 `$spec0` 还是别的编号**
- 等价关系写进注释与提交说明：同一 `$spec` 图名 ⇔ 同一个 `ProgramSpecializationKey` ⇔ 该常量在槽里的同一编码。这条是 `ProgramSpecializationKeyHasher` 哈希整字、两种编码两个 key 的直接后果。

状态：**等合并后落地**，本页只留草稿。