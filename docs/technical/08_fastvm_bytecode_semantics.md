# FastVM 字节码执行语义

本文档以解释器实现（`gotovm.cpp`）为基准，说明 FastVM 关键字节码的**执行细节**与**与 JOIN 的约定**，供解释器与 JIT 保持一致行为时参考。

---

## 1. 字节码布局（简要）

- **Header**（8 字节）：`opcode`、`opsize`（以 8 字节为单位）、`result`、`fastop[0]`、`fastop[1]`。
- **变长指令**（BRCH、JOIN、FUNC、OPER 等）：紧跟 header 为 `operands[]`：
  - `normCnt = fastop[0]`，`withCnt = fastop[1]`；
  - `nargs() = operands[0 .. normCnt-1]`；
  - `wargs() = operands[normCnt .. normCnt+withCnt-1]`。
- **data_idx_t**：正数 = 动态区 slot 索引；负数 = 静态区索引的相反数；0 = 无效。

详见 `src/builtin/passes/sched/linear/fastvm/bytecode.h`。

---

## 2. BRCH（分支）

### 2.1 语义

根据**条件**选择控制流跳转到若干目标之一；执行时**必须**把「所选分支的索引」写入 `bc->result` 槽，供后续 **JOIN** 使用。

### 2.2 普通 if-else（withCnt == 0）

- **nargs[0]**：条件槽（bool），由前序指令（如比较）写入。
- **执行**：
  1. 读 `condData = currFrame->get<bool>(nargs[0])`。
  2. `jumpIdx = condData ? 0 : 1`：
     - **条件为真** → `jumpIdx = 0`（走**第一分支**，即 then）；
     - **条件为假** → `jumpIdx = 1`（走**第二分支**，即 else）。
  3. **关键**：`currFrame->set(bc->result, fromSlot<Int32>(jumpIdx))`，即把**分支索引**（0 或 1）写入 `bc->result` 槽。
  4. `pc += bc->opsize + jumpIdx`，跳转到对应分支的第一条指令（通常该分支末尾会 **JUMP** 到 JOIN）。

### 2.3 match-case（withCnt > 0）

- **nargs[0]**：被匹配的值所在槽。
- **wargs[j]**：各 case 的匹配值（或用于类型/对象比较）。
- **执行**：按顺序与各 case 比较，找到匹配则 `jumpIdx = j`；若无一匹配则 `jumpIdx = withCnt`（else/默认分支）。
- 同样在跳转前执行：`currFrame->set(bc->result, fromSlot<Int32>(jumpIdx))`。

### 2.4 与 JOIN 的约定

- **BRCH 的 `bc->result`** 与 **JOIN 的 `nargs()[0]`** 在编译时约定为**同一 slot**（例如 if-then-else 中 BRCH 的 result 与 JOIN 的“分支索引”槽一致）。
- BRCH 必须写入的是**分支索引**（0 = 第一分支，1 = 第二分支，…），**不是**条件的 0/1 值。
- JIT 实现必须与解释器一致：例如 if-else 下「条件为真 → 写 0」「条件为假 → 写 1」；若写成「条件为真 → 写 1」「条件为假 → 写 0」，JOIN 会选错分支，导致错误结果（如 fib 返回 n 而非递归结果）。

---

## 3. JOIN（汇合）

### 3.1 语义

从多条控制流汇合到一点，根据 **BRCH 写入的分支索引** 从多个候选值中选一个写入 `bc->result`，即 **φ 节点** 的语义。

### 3.2 执行

1. **nargs[0]**：存分支索引的槽（由 BRCH 在跳转前写入）。
2. `brIndex = currFrame->get<int32_t>(nargs[0])`，要求 `0 <= brIndex < withCnt`。
3. `result = currFrame->get<slot_t>(wargs[brIndex])`，即选第 `brIndex` 个候选槽的值。
4. `currFrame->set(bc->result, result)`。

### 3.3 if-then-else 下的典型布局

- **withCnt == 2**：`wargs[0]` = then 分支的结果槽，`wargs[1]` = else 分支的结果槽。
- 因此：
  - BRCH 写 **0**（走 then）→ JOIN 取 `wargs[0]`（如基例的 `n`）；
  - BRCH 写 **1**（走 else）→ JOIN 取 `wargs[1]`（如递归的 `fib(n-1)+fib(n-2)`）。

---

## 4. 小结

| 步骤       | 责任   | 说明 |
|------------|--------|------|
| BRCH 执行  | 写槽   | 将**分支索引**（0/1/…）写入 `bc->result`，再按索引跳转。 |
| 各分支执行 | 写槽   | then 分支写 then 结果到约定槽（如 wargs[0] 对应 slot），else 分支写 else 结果到约定槽（如 wargs[1] 对应 slot）。 |
| JOIN 执行  | 读+写  | 从 `nargs[0]` 读分支索引，从 `wargs[brIndex]` 取值写入 `bc->result`。 |

解释器与 JIT 在「BRCH 写入的是分支索引、且 0=第一分支、1=第二分支」这一约定上必须一致，否则 if-then-else 的返回值会错误。
