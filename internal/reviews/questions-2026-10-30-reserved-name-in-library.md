# 库里的声明能不能叫保留名：`__add__` 与 `ReservedIdentifier`

裁定说"算子族的库名就是算子已经用的那个保留名，`__add__` 及其同族"。**今天库里的声明不能取这种名字。**

## 现象到符号

- 在 `stdlib/prelude.cml` 里照 `#op(':op/dtol')` 的形状写
  ```camel
  #op(':op/add_f')
  export func __add__(x: f32, y: f32): f32
  ```
  编译直接报 `Identifiers starting and ending with double underscores are reserved for internal use`（`SemanticDiag::ReservedIdentifier`，实测报在 `stdlib/prelude.cml` 第 7 行），随后 `Cannot import module 'prelude'`。**两条规则直接相撞**：一条说库名就是保留名，一条说库里不能写保留名。
- 保留名的证据是死的：`docs/cli.md#--ops` 的输出示例是 `OPERATOR {"module":"<builtin>","name":"__add__","uri":":op/add_l",…}`；`docs/.../02_lexical.md` 写"形如 `__x__` 的名字是编译器自己用的：运算符就是这样命名的函数（`a + b` 是 `__add__(a, b)`）"。同族还有 `__mul__`、`__sub__`、`__neg__`。
- 而已有的 `#op(...)` 声明都取普通名：`:op/mem_blocks` 叫 `std.mem.blocks`，转换一族叫 `int`/`float`/`f32`。**没有一条 `#op` 声明取保留名**。

## 为什么不能自己定

两条规则都在语言层：一条决定"这个名字是谁的"，一条决定"库能不能拿这个名字"。给其中一条开口子，等于改"保留"的含义，或者改"库名"的含义——都不是实现细节。

## 选项

- **① 只给 `#op(...)` 挂的声明开一个口子**：这类声明允许取保留名。**要多造**：解析/语义层按"是否带 `#op` 标签"放行 `ReservedIdentifier`，并写清不带标签时仍然禁止。**会不会改变已有语义**：不改变用户可见规则，但留下一处"只在一处放宽"的例外——与 `TagArgumentNotValue` 那个坑同类，下一个读到的人容易以为保留名整体可用。
- **② 库声明用普通名**（如 `plus`），`#jvp` 按 URI 关联到 `__add__`。**要多造**：一个族名 + 文档与例子 + "名字 ↔ URI"的关联规则。**会不会改变已有语义**：不改变，但同一个算子从此有两个名字（`plus` 与 `__add__`），正是这次命名页想避免的。
- **③ 保留名只作为标签的实参出现**：`#jvp(__add__)`，声明本身取普通名。**要多造**：标签实参接受保留名（与 `TagArgumentNotValue` 一并处理），声明仍叫 `add`/`plus` 之类。**会不会改变已有语义**：不改变——保留标识符规则不动，也不新增族名；与"标签里的名字不是值"这条同源。

**默认建议：③**。它不动保留标识符规则、不引入第二个名字，把"算子叫什么"和"库里怎么称呼它"分开——这正是标签该承担的事。

裁定之前 `stdlib/prelude.cml` 不新增任何算子声明；`#jvp` 仍只挂在注释与文档里。