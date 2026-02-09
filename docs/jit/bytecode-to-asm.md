# JIT 字节码到机器码翻译注解

本文档以 `fib.cml` 在 `std::inline std::asm` / `std::inline std::lbc` 下的输出为参照，逐条说明**每条字节码如何被 x64 JIT 翻译成机器码**。适用于理解 backend 行为与调试汇编。

---

## 1. 约定与前置知识

### 1.1 标准槽模型

- 每个 **slot** 为一字（8 字节），`slot_t = uint64_t`。
- 布尔、32 位整数、64 位整数、双精度、对象指针均占**一槽**，JIT 对所有槽的访问均为 **64 位**（`mov r64, [rdi+disp]` 或 `movsd xmm0, [rdi+disp]`）。
- 动态区基址由 **rdi** 传入；槽索引 `idx` 对应字节偏移 `disp = idx * 8`，即 `[rdi + idx*8]`。

### 1.2 字节码布局（简要）

- **Header**（8 字节）：`opcode`、`opsize`（以 8 字节为单位）、`result`、`fastop[0]`、`fastop[1]`。
- **变长指令**（BRCH、JOIN、FUNC、OPER 等）：紧跟 header 为 `operands[]`，`nargs() = operands[0..normCnt-1]`，`wargs() = operands[normCnt..normCnt+withCnt-1]`，其中 `normCnt = fastop[0]`，`withCnt = fastop[1]`。
- **data_idx_t**：正数 = 动态区 slot 索引；负数 = 静态区索引的相反数（访问时用 `staticBase[-idx]`）。

### 1.3 调用约定（Win64）

- 入口：**rcx** = `slot_t* slots`，**rdx** = `void* ctx`。
- Prologue 将参数复制到 **rdi** / **rsi**，便于与 SysV 一致：  
  `mov rdi, rcx`；`mov rsi, rdx`。
- 之后所有槽访问均为 `[rdi + disp]`，disp 为 8 的倍数。

### 1.4 寄存器用途（JIT 内）

- **rax**：通用临时、算术结果、返回值、trampoline 目标地址。
- **rbx**：临时（如静态区地址、JOIN 的 wargs[0]）。
- **rcx**：参数/临时；JOIN 时存 wargs[1]，再与 rbx 做 cmove。
- **rdi**：frame 基址（slots）。
- **rsi**：ctx。
- **r8–r11**：regalloc 可用；FUNC/OPER 调用时用于传参（如 r8d=pc，r9=graph）。

---

## 2. 按字节码逐条翻译

以下顺序与 `std::inline std::lbc` 中 fib 相关图的字节码一致；汇编片段取自 `std::inline std::asm` 的典型输出（偏移以十六进制给出）。

---

### 2.1 TAIL (尾调用)

**字节码示例**：`[ 0] TAIL (3) [ 1] | [ 0,  4] | () -> 4 | main<FRl>`

- **语义**：尾调用，不返回；调用的图入口写入 `fastop[1]`（此处为 main 的 pc 4）。
- **机器码要点**：按 FUNC 相同方式设置 rcx/rdx/r8（及 Win64 下 r9），然后 `mov rax, <trampolineTail>`；`call rax`；**ret**（不再回到当前图）。

**对应汇编（__root__ 入口）**：

```asm
[3]   mov rdi, rcx
[6]   mov rsi, rdx
[9]   mov rcx, rdi      ; 第 1 参 slots（Win64 要求 call 前参数在 rcx/rdx）
[c]   mov rdx, rsi      ; 第 2 参 ctx
[12]  mov r8d, 0        ; 第 3 参 pc（TAIL 目标由 trampoline 内根据 fastop[1] 跳转）
[1c]  mov rax, 0x...    ; trampolineTail 地址
[1e]  call rax
[1f]  ret
```

