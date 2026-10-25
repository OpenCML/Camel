# `func` 作为标签形参的类型

## Q1：标签形参的类型写 `func` 拼不出来，写 `function` 可以——哪一个才是名字？

设计原则 6.2 的例子就是 `export tag vjp<rule: func> on func`，诊断 `SemanticDiag::TagParameterType` 的帮助文字也写着"一个标签形参是 `int`、`float`、`bool`、`string` 或 `func`"。但 `func` 是关键字，类型位置不接受关键字，所以 Camel 程序写不出来：

```cml
tag rule<f: func> on field      // SyntaxDiag::KeywordAsIdentifier：'func' 是关键字，不能当名字
tag rule<f: function> on field  // 可以；编译器把 function 当作 func 的同一类（src/compile/gir/builder_tags.cpp 的 kindOfDeclaredType）
```

今天能声明 `func` 形参的只有 C++ 模块（`modules/autodiff/module.cpp` 的 `vjp` 直接写 `Decl::params`）。也就是说：Camel 里没有任何程序能贴出一个"实参是函数"的标签事实，`#vjp<rule>` 这一族用法在 Camel 一侧无法自述。

建议：**(a) 让标签声明的形参列表接受关键字 `func`**（只在这一处放宽，不动别的类型位置；`function` 与 `func` 保持同一类）。理由是设计原则 6.2 已经这么写了，实现与规范冲突，按规范改实现即可。若决定改成别的写法，请给出要写的名字——现在测试里用的是 `function`，改名字要连它一起改。

影响：不改的话，"函数实参"这条路（还原成函数本身、或在 `static` 头部报拒绝）在 Camel 里没有入口，只有 C++ 模块能到达；顺带 `#vjp` 一族的文档例子与实现不一致。

## Q2：函数实参在 `static` 头部读到时报拒绝，够不够？

同一批工作里已经落地：按形参声明的类型还原（`w: bool` 的实参读回 `bool`，`w: int` 读回 `int`，带参数与不带参数都如此），读到"实参是函数"的标签时在 `static` 头部报 `SemanticDiag::TagFunctionArgumentNotReadable`（图 id 的十进制文本不再当作 string 露出来）。两条同名标签写在同一个字段上（`#a::width<1> #b::width<2>`）报 `SemanticDiag::TagFieldNameAmbiguous`，点名两个限定名。

建议：按已落地的拒绝处理；"还原成那个函数本身"等编译期函数值时再做（`FunctionData` 今天要一个已物化的 `Program*`，`static` 展开期间没有）。可变参数（实参多于形参）也一并留到那时。

影响：不定的话，`tags` 结构体在"实参是函数"这一类字段上永远只能拒绝，函数的标签事实读不出来。