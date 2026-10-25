# `tags` 结构体的实参记录：`w: 8` 从哪来、非整数实参还原成什么（2026-10-25）

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