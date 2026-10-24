# `..` 的两个操作数是泛型函数时：显式实例化可以，靠期望类型不行

## 事实（H248 组 1 切片 A，2026-10-24，实机 `std::nvm`）
- **显式实例化可用**（不需要新规则，就是 lambda 已有的规则）：`func ident::<T>(x: T): T { return x }` 时，
  `let g = ident::<int>..ident::<int>` 通过，`g(4)` 打印 `4`。
- **裸泛型名不行**，即使 `let` 上有期望类型：`let h: (x: int) => int = ident..ident` 报
  `SemanticDiag::CompOperandMismatch`（`0x030001E1`），文本里两个操作数的类型都写成 `'void'`：
  `'..' takes two functions or two integers; the left operand is 'void' and the right one 'void'.`
- 原因在实现里是显式的：`Builder::visitLinkNode` 一进来就是 `ExpectedValueScope(expectedValueType_, nullptr)`，
  它把期望类型清掉，所以访问 `..` 两个操作数时没有期望类型可用，泛型函数值停在未实例化的形状（读出来是 `void`）。
  （`src/compile/gir/builder_operators.cpp` 的 `Builder::buildComposition`。）

## 为什么切片 A 没有自己决定
- 这是"泛型实参从哪里来"的问题，落在裁定之外的语义面：`let h: (x: int) => int = ...` 已经给出了只是**赋给整个 `..` 表达式**的期望类型，
  要把它传到两个操作数上，等于给 `..` 一条 lambda 参数那样的期望类型规则；裁定只说"类型、泛型实例化、重载都照 lambda 已有的规则"，
  没说 `..` 自己要不要做期望类型分发。
- 不改的话，今天的行为是**安全的**（拒绝并给出诊断），只是文本里的 `'void'` 对用户没有说明力，也没告诉他"给类型实参"。

## 请作者裁定
1. `..` 的两个操作数是否应当用期望类型实例化（`let h: (x: int) => int = ident..ident` 通过），还是**要求显式 `<...>`**、保持今天的拒绝？
2. 若保持拒绝：是否要一条独立诊断（例如"操作数是泛型函数，请给出类型实参"），而不是让 `CompOperandMismatch` 把 `void` 当作操作数类型念出来？

## 出处
- 裁定：`docs/design/io-concurrency-streams/plan.cn.md`（只读）
- 实现：`src/compile/gir/builder_operators.cpp`（`buildComposition` / `composeFunctionValue`）、`src/compile/gir/builder.h`
- 用例：`test/cases/compile/stream/composition_uses.cml`（`map<inc..dbl>` 与 `3->inc..dbl` 是具体函数，绿）