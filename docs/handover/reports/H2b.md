# H2b 报告：事实的统一入口、数值许可标签、`#requires`、浮点实参与声明标签的重载区分

分支 `handover/H2b-tag-facts`，从 `develop` 切出（`32ccd19d`）；全部改动都在工作区里，未提交、未建分支、未推送。构建与测试用仓库自己的脚本，日志都写在树外的 `D:\Temp\h2b`。任务书里指定的优先次序按 1→5 做完；次序引出的、评审没有定下的设计选择都写在 §8，没有在代码里替作者决定。

## 1. `#requires<...>`：规则的要求（第 1 项）

`rule` 声明前可以写 `#requires<许可, ...>`，`export` 在前后都可以：

```cml
#requires<approx_math>
rule mul_one(x: float) { x * 1.0 => x }

#requires<reassociate, contract>
export rule add_assoc(a: f32, b: f32, c: f32) { (a + b) + c <=> a + (b + c) }
```

- **语法**：`ruleDecl : frontMatter? 'export'? IDENTIFIER ...`。`parseAnnotatedStmt` 看到 `#requires` 开头的语句直接进 `parseRuleDeclRest`；`parseRuleDecl` 自己先读 front matter（`src/parse/syntax/parser.cpp`、`parser_rule.cpp`、`parser_impl.h`）。front matter 里的其他标签照常解析成 `TagUse`，由 AST 报错，不是语法错误。
- **编译期检查**：`Builder::visitRuleDecl`（`src/parse/ast/builder.cpp`）逐个 `TagRef` 取 `<...>` 里的名字，用 `tag::permissionOfName(name)` 换算成 `sem::PermissionMask` 的位，或进 `needs`；`finite_math` 展开为四项。`needs` 作为 `__rule('name', twoWay, needs, consts, lhs, rhs)` 的第 3 个实参（`modules/rewrite` 早已按索引 2 读它），引擎按节点的数值策略决定要不要应用这条规则——引擎代码一行未改。
- **不能检查的要求是定位诊断，不是静默通过**（`semantic.h` 0x16B、0x16C，另复用 0x163）：
  - 名字不是许可（拼错、或程序自己的标签）：`SemanticDiag::TagNotAPermission`，位置在**实参**上，帮助列出可写的许可名；
  - 规则前写了别的标签：`SemanticDiag::RuleTagNotRequires`，位置在那个标签名上；
  - `#requires<>`：`SemanticDiag::TagArgumentCount`（“takes 1 parameter(s), but 0 argument(s) are written”）；
  - `#requires` 写在函数前：没有名为 `requires` 的标签，走通用标签路径报 `TagUndeclared`，同样定位。
- **只认核心的标准许可名**：Camel 的 `tag` 声明在 GIR 阶段才登记，而规则要求在 AST 阶段就要成为常量，所以 `#requires` 只解析核心的许可标签（`tag::declareStandardTags()` 安装、`tag::find` 可见），程序自己声明的同名标签不改变规则要求。这条写进了 § 8，评审若要让程序标签也能作要求，需要把解析推到标签声明之后。
- 用例：`test/cases/modules/rules/requires.cml`、`requires_target.cml` 与 5 个 `err_requires_*` / `err_rule_tag.cml`；计划 7 项（2 个行为、5 个负向）全绿。行为用例证明：`#requires<approx_math>` 只改有 `@numeric(approx_math)` 的 `kept`（`scaled 1 kept 0`），不改 `scaled`；`#requires<reassociate>` 不写任何 `@numeric` 也生效（`scaled 0 kept 0`），即 7.3 的默认值没有被标签改动。

## 2. 读取事实的唯一入口（第 2 项）

`include/camel/core/tags.h` 新增：

```cpp
struct Facts {
    attr::AttrSetId tags = attr::kNoAttrs;   // 声明上的标签（节点上是属性集）
    uint8_t permissions = 0;                 // sem::PermissionMask
    std::vector<std::string> readers;        // 这些标签的 read_by，排序去重
};

Facts factsOf(const module::Module &, const Reference &name);
Facts factsOf(const module::Module &, const Reference &name, std::string_view signature);
Facts factsOfField(const module::Module &, const Reference &typeName, size_t index);
Facts factsOf(const camel::runtime::GraphDraft &, camel::runtime::gc_node_ref_t node);
Facts factsOf(const camel::runtime::GCGraph &, camel::runtime::gc_node_ref_t node);
```

一次查询同时给出标签、许可、读者；`ofDeclaration` / `ofField` 保留（内部走同一张表），C++ 模块不再分别摸属性集与许可列。改到入口的读取点：