**说明**：前两条是 prologue（入口时 Win64 给的是 rcx=slots、rdx=ctx，复制到 rdi/rsi 后整段 JIT 统一用 rdi 做 frame 基址）。后两条是「调用 trampoline 前按 Win64 把第 1/2 参放回 rcx/rdx」——因为这里**紧接 prologue 且第一条就是 TAIL**，rcx/rdx 还没被改过，所以这两条在语义上冗余（脱裤子放屁）；实现上可在「首条为 TAIL 时」省略这两条以优化。

---

### 2.2 RETN (返回)

**字节码**：`RETN (1) [ 0] | [ 1,  0]` 或 `RETN (1) [ 0] | [ 4,  0]`

- **语义**：从 `fastop[0]` 槽读取返回值，放入 rax 并 **ret**。
- **槽**：正数则 `[rdi + idx*8]`；负数则从静态区读（`mov rbx, imm64; mov rax, [rbx]`）。

**示例 1（从动态区返回）**：

```asm
[23]  mov rax, [rdi+8]   ; result 在 slot 1 → disp=8
[24]  ret
```

**示例 2（从静态区返回，main 末尾）**：

```asm
[137] mov rax, [rdi+-32] ; 实际编码为静态区基址 + 偏移，此处表示 result 在静态区
[138] ret
```

（注：`[rdi+-32]` 在汇编注释中表示“负 slot”访问；实际编码为 `mov rbx, <staticAddr>; mov rax, [rbx]`。）

---

### 2.3 OPER (算子调用)

**字节码**：如 `OPER (3) [ 1] | [ 1,  1] | (-2) <-1> | :str/format`

- **语义**：调用内置算子；norm 参数由 operands 指定，result 写入 `bc.result` 槽。
- **机器码**：rcx=rdi, rdx=rsi, r8d=pc, r9=graph（若需要），rax= trampolineOper，**call rax**；返回值 **rax** 写回 `[rdi + result_disp]`。

**对应汇编**：

```asm
[27]  mov rcx, rdi
[2a]  mov rdx, rsi
[30]  mov r8d, 4        ; 当前 pc（此处为 4）
[3a]  mov r9, graph     ; Graph* 用于 OPER 查找
[44]  mov rax, 0x...    ; trampolineOper
[46]  call rax
[4a]  mov [rdi+8], rax  ; result 写入 slot 1
```

---

### 2.4 FUNC (普通函数调用)

**字节码**：`FUNC (4) [ 4] | [ 1, 26] | (-1) -> 26 | fib<FN1lRl>`

- **语义**：以当前 slots/ctx 调用目标图（如 fib）；norm 参数在 operands 中，返回值写入 `result` 槽；`fastop[1]` 在链接后为目标图入口 pc（如 26）。
- **机器码**：与 OPER 类似，但调用 **trampolineFunc**，目标由 extra 或 fastop[1] 决定；返回后 `mov [rdi+result_disp], rax`。

**对应汇编（fib 内调用 fib）**：

```asm
[175] mov rcx, rdi
[178] mov rdx, rsi
[17e] mov r8d, 31       ; pc
[188] mov rax, 0x...    ; trampolineFunc / 目标 JitEntryFn
[18a] call rax
[18e] mov [rdi+48], rax ; result 写入 slot 6，disp=48
```

---

### 2.5 DSUB (双精度减)

**字节码**：`DSUB (1) [ 6] | [ 5,  3]`

- **语义**：`result = fastop[0] - fastop[1]`（双精度），每槽 8 字节；使用 SSE2 **movsd / subsd**，操作数/结果可在 slot 或寄存器。
- **机器码**：将左操作数装入 **xmm0**，再 **subsd xmm0, 右操作数**（来自 frame/静态区/寄存器），最后将 xmm0 写回 result（或 **movq** 到 GPR 若 result 在寄存器）。

**对应汇编**：

