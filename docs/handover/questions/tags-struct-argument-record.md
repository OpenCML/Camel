# `tags` 结构体的实参记录：`w: 8` 从哪来、非整数实参还原成什么（2026-10-25）

> 说明（协调者）：提出问题时的代码在本地**翻转线** `handover/h465-flip-integration`（尚未并入 develop、也未推送）；页里引用的提交号若在 develop 上查不到，属于那条线。


**问题一句话**：裁定（`docs/handover/reviews/questions-2026-10-24-evening.md` 的
`fields-third-name-tags-binding` 一节）只覆盖了 `tags` 结构体的字段名与还原类型；它没有覆盖的两处现在按
现有机制落下——同一个字段上两个标签**名字相同**时先写的留下、后写的被丢掉且不报诊断，变参标签的实参比
形参多时多出来的实参按位置命名为 `"0"`、`"1"`——**这两处要不要报诊断（或另给一个说法）？**

**卡住**：`tags` 那一族实现的收尾——`src/compile/gct/builder.cpp` 的 `visitTagDecl`（标签的形参表）、
`src/compile/gir/builder_tags.cpp` 的 `tagValueOf`（实参写成 `attr` 值）、记录构造一侧的
`StructDataFactory::add`（按字段名去重，先写先得），以及 `tags` 结构体的用法与用例。

本文件只记事实与读数，不写裁定、不猜倾向。

## 一、`{ width: { w: 8 } }` 的两个名字从哪来

- 裁定原文在 `docs/handover/reviews/questions-2026-10-24-evening.md` 的 `fields-third-name-tags-binding`
  一节。
- 外层字段名 `width`：标签的**属性键**是限定名（`src/core/tags.cpp` 的 `declare`，`decl.key = attr::key(decl.name)`，
  `decl.name = module::ident`），所以 `attr::keyName(entry.key)` 是 `M::width`；这个名字经
  `camel::core::tag::find` 找回 `tag::Decl`，取 `decl->ident` 得 `width`。
- 内层字段名 `w` 与值 `8`：`w` 是标签声明的形参名（`src/compile/gct/builder.cpp` 的 `visitTagDecl`，
  `tag.params.push_back({param.name, param.type})`）；`8` 是标签的实参值，`src/compile/gir/builder_tags.cpp`
  的 `tagValueOf` 把字面量 `8`（Int64 数据）写成 `attr::integer(8)`，读回时从 `attr::value(entry.value).integer`
  取。

## 二、还原成的类型（逐项读数）

| 实参 | 还原成 | 依据 |
|---|---|---|
| `int` | `LongData`（`int` = i64） | `src/core/type/base.cpp` 的 `{"i64", "int"}` |
| `float` | `DoubleData`（`float` = f64） | 同上，`{"f64", "float"}` |
| `string`、名字实参 | `StringData` | `tagValueOf` 把名字写成 `attr::symbol` |
| `func`（图引用） | `StringData`（图 id 的十进制文本） | `attr::Value::text` 对 Graph 就是它 |
| `bool` | **整数 0/1** | `attr` 没有 bool 种类：`tagValueOf` 对 bool 实参写 `attr::integer`，读出来是 `int`，不是 `bool` |
| 无实参 | 空记录 `{}` | 裁定第 1 条 |
| flags（只有 C++ 模块注册的键会造） | 空记录 `{}` | `attr` 的 Flags 没有对应的编译期数据种类 |

## 三、裁定没有覆盖、按现有机制落下的两处（事实）

1. 同一个字段上两个标签的**名字**相同（`#a::width<1> #b::width<2>`）时，结构体只能有一个 `width` 字段：
   `StructDataFactory::add` 按字段名去重，先写的留下，后写的被丢掉，不报诊断。
2. 变参标签的实参个数与形参个数不等：多出来的实参按位置命名为 `"0"`、`"1"`（`reads` 是 `on func`，
   字段标签今天没有这一种）。

## 四、选项

1. **（第三处第 1 条）A — 重名字段报一条诊断**（后写处）。代价：今天这类程序编译得过、读数是"先写先得"，
   加诊断之后它们变成编译错误，`tags` 与 `fields` 一族的用例要跟着看。
2. **（第三处第 1 条）B — 保持先写先得、不报诊断**。代价：后写的值无声消失（`StructDataFactory::add`
   按字段名去重），程序照常编译，写标签的人读不到任何提示。
3. **（第三处第 2 条）A — 多出来的实参给一个说法**（报诊断，或按形参名对齐）。代价：`"0"`、`"1"` 这个
   位置命名是现在落下的行为，改动要动记录构造那一侧的命名规则与已落的读数。
4. **（第三处第 2 条）B — 保持按位置命名**。代价：名字与形参名无关，读记录的人看不出它对应哪个实参
   （用户可见的后果本页没有实测）。