| 读取点 | 之前 | 现在 |
|---|---|---|
| `modules/graph/read.cpp` 的 `tags` 算子（`graph.tags`） | `tag::ofDeclaration`/节点属性 | `factsOf(graph, node).tags` |
| `modules/tiles/src/compiler.cpp`、`graph_regions.cpp`（2 处）、`region_extract.cpp`（2 处） | 节点许可列 | `factsOf(...).permissions` |
| `src/passes/trans/dot/graphviz.cpp`（`std::gir`/`rgir`/`agir`/`argir`） | 属性 + 许可两处 | `factsOf(...).tags` / `.permissions` |
| `src/passes/trans/json/gir.cpp` | 同上 | 同上 |
| `src/runtime/operator_instance.cpp` | 同上 | 同上 |

**数值许可读取逐字节不变的证明**：`factsOf` 对节点返回 `permissions |= 节点列上已有的许可`；没有权限标签的节点，结果就是原来那个字节，读许可的候选集、拒绝条件、打印文本都没有变。改动前后各跑一次只含数值许可的 `feat/compile/numeric_permissions.plan.toml`：改动前 14/14 通过（`D:\Temp\h2b\numeric-baseline.log`），改动后 14/14 通过（`D:\Temp\h2b\test-numeric.log`）；另有 `tag_permissions` 计划逐节点比对 `@numeric(...)` 与标签两种写法的 `numeric=` 完全相同（§3）。`test_attrs` 是属性的**写**方（装饰器），不读声明事实；节点属性的读取仍走节点，未改语义。

## 3. 数值许可就是标签（第 3 项）

- `src/core/tags.cpp` 的 `declareStandardTags()` 安装 8 个标准标签：`reassociate`、`contract`、`approx_math`、`change_precision`、`no_nans`、`no_infs`、`no_signed_zeros`、`flush_subnormals`，位置都是 `on func, nodes`，不需要导入；`finite_math` 是组，`tag::permissionOfName("finite_math")` 把四项 OR 起来，`permissionOf(TagId)` 对它返回 0（成员各自带位）。
- `Decl.permission` 存这个标签授予的许可；`Builder::addTagFacts` 遇到权限标签时把位并进 `TagFacts.permissions`，**不**写进 `attrs=`：一个事实只放一处（许可列）。声明处另外记一个 unit 属性，`camel inspect --tags` 因此能看到它。
- 许可经由两条已有的路进入节点：`buildFuncBody` 的 `NumericPermissionScope(body->numericPermissions() | bodyTags.permissions)`，`visitExecNode` 的 `NumericPermissionScope(gct->numericPermissions() | numericPermissions_ | blockTags.permissions)`；`@numeric` 与标签从同一列落到节点，默认值仍由 `NumericPolicy` 决定（7.3：`reassociate`/`contract` 默认允许，其余须授予）。
- 声明汇总：`Module::declarationPermissions_` 按名字与签名存声明授予的许可（声明处标签的位 ∪ 函数体的 `numericPermissions()`），`tag::factsOf(module, name)` 把它和标签一起给出；`builder.cpp::visitFuncNode` 负责写，泛型实例化不会抹掉。
- 行为/结构用例 `test/cases/compile/tags/permissions.cml` 与新计划 `feat/compile/tag_permissions.plan.toml`：`#contract`、`#reassociate` 与 `@numeric(...)` 的 `numeric=` 逐字节相同，`#[reassociate, contract]` 打印 `numeric="reassociate, contract"`（7.2 的写法），`#finite_math` 打印 `finite_math`，块上的标签与函数上的相同，`plain` 为空，且这些标签不出现在 `attrs=` 里。4/4 通过（nvm/fvm/prl 行为一致 + 结构）。

## 4. `read_by` 与读者登记表（第 4 项）

- `PassDescriptor` 增加 `bool readsTags`（`include/camel/execute/pass/params.h`）；`registerModulePass` 在它为真时把路径登记进 `tag::declareReader`，并把已经指向它的别名一起登记；`registerPassAlias` 在目标已是读者时也登记别名。核心读者在 `initPassScope` 末尾登记：`std::gir`、`std::rgir`、`std::gir_regions`、`std::rgir_regions`、`std::jgir`、`std::agir`、`std::argir`、`std::opt`；模块自己的读者由模块登记（`tiles::compile`、`tensor::fuse`）。`tag::isReader(name)` 按“完全同名，或在其 `::` 之下”匹配，所以 `read_by std::opt` 命中 `std::opt::inline`。
- `src/execute/pass.cpp::tagsNeverReadWarnings` 现在只把 `tag::isReader` 认可的名字算作读者：一个名字恰好在流水线里、但并不读标签事实的遍，不再掩盖警告；消息与帮助文字未改，`tags.plan.toml` 的 `read_by.present` / `read_by.absent` 都绿。
- 未做：Camel 写的遍（`registerCamelPass`）还没有声明自己读标签的入口，`read_by` 点到它仍会警告；见 §8 第 5 条。