```asm
[dd]  movsd xmm0, [rdi+40]   ; slot 5 → 左操作数
[e2]  subsd xmm0, [rdi+24]   ; slot 3 → 右操作数
[e7]  movsd [rdi+48], xmm0   ; slot 6 → result
```

---

### 2.6 LLE (64 位小于等于，结果为 0/1)

**字节码**：`LLE (1) [ 2] | [ 1, -1]`

- **语义**：`result = (slot[fastop[0]] <= 1) ? 1 : 0`（当前 JIT 仅支持 rhs 常量为 1）；用于 fib 中 `n <= 1` 判断。
- **机器码**：从 slot 取左操作数到 **rax**；**cmp rax, 1**；**setle al**；**movzx rax, al**；将结果写回 result 槽。

**对应汇编**：

```asm
[13c] mov rax, [rdi+8]   ; slot 1 (n)
[140] cmp rax, 1
[143] setle al           ; al = (rax<=1 ? 1 : 0)
[147] movzx rax, al      ; 零扩展为 64 位
[14b] mov [rdi+16], rax  ; result 写入 slot 2
```

---

### 2.7 BRCH (条件分支，简单 if-else)

**字节码**：`BRCH (2) [ 3] | [ 1,  0] | (2) <>`

- **语义**：`withCnt()==0` 表示 bool 分支；读 `nargs()[0]`（条件槽），若为 true 跳 `pc+opsize+0`，否则跳 `pc+opsize+1`；并将“分支索引”0 或 1 写入 `result` 槽（供后续 JOIN 使用）。
- **机器码**：条件槽装入 **rax**；**test rax, rax**；**jz** 到 else 块；否则顺序进入 then 块；两路末尾都会**跳转到 JOIN**（见 JUMP）。

**对应汇编**：

```asm
[14f] mov rax, [rdi+16]   ; 条件槽 slot 2（LLE 的结果）
[152] test rax, rax
[158] jz .+7              ; 条件为 0 → 跳到 else（下一句 jmp 之后）
[15d] jmp .+91            ; 条件非 0 → then 块（fib(n-1)+fib(n-2) 路径）
[161] ...                 ; else 块起始（n<=1 时返回 n 的路径）
```

（实际 then/else 块内会再通过 **JUMP** 跳到 JOIN。）

---

### 2.8 JUMP (无条件跳转)

**字节码**：`JUMP (1) [ 0] | [41,  0]`

- **语义**：`fastop[0]` 为链接后的目标 pc（如 41 = JOIN 所在位置）。
- **机器码**：**jmp rel32**，rel32 由第一遍计算的 `pcToOffset` 得到。

```asm
[15d] jmp .+91   ; 跳到 JOIN 对应机器码
```

---

### 2.9 LSUB (64 位整数减)

**字节码**：`LSUB (1) [ 5] | [ 1, -2]`

- **语义**：`result = fastop[0] - fastop[1]`；此处 fastop[1]=-2 为静态区常量（如常量 1）。
- **机器码**：左操作数装入 **rax**（来自 slot 或寄存器）；右操作数从 slot 或**静态区**减到 rax（静态区用 `mov rbx, imm64; sub rax, [rbx]`）；结果写回 result。

**对应汇编**：

```asm
[161] mov rax, [rdi+8]       ; slot 1 (n)
[16b] mov rbx, 0x234f7176868 ; 静态区常量地址
[16e] sub rax, [rbx]
[172] mov [rdi+40], rax      ; result 写入 slot 5 (n-1)
```

---

### 2.10 LADD (64 位整数加)

**字节码**：`LADD (1) [ 9] | [ 6,  8]`

- **语义**：`result = fastop[0] + fastop[1]`（如 fib(n-1) + fib(n-2)）。
- **机器码**：左操作数装入 **rax**，**add rax, 右操作数**（frame/寄存器），写回 result。

**对应汇编**：

