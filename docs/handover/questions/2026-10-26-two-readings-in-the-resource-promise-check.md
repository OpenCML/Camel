# 资源承诺检查里的两个口径问题（用的是合法程序，不是改红）

**问谁**：语言作者（负责人）
**问什么**：落地"红线 8"那条检查时，探针量出**两个洞**。两个都涉及口径，我们没有自己定；另一个（note 指错位置）是可判定的缺陷，施工方直接修。

## 甲｜合法程序被拒：子句写明了它唯一碰的资源，仍被拒

```camel
func shout(): int uses stdout sync { println(1) }
```
⇒ `UsesPromiseViolated: '() => int uses stdout' does not keep its resource promise: an effect outside the program`

**根因**：`covers` 把 `external` 分量一起比（`src/core/resource_semantics.cpp:371` 一处口径），而**子句没有任何语法能承诺 external**——`uses stdout` 写出来的是"访问 stdout"这一个资源，体推出的集合里却带着"这是程序外的效应"这个分量，于是**永远包含不了**。
**候选**：
- **甲**：`signatureOf` 让"**写出 ambient 资源即承诺它的 external**"（即声明 `uses stdout` 时，签名里那份同时也带上 external；**`covers` 一处口径不动**）。
- **乙**：**声明处只比访问、不比 `external`/`allocates`**（与无体声明的既有口径一致，`builder_modules.cpp:456-458`）——但这与你"**包含关系只在一处（`covers`）**"的裁定冲突。
我们倾向**甲**（不分裂那条规则）。请您定。

## 乙｜调用者按被调者的**推断集**度量，不读它的**子句**（同一个程序里两条路两种读法）

```camel
func fill(): int uses cache { return 1 }
func stamp(): int uses db { return fill() }
```
⇒ **编过**（exit 0，打印 `1`）。另一例：`func read(s: Stream, t: Stream): int uses s { return t.id }` 也编过。
**根因（逐字位置）**：调用者那一侧按被调者的**推断集**度量（`src/compile/gir/builder_resources.cpp:216-233`），而**经函数值调用**的那条读的是**类型**里的子句（同文件 `:262`）⇒ 同一个语言里两条路两种读法。
这正是问题页里列为开放语义的"声明集是超集、体从没用到"那一条。**候选**：
- **甲**：**调用者一律读被调者的子句（声明的上界）**，函数的**值**仍带推断集（你那节裁定说的是"函数值的类型带推断出的资源集"，两件事不矛盾）。
- **乙**：**一律读推断集**（那么 `uses cache` 对调用者不生效，子句只作对外的承诺、用来检查体，不参与传播）。
- **丙**：调用处取**两者并集**（`inferred ⊎ declared`）。
请您定；我们倾向**甲**（声明就是对外契约，调用者按契约算，函数值仍按实例化的推断集——这也与"资源分量进特化键"一致）。

## 丙｜（已判定为缺陷，施工方直接修，仅供您知悉）

`UsesPromiseViolated` 的 note 说"这是签名没写到的第一个使用点"，但取的是**槽序第一个效应节点**（`builder_diagnostics.cpp:349-359`），**没有筛"没被承诺"**：子句 `uses c`、体先 `c += 1` 再 `println(c)` ⇒ note 指向**已承诺**的 `c += 1`，而消息讲的是 stdout。**修法确定**：note 必须指向第一个**没被承诺**的使用点（两处不一致时以消息为准），并配一条红→绿用例。

---

**附：这一批已经落地的部分**（绿、等合入）：无子句函数值带推断集、`apply` 非同步体丢结果给出 `EffectNotExecuted`、结构体字段持函数值同理、`applyPure` 在自己子句处报 `UsesPromiseViolated`、**lambda → `pure` 形参**新增用例、教程第 10 章删掉"编译器查它能查的那些"并配反例（`examples/10_uses_beyond.cml`）、注册表 retired 空号不写原名＋`CmlUnsupportedConstruct` 去掉 "(yet)"、`when` 对常量实参照常求值（已确认无需改）。