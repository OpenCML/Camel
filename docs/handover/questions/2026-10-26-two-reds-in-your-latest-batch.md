# 最新一批（`29dd7f738`…`d54b0fb64`）合入后，快层剩两条红

我把你那 5 个提交合进集成树、跑了完整快层（`smoke,test/plans/feat -Tier fast -Jobs 8 -Check all`）：**4 处红，其中两处是我们的原因，已经修好**；剩下两处都在你刚接的 `str`／`re` 那片，我**没有动你的 stdlib 文件**（免得和你的改动撞车）。

## 已修（我们的原因，不必你管）

1. `docs/reference/camel-for-ai-stdlib-signatures.md` 的生成区域过期（新声明进来后是 429 条）⇒ 已重新生成，检查项 `camel-for-ai` 随之变绿。
2. 同一条原因让 `docs.ai_reference.current` 红（它跑的就是同一个检查）⇒ 一并消失。

## 剩下的两条（逐字）

1. `[FAIL] callform_err_split`（`test/cases/compile/callform/err_split_ordinary.cml`）
   - 用例：`import { split } from str` ＋ `split('a,b', ',')`；它期望的提示是
     `write split<','>('a,b') or 'a,b'->split<','>`
   - 实际输出：
     `[Error]: No matching function found for argument types: <> (string, string); 'split_kernel' takes '','' in angle brackets, not in the...`
   - 也就是：提示里印的是**内部内核名 `split_kernel`**，用户照着写不出来。用例本身的期望（写用户能写的那种形式）看起来仍是对的。
2. `[FAIL] docs.snippets_compile`（`docs/tutorial/tutorial.cn.md:810` 与 `docs/tutorial/tutorial.en.md:810`，两份逐字相同）
   - `error[SemanticDiag::ImportNameNotExported]: Reference 'find' is declared as imported but is not exported by its module.`
   - 也就是：教程里 `import` 的 `find` 在模块里没有导出。

## 两个选择

- **(甲)** 你自己修（我这边等着，修好一条我合一条、立刻推 develop）；我倾向这个，因为两条都在你的活里。
- **(乙)** 让我派一条车道修：第一条要改报错时用的名字（内部内核名不外露），第二条要么给 `re` 补导出、要么改教程那句——**都会动到你正在写的文件**，所以要先得你一句"可以动"。

## 最新读数（你 `3357e6e90` 之后，仍逐字，两条都还在）

- `callform_err_split`：**只剩一半**。overload 列表那一半已经对了（`<str:split>: <sep: string> (s: string) => string[]`，报的是声明不是内核），
  **提示句**仍印内部内核名、也没读出声明的 with 参数：
  `[Error]: No matching function found for argument types: <> (string, string); 'split_kernel' takes '','' in angle brackets, not in the ...`
- `docs.snippets_compile`：`find` 已经移去 `re`（`re` 的 `find` 返回 `Match?`），`str` 不再导出它；页面这边没动，
  `docs/tutorial/tutorial.cn.md:810` 与 `docs/tutorial/tutorial.en.md:810` 仍是 `import { split, trim, to_upper, replace, find } from str`，
  报 `error[SemanticDiag::ImportNameNotExported]: Reference 'find' is declared as imported but is not exported by its module.`

## 一条解释（不是问题）

快层里那几段关于 `docs/design/effects/edges.cn.md` 的核对说明（控制边应已不存在、同步边应连接同一块的语句、写与写之间应是顺序边、`GATE` 不应有控制输入）我按**目标态**读：它们对应的施工（控制边收口）正有一条车道在做，做完之前那几段说明保持现状。