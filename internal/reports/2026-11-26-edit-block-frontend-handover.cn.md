# 论文 A 车道（`edit`/`view` 前端与块降级）：现在到哪（交接）

车道 `lane/editview-a`，工作树 `D:\Projects\Camel-wt\editview-a`，分支 `lane/editview-a`，tip `29977319b`。
（块身份哈希那条审查活已转给 `lane-block-hash`，本车从此不碰 `block_form.cpp` / `block_body.cpp`；`e3d9219c7` 由它 cherry-pick。）

## 已落（按落地顺序）

| 提交 | 内容 |
|---|---|
| 前端一批 | 语法（`edit`/`view` 只在「词+名+=」三 token 时开块，别处仍是普通名字）、`EditExpr`/`ViewExpr` 两种 AST 节点、`BlockHeadLoad`、语义索引（根名只在块体内可见）、两块探针计划 |
| `80b9f1965` | AST 层脱糖：块节点保留名字，第三子 = 「`let d = f` + 体的语句」；GCT 层 `visitBlockHeaded` 与「块的值」同一条降级（无参函数 + 原位调用 `LINK(0)[FUNC[TYPE, EXEC]]`），`valueUse_` 记账一行没碰 |
| `a0b3cadad` | 探针补 `import { is_function } from types` |
| `6d3f80527` | `check_view_head` 期望随行为改成 exit 0 |
| `c8594ab0e` | `edit` 块语句列表末尾交出的不是体的值，而是块头绑的名字 |
| `f51c137f4` | 三条判据的用例改成**具体类型**（见下） |
| `29977319b` | 本文 |

## 现状（一句话）

`view` 通了；`edit` 的**类型**也通了、**值也改成“交出那个函数”**，**只差“提交”这一层**（草稿、全成/全不成、`return` 提前到块尾照常提交、逃逸检查）。

## 三条判据的读数（都在 `test/plans/probe/editview.plan.toml`）

```
① 正  edit d = f { 42 } 用在 ((x: float) => float) 位置 → PASS
② 反  同一块用在 int 位置 → 拒绝，原文（6:5）：
      [Error]: Return type '(x: float) => float' does not match the function's declared return type 'int'
③ 旁  view d = f { f } 用在函数类型 → PASS；用在 int → 拒绝
```
**⇒ 教训写进提交说明了：返回类型写成 `::<F>` 时，自由类型变量让“接受/拒绝”都不说明块的事；换成具体类型才证明块的值类型就是那个函数类型。**

读数快照（tip `f51c137f4` 之后）：`test/plans/probe` 全部 30 / 26 pass / 4 fail（4 条全是 §15 算子面：`Node`、`is_mul`、`replace`、`console`）；`feat/parse` 126 / 125 / 1（`frontend.ast_record`，在 promotion-9 红表里）。

## 下一步（唯一一件）

「草稿 + 提交」：`edit` 的值 = 提交后的 `G!`；`return` = 提前走到块尾并照常提交；体内最后一个表达式不当值；中途失败不提交、旧函数不动。判据是可观测差别（提交前旧函数仍是旧的、提交后新函数可用；“全成或全不成”用一条会中途失败的块证明旧函数没被换掉）。做完全部提交那一层后，再一起做**句柄逃逸检查**，把 `sec12_escape` 从「未验证」转成「已验证」。**`sec12_escape` 目前仍是未验证**（没有逃逸检查，它 PASS 不代表逃逸规则成立）。

## 不许

冻结 C++（`src/stdlib/autodiff/`、`rule_templates.cpp`、C++ 优化遍）不动；不为让探针变绿改写诊断文本；车道分支不推 origin；不自己造 §15 的 `Node`/`is_mul`/`replace`/`console`（那是 D 车的算子面）。