> **裁定：①**（三条标签进 `prelude`，`autodiff` 反过来从 `prelude` 引入）——落地在 `d1b3531b4`。
> 页里"`prelude` 看不到 `add_jvp` 的标签"那句报错另有根因：`checkPreludeDeclarations` 造了 prelude
> 却没登记进 `modulesByPath_`，同一个文件被检查第二遍（修在 `1ab619a57`）。页不删，留着：下一个人
> 要知道这条路为什么长这样。

# `#jvp` 标注点写在哪个模块：标签有自己的命名空间，而运算符声明在 `prelude`

给标量算子挂 `#jvp(rule)` 时撞上的不是"一个位置只能有一个标签"，而是**标签名在哪个模块可见**。

## 现象到符号

- `stdlib/prelude.cml` 里把两个标签写在同一行：
  ```camel
  #op(':op/add_f') #jvp(add_jvp)
  export func operator +(x: f32, y: f32): f32
  ```
  **并排写本身没有被拒**——"一个位置只能有一个标签"这条担心不存在。被拒的是名字：
  ```
  stdlib/prelude.cml:59:19: [Error]: No tag 'jvp' is declared. A tag has a space of its own,
  separate from functions, types and values, and the tag itself is not a value: import the module
  that declares it.
  ```
  随后 `Cannot import module 'prelude': it has errors`。
- 原因：`jvp` 声明在 `stdlib/autodiff.cml#jvp`，而 `stdlib/prelude.cml` 只 `import { … } from types`。**标签按模块 import 解析，`prelude` 看不到 `autodiff` 的标签。**
- 而 `prelude` 是最底层的模块：它若 `import autodiff`，`autodiff` 自己要用 `prelude`，很可能成环。

## 为什么不能自己定

这是模块依赖方向的问题，不是写法问题：改它要么改"哪一层能依赖哪一层"，要么改"标签能否跨模块再导出"——两者都是语言层的规定。

## 选项

- **① `jvp` 标签放 `prelude.cml`**，并把 `vjp`/`transpose` 一起搬过去（同一族标签一处声明，`autodiff` 反过来从 `prelude` 引入）。**要多造**：三个 `export tag` 搬家 + `autodiff` 侧 import 调整。**会不会改变已有语义**：不改变已有程序的含义；但标签的归属模块变了，`camel inspect --tags` 的归属与文档里的位置跟着变。同族标签恰好不分家。
- **② 运算符声明搬进 `autodiff`**。**要多造**：`#op` 那批声明从 `prelude` 移走。**会不会改变已有语义**：运算子是 `prelude` 提供的最基础名字，搬走等于要求用户先 import `autodiff` 才能用内建加法——**这条会改变已有语义**。
- **③ 允许标签跨模块再导出**。**要多造**：标签命名空间上的再导出规则 + 诊断。**会不会改变已有语义**：不改变既有程序，但新增一条"标签可以转手"的规定，牵动标签命名空间的独立性。
- **④ `#jvp` 的标注点搬进 `autodiff`**：需要 `#jvp` 的运算符族在 `autodiff` 里再声明一次，`prelude` 不动。**要多造**：同一算子出现两条声明（`prelude` 一条 `#op`、`autodiff` 一条 `#op`+`#jvp`），要证明它们不会双发。**会不会改变已有语义**：不改变；代价是同一概念两处声明，与"同一棵树里同类东西只有一种写法"相冲。

**默认建议：①**。它让同族标签留在一处，且不改"最底层模块不依赖上层"这条方向。

裁定之前 `#jvp(...)` 不挂到任何 `#op` 声明上；`add_jvp` 作为普通函数已在库里。