## 5. 浮点实参、实参种别与声明标签的重载区分（第 5 项）

- **浮点实参**：`attr::ValueKind::Float`（`include/camel/core/node_attrs.h`），`attr::real(double)` 按位（`std::bit_cast<uint64_t>`）交接，`valueText` 用 `camel::utils::floatText`，`1.5` 打印为 `1.5`；`0.0`/`-0.0`、两个不同 NaN 是不同的事实。`builder_tags.cpp` 接受 `TypeCode::Float32` / `Float64`。用例 `float.cml` 的 `inspect --tags` 输出 `T : main::ratio=1.5`、`U : main::ratio=0.25`。
- **实参必须是形参声明的那种常量**：`kindOfDeclaredType` 把 `int`/`float`/`bool`/`string`（及 `i32`、`f64` 等写法）归一，`kindOfValue` 读实参的数据类型；不符即 `SemanticDiag::TagArgumentType`（0x16A），位置在标签上，消息给出形参名、标签名、声明的类型、写下的实参与实际种别（浮点用 `floatText`，不再出现 `1.500000`）。
- **声明载不了的形参类型**：`SemanticDiag::TagParameterType`（0x16D），在写下标签的地方报“形参是 int/float/bool/string 之一”。这样 `#vjp<rule: func>` 一类（设计原则 6.2 已写、实现未到）是明确诊断，而不是把实参悄悄当成别的值。`TagArgumentUnsupported` 保留给“实参本身不是可载的值”（函数值、复合值），帮助文字相应改成“整数、浮点、字符串、布尔（0/1）、符号或图引用”。
- **声明标签按重载区分**：`Module` 的声明标签与许可都从 `map<name, AttrSetId>` 变成 `map<name, map<signature, ...>>`；`Builder::visitFuncNode` 只为可重载的算子组（`isOverloadableOperatorGroup`）计算 `FunctionType::toString()` 作签名，其余声明仍用空签名。签名唯一时 dump 输出逐字未变。
- 用例：`float.cml`（正向）、`err_type.cml`（`bool` 给 `int`）、`err_param_type.cml`（`(x: int) => int` 形参）、`err_float.cml`（`1.5` 给 `int`）；计划见 §7。

## 6. 改动过的期望值

只有一处既有期望被改，且是任务书要求的行为变化：

- `test/plans/feat/compile/tags.plan.toml` 的 `unsupported_argument`（原 `err_float.cml`，期望 `SemanticDiag::TagArgumentUnsupported`、“has a type a tag cannot carry yet”）拆成：
  - `float_argument`（正向，`float.cml`）；
  - `argument_type`（同一 `err_float.cml`，期望 `SemanticDiag::TagArgumentType`、`The parameter 'w' of the tag 'width' is declared 'int', but the argument is '1.5' (float).`）；
  - `argument_type_bool_for_int`（`err_type.cml`）；
  - `parameter_type`（`err_param_type.cml`）。

理由：任务第 5 项要求浮点实参可载、且实参必须是形参声明的那种常量；`err_float.cml` 的形参声明的是 `int`，所以浮点实参现在是**种别不符**，不再是“标签还载不了的类型”。旧消息在新语义下是错的，不能保留。其余计划的期望值一字未动；`docs/diagnostics.md` 与语法表是生成结果，按脚本重生成。

## 7. 验证

日志都在 `D:\Temp\h2b`。

| 计划 | 结果 |
|---|---|
| `feat/compile/numeric_permissions.plan.toml`（改动前基线） | 14/14（`numeric-baseline.log`） |
| `feat/compile/numeric_permissions.plan.toml`（改动后） | 14/14（`test-numeric.log`） |
| `feat/compile/tags.plan.toml` | 19/19（`test-tags.log`） |
| `feat/compile/tag_permissions.plan.toml`（新） | 4/4（`test-tagperm.log`） |
| `feat/compile`（整个目录） | 885/885（`test-compile.log`） |
| `feat/modules/rules.plan.toml` | 21/21（`test-rules.log`） |
| `feat/modules`（整个目录） | 936 通过 / 19 跳过 / 0 失败（`test-modules.log`） |
| `feat/opt`（整个目录，属性/CSE） | 679/679（`test-opt.log`） |
| `feat/consistency`（整个目录） | 988/988（`test-consistency.log`） |
| `feat/parse/rules.plan.toml` | 7/7（`test-parse-rules.log`） |
| `smoke` | 10/10（`test-smoke.log`） |

