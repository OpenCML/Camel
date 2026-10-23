# 标准库里同名重载的形参名不一致——要不要改名

Author: 协调者（合并闸门），2026-10-24。Status: open — 需要负责人裁定（他拥有标准库的公开名字）。测量由 `handover/h376-optim-names` 在 `docs: bind a named argument across an overload set` 落地时给出。

## 事实

`0548ce14e` 之后，**具名实参只有在"每个有此位置的声明都给它同一个名字"时才成立**（名字定位到位置，实参按名字放，随后由实参类型选重载）。因此下面这些集合今天会拒绝具名调用，而在它们各自的语义里具名调用是自然的：

| 位置 | 集合 | 名字 | 位置的含义 |
|---|---|---|---|
| `stdlib/json.cml` | `get` with[0] | `key` vs `index` | **同一个意思**（取哪个键／哪个下标，由实参类型分派） |
| `stdlib/signatures/globals.cml` | `len` | `x` vs `c` | **同一个意思** |
| `stdlib/signatures/globals.cml` | `contains` | `xs,value` / `s,sub` / `s,value` | **不同意思**（集合里有没有某个值 vs 字符串里有没有某个子串） |
| `stdlib/signatures/globals.cml` | `items` | `m` vs `s` | **同一个意思**（映射的项／集合的项） |
| `stdlib/signatures/python.cml` | `py_call` | `...args`（rest） vs `args?, kwargs?` | 元数不同（rest 与两个可选形参） |

扫描范围：`stdlib/` 与 `modules/` 共 44 个文件、93 个重载组；**没有**"形参个数相同、默认值顺序不同"的实例。

## 选项与代价

1. **只给"含义相同"的三处改名**（`json.get` 的 `index`→`key`、`len` 的 `c`→`x`、`items` 的 `s`→`m`），`contains` 与 `py_call` 保持现状并在文档里写明"这些集合不接受具名实参，因为同名位置含义不同"。代价：标准库公开名各改一处、`docs/stdlib-reference.md` 重生成、三条用例；约一小时，用户可见面只有"具名调用现在可用"。
2. **全部改名以求一致**：`contains` 的位置本义不同，改名会把"子串"叫成"值"，把 `py_call` 的 rest 与可选形参混为一谈——不建议。
3. **不改名，把"同名位置"规则放宽为"名字唯一即可定位"**（例如允许 `get(key: …)` 只要名字在整个集合里只出现在一个位置）。代价：语言面变复杂，且"同名位置含义不同"的集合会让具名调用随实参类型落到不同位置上——**不建议**，这条规则刚刚才立起来。

## 建议

**选 1。** 三处含义相同的改名让具名调用在标准库最常见的位置上可用，且不改任何语义；`contains`/`py_call` 的拒绝是规则的正确后果（位置含义不同），写进参考页比迁就更诚实。