```asm
[1c3] mov rax, [rdi+48]   ; slot 6
[1c7] add rax, [rdi+64]   ; slot 8
; result 在 slot 9，若下一句为 JOIN 则可能不再单独写回 slot 9，由 JOIN 从 slot 读
```

（此处 LADD 结果在 slot 9，紧接着被 JOIN 的 wargs[1] 使用。）

---

### 2.11 JOIN (两路合并，if-else 选值)

**字节码**：`JOIN (2) [ 4] | [ 1,  2] | (3) <1, 9>`

- **语义**：`nargs()[0]` = 分支索引槽（0 或 1）；`wargs() = (1, 9)` 为两路的值槽；`result = wargs[branchIndex]`。即 `result = (branchIndex==0) ? slot1 : slot9`。
- **机器码**：  
  - 将 **wargs[0]**（slot 1）装入 **rbx**，**wargs[1]**（slot 9）装入 **rcx**（当 regalloc 将某路分到 rax 时，为避免 rax 被前一步占用，强制从 slot 再加载）。  
  - 将 **nargs[0]**（slot 3）装入 **rax**；**test rax, rax**；**cmove rcx, rbx**（若 rax==0 则 rcx=rbx）；**mov rax, rcx**；写回 result 槽。

**对应汇编**：

```asm
[1cb] mov rax, [rdi+8]    ; wargs[0] = slot 1 → rax
[1ce] mov rbx, rax        ; rbx = 分支 0 的值
[1d2] mov rax, [rdi+72]   ; wargs[1] = slot 9 → rax（disp=9*8）
[1d5] mov rcx, rax        ; rcx = 分支 1 的值
[1d9] mov rax, [rdi+24]   ; 分支索引 slot 3 → rax
[1dc] test rax, rax
[1e0] cmove rcx, rbx      ; 若 index==0 则 rcx=rbx
[1e3] mov rax, rcx        ; 选中值 → rax
[1e7] mov [rdi+32], rax   ; result 写入 slot 4
[1eb] mov rax, [rdi+32]
[1ec] ret
```

---

## 3. fib 图整体流程（与汇编对应）

以 **fib<FN1lRl>** 为例，字节码与机器码对应关系概括如下：

| 字节码索引 | Opcode | 作用           | 汇编要点 |
|------------|--------|----------------|----------|
| 26         | LLE    | n<=1 → slot2   | mov/cmp/setle/movzx，写 slot2 |
| 27         | BRCH   | 按 slot2 分支  | test rax,rax; jz/jmp 到 then/else |
| 29         | JUMP   | then→JOIN      | jmp 到 JOIN |
| 30         | LSUB   | n-1→slot5      | rax=[slot1]; sub [静态]; 写 slot5 |
| 31         | FUNC   | fib(slot5)→slot6 | call trampolineFunc; mov [slot6],rax |
| 35         | LSUB   | n-2→slot7      | 同上，写 slot7 |
| 36         | FUNC   | fib(slot7)→slot8 | call; mov [slot8],rax |
| 40         | LADD   | slot6+slot8→slot9 | mov rax,[48]; add [64]; (slot9 供 JOIN 读) |
| 41         | JOIN   | slot3? slot1 : slot9 → slot4 | rbx=slot1, rcx=slot9, test slot3, cmove, 写 slot4 |
| 43         | RETN   | slot4 → 返回   | mov rax,[rdi+32]; ret |

---

## 4. 参考命令与文件

- 查看链接后字节码：  
  `camel <path>/fib.cml std::inline std::lbc`
- 查看 JIT 汇编：  
  `camel <path>/fib.cml std::inline std::asm`
- 实现位置：  
  - 第一遍（算偏移）：`x64_backend.cpp` 中 `compileBytecode` 的 `switch (bc.opcode)`。  
  - 第二遍（发码）：同文件内第二处 `switch`；编码细节在 `x64_encoder.h`。

本文档与上述命令输出一致；若后端或字节码格式有变更，以源码与最新 asm/lbc 输出为准。
