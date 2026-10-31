# 一个 `Ref` 值允许被哪些判定读到，读到时应答什么？

**状态**：待裁 ｜ 2026-10-31 ｜ 起因：把 `src/core/type/base.cpp` 的 `assignableFrom` 那句改成发布版生效

## 问题

`Type::assignableFrom` 里有一句：

```cpp
GUARD(to->code_ != TypeCode::Ref && from->code_ != TypeCode::Ref, "Ref type cannot be assigned");
```

它说的是：一次赋值判断的两端都不该是 `Ref`。作者对最初那四个用例裁过 A：`Ref` 是尚未解析的引用，
`assignableFrom` 不该替它作答，修法在调用方——进入赋值判断之前先 `resolve` 标注。

按这个方向做了三版，**量出来的红是 4 条的两倍还多**，所以现在要问的不是"哪个调用方忘了 resolve"，
而是：

> **一个 `Ref` 值允许被哪些判定读到？读到的时候，每个判定应当答什么？**

## `Ref` 不止是"标注还没解析"

它还是**"这个名字的值类型暂未读出"的占位**。编译器里十几处判定会拿到它：gct 的
`visitStructInto`（字段比较）、`gir` 的返回类型检查（那里遇到 `Ref` 直接跳过，注释写"types that
are not resolved yet"）、`checkDeclaredReturn`，以及 `rank_zero_*` 那一族碰到的路径。
也就是说：`Ref` 不是某一个调用点漏了解析，它是**一个跨多个判定存在的中间形态**。

## 三版读数（每版都是全量 `feat` fast，`-Jobs 8`）

| 版本 | 做法 | 读数 |
|---|---|---|
| v1 | 调用方整条字段比较跳过 → 通用路径 `visitData(ast)` | 目标 4 条绿；`rank_zero_slot_tuple_element`（nvm/fvm/jit）+ `print_and_recompile` 红 |
| v2 | 只在"值类型 ≠ 字段类型"时跳过 | 同上三条红 |
| v3 | 复合类型层豁免（`isComposite(to)` / `isOtherType(to)` 时这条不查），其余原样 | **17 条红** |

**v3 的 17 条**：`rank_zero_slot_field`（5，含 fvm/jit/tf）、`rank_zero_slot_tuple_element`（4）、
`rank_zero_slot_nested_field`（4）、`print_and_recompile`、`ort_struct_casts`、
`comprehension_context.annotated`（nvm/fvm/jit）。**其中 12 条从来没被提名过**：
`rank_zero_slot_field*`、`rank_zero_slot_nested_field*`、`ort_struct_casts`。

## 三条已量到的边界

1. **跳过比较改的是"谁按什么类型建"**：v1/v2 让字面量从"按位置类型建"翻成"按自己推出来的类型建"，
   三处用例因此换形状。所以"不相等就跳过"不是解法。
2. **复合层豁免不够**：原始类型目标在别的调用点就拿到 `Ref` 值，豁免复合目标之后 `rank_zero` 那一族仍然红。
3. **`Ref` 与 `Ref` 相等的比较是通的**（`to == from` 先返回真），红的是 `Ref` 对具体类型。

## 需要裁的

一句话就够：**`Ref` 值允许被哪些判定读到；读到时应答"不是这个类型"、"看成未知"、还是"留到解析后再判"？**
答出来之后，`assignableFrom` 这一句才谈得上变发布版生效（当前整族 57/58，就差这一处）。

## 判据

「指定的修法量出来的红是 4 条的两倍还多的时候，错的不是修法，是问法。」