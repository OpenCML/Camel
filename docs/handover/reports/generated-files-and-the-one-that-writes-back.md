# 生成物是一组，且有一个是反写的

**给下一个改消息文案、改窗口表、改诊断编号的人。这不是任务，是一次已经踩到的事故的说明。**

## 事实

`diagnostics/` 下的三样东西是一组，同一次生成出来，但**源不一样**：

| 产物 | 谁生成 | 源 |
|---|---|---|
| `include/camel/core/error/diagnostics/base.h` | `node scripts/diagnostics-catalog.mjs --write` | `diagnostics/registry.tsv` |
| `src/core/error/diagnostics/messages/semantic.cpp` | 同一脚本 | `diagnostics/registry.tsv` |
| `docs/diagnostics.md` | 同一脚本 | `diagnostics/registry.tsv`（示例来自 `diagnostics/examples.tsv`） |

**其中 `semantic.cpp` 是反写的**：它不是“可以手改、生成器只追加”的文件，而是每次 `--write` 都会被
`diagnostics/registry.tsv` 整段覆盖。**手改 `semantic.cpp` 的文案，下一次跑 `--write` 就被抹掉**——而且
是在你眼睛底下抹掉：`git status` 会显示那个文件“又回到原样”，你会以为是编辑器、是别的车道、是 git 出了问题。

2026-10-31 就发生过一次：为了改 `TagArgumentNotValue` 的 help 文案，先把新句子写进 `semantic.cpp`，
再跑 `--write` 想顺手刷新 `docs/diagnostics.md`，结果那 1 个被写的文件正是 `semantic.cpp` 本身，
新文案被 registry 里那一行覆盖回去，`docs/diagnostics.md` 一个字没变。同一次里还试过“先跑脚本再提交”，
同样被抹。三次尝试、两次以为是自己写错。

## 正确顺序（照这个顺序做，只有一种）

1. 改 `diagnostics/registry.tsv` 里那一行：`模块<TAB>名字<TAB>编号<TAB>严重度<TAB>消息<TAB>help<TAB>note`。
   文案、编号、严重度都在这一行。
2. 跑 `node scripts/diagnostics-catalog.mjs --write`。它把 `registry.tsv` 反写成 `semantic.cpp`（以及
   `base.h`、`docs/diagnostics.md`），并且**不要**在这之前或之后手改 `semantic.cpp`。
3. `git status` 必须同时看到 `diagnostics/registry.tsv`、`src/core/error/diagnostics/messages/semantic.cpp`、
   `docs/diagnostics.md` 三处变化（只改文案时 `base.h` 不变）。**只看到一处就是被抹了。**
4. 要改的文案若也出现在某个用例的 `expect_output_contains` 里，用例跟着改——它比的是原文，不是编号。

## 一条可检查的判据

> **`semantic.cpp` 里出现你手写的句子、而 `registry.tsv` 里没有同一句，下一次 `--write` 就会让它消失。**

遇到“我明明改了、它自己变回去了”，先跑一次 `--write` 再看 `git status` 的那三行，而不是先怀疑编辑器或别的车道。