构建 `camel-build.ps1` 最后一次 exit 0（`build-5.log`，103 s + 20 s 两次增量）。跑过的完整套件之外没有跑全量、poison、GC 压力、progen 或 PyTorch 对比。`feat/parse/rules` 绿，说明语法表的改动没有让既有规则用例的 AST 变；`ast_hashes.txt` 无需改。

## 8. 未决问题（需要作者定，不在代码里替定）

1. **`nondeterministic` 没有许可位**。设计原则 7.2 列了它，7.3 说“不允许，用标签授予”，但 `sem::PermissionMask` 是 8 位且已满（`parsePermission` 也没有这个名字）。因此 `#nondeterministic` / `#requires<nondeterministic>` 现在报 `TagNotAPermission` / `TagUndeclared`。要支持它，必须由作者决定是扩到 16 位还是另设一列；本次没有擅自扩掩码。
2. **函数值实参**。6.2 的 `#vjp<rule: func>`、rewrite 设计里的规则引用都要把函数名作为标签实参；现在这会在**形参类型**处报 `TagParameterType`（或实参不是可载值时 `TagArgumentUnsupported`）。需要 `attr` 里能存图/函数引用、以及数据层的函数值，属下一阶段。
3. **`#requires` 只认核心许可名**（§1 末）。程序自己的 `tag` 在 AST 之后才登记，所以不能作规则要求；若要允许，解析要移到标签声明之后（GIR），或者让 AST 先收标签声明。
4. **重载区分只对算子组**。同一名字的用户函数若也能重载，声明标签仍按名字合并；现在没有这种用例，也没有为它发明键。
5. **Camel 遍不能声明 `readsTags`**。`registerCamelPass` 没有对应入口，`read_by` 点到 Camel 写的遍仍会警告；需要作者定一种“Camel 遍如何声明读标签”（例如在模块里调用 `tag::declareReader`，或给 `registerCamelPass` 加参数）。
6. **不同 NaN 的文本相同**。事实按位区分（正确），但 `inspect --tags` 与 `attr::text` 都打印 `nan`，同一份输出可能对应两个不同事实；这是 dumps 的可读性问题，不是求值问题。
7. **`#requires` 与本地同名标签**：`#requires<contract>` 永远指核心许可，即使程序里声明了一个也叫 `contract` 的标签；帮助文字说“A requirement names a permission”，没有说明遮蔽规则，文档 §7.6.2 已写“只有许可名”。

## 9. 改动文件

核心：`include/camel/core/tags.h`、`src/core/tags.cpp`、`include/camel/core/module/module.h`、`src/core/module/module.cpp`、`include/camel/core/node_attrs.h`、`src/core/node_attrs.cpp`、`include/camel/execute/pass/params.h`、`src/execute/pass.cpp`、`include/camel/core/error/diagnostics/messages/semantic.h`、`src/core/error/diagnostics/messages/semantic.cpp`。

编译与解析：`src/compile/gir/builder.{h,cpp}`、`builder_tags.cpp`、`builder_generics.cpp`、`src/parse/ast/builder.cpp`、`src/parse/syntax/parser.cpp`、`parser_rule.cpp`、`parser_impl.h`。

读取点：`modules/graph/read.cpp`、`modules/tiles/src/{compiler,graph_regions,region_extract,region_pass}.cpp`、`modules/tensor/passes/fuse.cpp`、`src/passes/trans/dot/graphviz.cpp`、`src/passes/trans/json/gir.cpp`、`src/runtime/operator_instance.cpp`。

测试：`test/cases/compile/tags/{permissions,float,err_type,err_param_type}.cml`（新）、`test/cases/modules/rules/{requires,requires_target,err_requires_*,err_rule_tag}.cml`（新）、`test/cases/modules/rules/counts.cml`（加 `report_requires`）、`test/plans/feat/compile/tag_permissions.plan.toml`（新）、`tags.plan.toml`、`rules.plan.toml`。

文档：`docs/language/07_statements.md` §7.6.2 与 `en/` 对应节、`docs/language/08_top_level_and_grammar.md` 与 `en/`（生成）、`docs/technical/46_rules.md`、`docs/diagnostics.md`（生成，182 条）。