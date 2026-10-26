# 四个"声明"用例钉的是已经退役的 `module` 机制（改名/改代码变了，不是位置）

这四条现在红着，施工方**一个字没改**（它按分类法把它们标成"待你定"）：

| 用例 | 期望（原文要点） | 现在的实际（逐字） |
|---|---|---|
| `entry_uri_prefix` | exit 0、stdout `"3\n"`（用例 `desc` 仍写"用 `module` 声明里的名字做 URI 前缀（`bytes:size`）"） | `entry_uri.cml:9:1: [Error]: The declaration 'size' names the operator 'main:size', which no loaded module implements` ＋ `UnresolvedReference 'size'` |
| `declaration_type_argument` | `NoMatchingFunction`（`0x0300000F`）、位置 `declaration_type_argument.cml:17:41`、候选 `<tensor:from_bytes_f32>: (encoded: Bytes, shape: int[]) => L` | `The declaration 'leaf_decode' names the operator 'tensor:from_bytes_f32', which another module declares`（`10:1`） |
| `declaration_per_result_type` | `SemanticDiag::Redeclaration`（`0x03000001`）、位置 `13:1` | `The declaration 'from_bytes' names the operator 'tensor:from_bytes_f32', which another module declares`（`13:1`） |
| `unselected_case_sees_declaration` | 未被选中的分支也能看到同一批名字 | 同一条"归属"拒绝 |

**根因**：删掉 `module` 声明这件事本身＋你的规则 `57e49f97d`（"一个模块只声明它实现的算子"）。`git show 91bdec167` 显示这四个用例文件当年写着 `module bytes` / `module tensor`，声明行一去掉，文件就是 `entry_uri` / `declaration_type_argument`，于是 `#op('bytes:size')` / `#op('tensor:from_bytes_f32')` **不再属于该文件自己的模块** ⇒ 报的是"名字与代码变了"，不是位置漂移。

**可选项**（要你定，我们照做）：
1. **重写这四个用例**：把声明放进真正的 `tensor` / `bytes` 声明文件里，用例改成从"别的模块声明了同名算子"的正当路径去测；
2. 或者你认为这些用例测的机制（"用 `module` 声明的名字做 URI 前缀"）**本身已退役** ⇒ 那我们把这四条**删掉**（并说明为什么删除是正确动作，而不是把红改绿）；
3. 或者这两条规则还有第三种用法，你说清楚我们就按它改。

在你答之前这四条保持红、断言不动。