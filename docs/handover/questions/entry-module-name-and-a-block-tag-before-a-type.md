# The entry module's name, and what a tag written before a namespace block reaches

> 说明（协调者）：提出问题时的代码在本地分支 `handover/h554-no-module-decl`（module 声明删除 + 模块位标签删除 +
> 命名空间块标签），基线 `origin/develop`，已并入 develop 的 `fact`/`with` 参数/标签实参拼写。
> 本页只报读数与逐字证据，不改 `docs/language/**` 散文。

**提问一，一句话**: 入口文件的模块名是文件名，还是 `main`？

**卡住什么**: 4 条 plan 条目——`test/plans/feat/compile/generics.plan.toml:8` 的 `explicit_type_arguments`（用例里写
`generic_explicit_types::id`）、`test/plans/feat/compile/namespace_rules.plan.toml:204` 的 `ns.p18_self_module`
（`p18_self_module::f()`）、同文件 `:280` 的 `ns.p30_using_own_nested`、`test/plans/feat/compile/modules_213.plan.toml:13`
的 `modules_213.self_entry`（`import self_entry` + `self_entry::twice(5)`）。删声明后它们全红：入口不是按文件名命名的模块。

**文档逐字**（`docs/language/05_modules.md`）:

> `:28`：**模块导入自己是允许的，没有效果**（设计原则 10.1）：`module m` 里写 `import m`、`import { f } from m` 不绑定任何名字，
> 它的名字本来就是它自己的（带不带 `m::` 都可以，5.4）。**入口文件按文件名导入自己时也不会再加载一份。**

> `:195`：**模块可以用自己的名字限定自己的东西**：`module shapes` 里写 `shapes::area` 与写 `area` 相同（**文件名作为模块名时也一样**）。

> `:198`：诊断、调试器与图里显示完整的名字：别的模块的函数显示为 `模块::路径::名字`，**入口模块自己的省略模块名**。

**树里逐字**（`src/service/semantic/analyze.cpp`）:

> `:512-515`：
> `// The entry module stands for the program that runs rather than for a name another module`
> `// imports, so it keeps the runtime's own name for it (`main`); every other file is a module`
> `// named by its file name (design principles 10.2).`
> `auto mainModule = std::make_shared<UserDefinedModule>("main", path, ctx, parser);`

**读数逐字**（把 `analyze.cpp` 改成按文件名构造后，重新编译，仍然）:

> `$ out/latest/bin/camel -vv /tmp/nameprobe/stem_probe.cml`
> `Start compiling module 'main' from file '/tmp/nameprobe/stem_probe.cml'`

并且 `main::f` 解析成功、`stem_probe::f` 报 `Unresolved reference`：跑起来的那份入口模块不是 `analyze.cpp` 构造的那一份，
它由另一条路径（按 `main` 装载同一个文件）给出。因此改 `analyze.cpp` 不影响 `camel <file>` 的运行，本支已回退该改动、不再改实现。

**选项与代价**:

| 选项 | 代价 |
|---|---|
| 入口也按文件名（跑起来的那份入口模块也如此） | 4 条 plan 条目转绿；`:198`「入口模块自己的省略模块名」要重写；需要找到按 `main` 装载的那条路径并让它用文件名（改动可能落在 `src/core/context/`，不在本支范围内） |
| 入口不算可导入名字（现状，`main` 只是占位） | 4 条 plan 条目要改写：用例改成不带模块名限定/不自导入；`:28`、`:195` 的「入口文件按文件名导入自己」要删掉或加例外；`native.entry_declaration_names_the_program` 已按 `main:exists` 固化 |

---

**提问二，一句话**: 写在命名空间块前面的标签（`on field` 这类），是作用于块里**类型的字段**，还是只作用于块里**直接写出的声明**？

**卡住什么**: `test/plans/feat/modules/tiles.plan.toml` 的 `gpu_scope.namespace_block_tag_propagates` 与
`test/plans/feat/compile/tags.plan.toml` 的 `block_tag_no_target` 的读法边界（两者当前都按「只作用于块里直接写出的声明」写）。

**裁定逐字**（`docs/handover/reviews/questions-2026-10-24-evening.md`，§`h49-g1-g3-open-points` 第 8 条）:

> 命名空间块上的标签：不加新位置，也不借用 `Nodes`。命名空间块是一组声明，不是标签的一个位置。**写在它前面的标签，作用于块里每一个能贴这个标签的声明**（`#gpu` 能贴函数，就作用于块里的每个函数），**内层自己写的胜出**；**块里一个能贴的声明都没有，就在标签处报错**。

**树里逐字**（`src/compile/gir/builder_tags.cpp`）:

> `:643`：`// A tag of a namespace block is read at the position of each declaration of the block:`
> `:828`：`// The tags written on the namespace blocks the declaration is written in (design principles`
> `:854`：`/// The facts the tags written on the namespace blocks of `gct` give it (design principles 6.2: a`
> `:855`：`/// tag of a function, a block or a namespace block). Every block's tags are read at the position`

实现把块的标签读成**声明自己位置上的事实**（`at == Func || Type`），所以块里的 `type` 只按 `Type` 收标签；
`on field` 写在块前，块里没有能贴 `field` 的声明，按上面第三条在标签处报 `SemanticDiag::TagNotAtPosition`（`0x03000161`）。

**选项与代价**:

| 选项 | 代价 |
|---|---|
| 只作用于块里直接写出的声明（现状） | `on field` 写在块前、块里只有类型时是错误；如果要贴到字段，只能进块写在字段上 |
| 也作用于块里类型声明的字段 | 要定义「块的事实」怎么进到类型的每个字段（现在标签位置模型只有 `Func/Type/Field/Nodes/Param`，字段不是块里的声明）；`block_tag_no_target` 的读数要改，且 `#gpu` 这类 `on Func` 的标签不受影响 |

Status: open, for the language author. Raised by the lane that owns the module-declaration removal
(handover/h554-no-module-decl, based on `origin/develop`).